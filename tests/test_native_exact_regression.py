import os
import threading
import time

import pytest

from cg.api import exact_decide, exact_decide_v2, to_observation_class
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
