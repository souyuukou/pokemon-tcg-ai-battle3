import os
import threading
import time

import pytest

from cg.api import (exact_decide, exact_decide_v2, exact_turn_advance,
                    exact_turn_begin, exact_turn_progress, exact_turn_release,
                    to_observation_class)
from cg.game import battle_finish, battle_select, battle_start_seeded
from exact_solver.profile import load_profile


def _seeded_first_main(seed: int):
    profile = load_profile()
    deck = list(profile.cards)
    observation, start = battle_start_seeded(deck, deck, seed)
    assert observation is not None, (start.errorPlayer, start.errorType)
    for _ in range(30):
        select = observation.get("select")
        current = observation["current"]
        if select and current["turn"] >= 1 and select["type"] == 0 and select["context"] == 0:
            return profile, deck, observation
        action = deck if select is None else list(range(select["maxCount"]))
        observation = battle_select(action)
    raise AssertionError("seeded battle did not reach the first main selection")


def _hand_values(profile):
    values = profile.evaluator["hand_values"]
    return [int(values.get(str(card_id), values.get("default", 100))) for card_id in profile.cards]


def _seeded_second_main(seed: int = 6):
    """Replay the certified turn-one policy without re-running its 132s proof."""
    profile, deck, observation = _seeded_first_main(seed)
    assert seed == 6, "the recorded exact policy is a seed-6 regression fixture"
    for action in ([0], [5], [0], [0]):
        observation = battle_select(action)
    for _ in range(30):
        select = observation.get("select")
        if select and select["type"] == 0 and select["context"] == 0:
            return profile, deck, observation
        observation = battle_select(deck if select is None else list(range(select["maxCount"])))
    raise AssertionError("seeded battle did not reach the second main selection")


def test_seeded_battle_is_reproducible_and_contains_deck_search():
    try:
        profile, deck, first = _seeded_first_main(6)
        first_signature = (first["current"], first["select"])
    finally:
        battle_finish()
    try:
        _, _, second = _seeded_first_main(6)
        assert (second["current"], second["select"]) == first_signature
        me = second["current"]["players"][second["current"]["yourIndex"]]
        playable_ids = [me["hand"][option["index"]]["id"]
                        for option in second["select"]["option"] if option["type"] == 7]
        assert 1152 in playable_ids  # Poke Pad performs a deck search.
    finally:
        battle_finish()


def test_exact_decide_v2_accepts_known_opponent_profile():
    try:
        profile, deck, observation = _seeded_first_main(6)
        result = exact_decide_v2(to_observation_class(observation), deck, _hand_values(profile),
                                 50, opponent_deck=deck)
        assert result["selected"]
        assert result["unknownOpponentListNodes"] == 0
    finally:
        battle_finish()


def test_completed_turn_policy_reroots_without_research():
    session_id = None
    try:
        profile, deck, observation = _seeded_first_main(21)
        assert len(observation["select"]["option"]) == 1
        first = exact_turn_begin(to_observation_class(observation), deck, _hand_values(profile),
                                 1_000, opponent_deck=deck)
        session_id = first["sessionId"]
        second = exact_turn_advance(session_id, to_observation_class(observation), 100)
        # Progress is read-only and exposes the canonical-DAG diagnostics without
        # consuming another search slice.
        progress = exact_turn_progress(session_id)
        assert progress["sessionId"] == session_id
        assert progress["expandedNodes"] == second["expandedNodes"]
        assert progress["sessionBytes"] == second["sessionBytes"]
        assert first["selected"] == second["selected"] == [0]
        assert first["certified"] is second["certified"] is True
        assert second["policyHits"] >= 1
        assert second["avoidedExpandedNodes"] >= 1
        assert second["resumedNodes"] == 0
    finally:
        if session_id is not None:
            exact_turn_release(session_id)
        battle_finish()


def test_turn2_reports_every_root_interval_and_certifies_end():
    session_id = None
    try:
        profile, deck, observation = _seeded_second_main()
        result = exact_turn_begin(to_observation_class(observation), deck, _hand_values(profile),
                                  1_000, opponent_deck=deck)
        session_id = result["sessionId"]
        roots = {tuple(item["selected"]): item for item in result["rootActions"]}
        assert set(roots) == {(0,), (1,), (2,), (3,), (4,)}
        assert roots[(4,)]["certified"] is True
        assert roots[(4,)]["lowerNumerator"] == roots[(4,)]["upperNumerator"] == -3520
        assert result["selected"] == [4]
        assert result["rootRetryKeyMismatches"] == 0
        assert result["successorMerges"] >= 1  # duplicate Telepath Energy copy
    finally:
        if session_id is not None:
            exact_turn_release(session_id)
        battle_finish()


@pytest.mark.skipif(os.environ.get("PTCG_RUN_SLOW_EXACT") != "1",
                    reason="set PTCG_RUN_SLOW_EXACT=1 for the 600-second acceptance run")
def test_seed6_first_main_is_fully_certified_under_match_limits():
    try:
        profile, deck, observation = _seeded_first_main(6)
        import psutil
        process = psutil.Process()
        peak_rss = process.memory_info().rss
        stopped = threading.Event()
        def sample_memory():
            nonlocal peak_rss
            while not stopped.wait(0.02):
                peak_rss = max(peak_rss, process.memory_info().rss)
        monitor = threading.Thread(target=sample_memory, daemon=True)
        monitor.start()
        started = time.perf_counter()
        try:
            result = exact_decide(to_observation_class(observation), deck, _hand_values(profile), 570_000)
        finally:
            stopped.set()
            monitor.join()
        elapsed = time.perf_counter() - started
        assert elapsed < 570
        assert result["certified"] is True
        assert result["lowerNumerator"] * result["upperDenominator"] == \
               result["upperNumerator"] * result["lowerDenominator"]
        assert result["opaqueNodes"] == 0
        assert result["timedOut"] is False
        assert result["arithmeticOverflow"] is False
        assert result["leafNodes"] > 0
        assert result["selected"] == [0]
        assert result["rootWorkers"] <= 2
        assert peak_rss < 3 * 1024**3
    finally:
        battle_finish()


@pytest.mark.skipif(os.environ.get("PTCG_RUN_SLOW_EXACT") != "1",
                    reason="set PTCG_RUN_SLOW_EXACT=1 for the turn-policy acceptance run")
def test_seed6_completed_policy_serves_the_rest_of_the_turn():
    session_id = None
    try:
        profile, deck, observation = _seeded_first_main(6)
        first = exact_turn_begin(to_observation_class(observation), deck, _hand_values(profile), 570_000)
        session_id = first["sessionId"]
        assert first["certified"] is True
        assert first["selected"] == [0]
        assert first["policyNodes"] > 0
        turn = observation["current"]["turn"]
        observation = battle_select(first["selected"])
        calls = 0
        while observation.get("select") is not None and observation["current"]["turn"] == turn:
            started = time.perf_counter()
            result = exact_turn_advance(session_id, to_observation_class(observation), 10_000)
            assert time.perf_counter() - started < 0.1
            assert result["certified"] is True
            assert result["policyHits"] > calls
            assert result["resumedNodes"] == 0
            calls += 1
            observation = battle_select(result["selected"])
        assert calls >= 1
    finally:
        if session_id is not None:
            exact_turn_release(session_id)
        battle_finish()
