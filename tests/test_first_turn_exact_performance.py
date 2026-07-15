import time
from pathlib import Path

from cg.api import (exact_decide_v2, exact_load_evaluator_model,
                    exact_unload_evaluator_model, to_observation_class)
from cg.game import battle_finish, battle_select, battle_start_seeded

from exact_solver.profile import load_profile


ROOT = Path(__file__).resolve().parents[1]

WATER_SEARCH_DECK = (
    [721] * 2 + [722] * 4 + [723] * 4 + [1092] + [1121] * 2
    + [1145] * 2 + [1163] * 2 + [1219] * 4 + [1227] * 4
    + [1262] * 2 + [3] * 33
)


def _first_main(deck: list[int], seed: int) -> dict:
    observation, start = battle_start_seeded(deck, deck, seed)
    assert observation is not None, (start.errorPlayer, start.errorType)
    for _ in range(40):
        select = observation.get("select")
        if select and select["type"] == 0 and select["context"] == 0:
            return observation
        action = deck if select is None else list(range(int(select["minCount"])))
        observation = battle_select(action)
    raise AssertionError(f"seed {seed} did not reach its first main phase")


def test_seed6_first_turn_is_exact_and_finishes_inside_submission_budget():
    profile = load_profile()
    deck = list(profile.cards)
    model = ROOT / "sample_submission" / "sample_submission" / "exact-evaluator-v3.bin"
    exact_load_evaluator_model(str(model))
    observation = _first_main(deck, 6)
    try:
        started = time.perf_counter()
        result = exact_decide_v2(
            to_observation_class(observation), deck, [100] * len(deck), 90_000,
            opponent_deck=deck,
        )
        elapsed = time.perf_counter() - started

        assert elapsed < 90
        assert result["certified"] is True
        assert result["probabilityExact"] is True
        assert result["informationSetSafe"] is True
        assert result["timedOut"] is False
        assert result["memoryLimitReached"] is False
        assert result["opaqueNodes"] == 0
        assert result["exceptionNodes"] == 0
        assert result["chanceMassMismatches"] == 0
        assert result["hiddenInformationLeakDetected"] is False
        assert result["rootWorkers"] == 2
        # Dynamic Card Partition reveal outcomes are combined analytically before they
        # become physical successor States, so the old 200k post-step merge
        # count is intentionally gone.  The remaining semantic Main states must
        # still hit the collision-safe canonical cache.
        assert result["canonicalStateMerges"] > 1_000
        assert result["enumeratedHiddenWorlds"] < 2_000
        assert result["expandedNodes"] < 100_000
        assert result["dynamicPartitionBuilds"] > 0
        assert result["dynamicPartitionMaxClasses"] > 1
        assert result["dynamicPartitionMaxVisibleIdentities"] > 0
        assert result["peakRssBytes"] < 3 * 1024**3
        assert all(action["certified"] for action in result["rootActions"])

        # Playing Poké Pad before Dunsparce and doing the same operations in the
        # opposite order are an exact turn-one transposition for this deck.
        by_option = {tuple(action["selected"]): action for action in result["rootActions"]}
        assert by_option[(0,)]["lowerNumerator"] == 14_071_521_832_061
        assert by_option[(0,)]["lowerDenominator"] == 433_160
        assert by_option[(0,)]["lowerNumerator"] == by_option[(1,)]["lowerNumerator"]
        assert by_option[(0,)]["lowerDenominator"] == by_option[(1,)]["lowerDenominator"]
    finally:
        battle_finish()
        exact_unload_evaluator_model()


def test_dynamic_partition_is_monotone_across_multiple_turn_one_queries():
    """A later predicate must not hide IDs exposed by an earlier search."""
    profile = load_profile()
    deck = list(profile.cards)
    model = ROOT / "sample_submission" / "sample_submission" / "exact-evaluator-v3.bin"
    exact_load_evaluator_model(str(model))
    observation = _first_main(deck, 0)
    try:
        result = exact_decide_v2(
            to_observation_class(observation), deck, [100] * len(deck), 30_000,
            opponent_deck=deck,
        )
        assert result["certified"] is True
        assert result["chanceMassMismatches"] == 0
        assert result["dynamicPartitionBuilds"] > 0
        assert result["dynamicPartitionMaxClasses"] == 7
        assert result["dynamicPartitionMaxVisibleIdentities"] == 6
        assert result["enumeratedHiddenWorlds"] < 15_000

        values = {
            tuple(action["selected"]): (
                action["lowerNumerator"], action["lowerDenominator"]
            )
            for action in result["rootActions"]
        }
        assert values == {
            (0,): (77_473_748, 1),
            (1,): (77_473_748, 1),
            (2,): (73_137_200, 1),
            (3,): (77_473_748, 1),
            (4,): (77_473_748, 1),
            (5,): (61_022_592, 1),
        }
    finally:
        battle_finish()
        exact_unload_evaluator_model()


def test_dynamic_partition_is_not_tied_to_the_submission_deck_ids():
    model = ROOT / "sample_submission" / "sample_submission" / "exact-evaluator-v3.bin"
    exact_load_evaluator_model(str(model))
    observation = _first_main(WATER_SEARCH_DECK, 0)
    try:
        result = exact_decide_v2(
            to_observation_class(observation), WATER_SEARCH_DECK,
            [100] * len(WATER_SEARCH_DECK), 1_000,
            opponent_deck=WATER_SEARCH_DECK,
        )
        assert result["probabilityExact"] is True
        assert result["chanceMassMismatches"] == 0
        assert result["dynamicPartitionBuilds"] > 0
        assert result["dynamicPartitionMaxClasses"] > 1
        assert result["dynamicPartitionFallbacks"] == 0
        assert result["provisionalOpponentPolicyNodes"] == 0
    finally:
        battle_finish()
        exact_unload_evaluator_model()
