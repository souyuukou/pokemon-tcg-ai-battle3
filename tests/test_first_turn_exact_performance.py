import time
from pathlib import Path

from cg.api import (exact_decide_v2, exact_load_evaluator_model,
                    exact_unload_evaluator_model, to_observation_class)
from cg.game import battle_finish, battle_select, battle_start_seeded

from exact_solver.profile import load_profile


ROOT = Path(__file__).resolve().parents[1]


def _seed6_first_main(deck: list[int]) -> dict:
    observation, start = battle_start_seeded(deck, deck, 6)
    assert observation is not None, (start.errorPlayer, start.errorType)
    for _ in range(40):
        select = observation.get("select")
        if select and select["type"] == 0 and select["context"] == 0:
            return observation
        action = deck if select is None else list(range(int(select["minCount"])))
        observation = battle_select(action)
    raise AssertionError("seed 6 did not reach its first main phase")


def test_seed6_first_turn_is_exact_and_finishes_inside_submission_budget():
    profile = load_profile()
    deck = list(profile.cards)
    model = ROOT / "sample_submission" / "sample_submission" / "exact-evaluator-v3.bin"
    exact_load_evaluator_model(str(model))
    observation = _seed6_first_main(deck)
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
        assert result["successorMerges"] > 200_000
        assert result["peakRssBytes"] < 3 * 1024**3
        assert all(action["certified"] for action in result["rootActions"])

        # Playing Poké Pad before Dunsparce and doing the same operations in the
        # opposite order are an exact turn-one transposition for this deck.
        by_option = {tuple(action["selected"]): action for action in result["rootActions"]}
        assert by_option[(0,)]["lowerNumerator"] == 13_415_503_804_231
        assert by_option[(0,)]["lowerDenominator"] == 433_160
        assert by_option[(0,)]["lowerNumerator"] == by_option[(1,)]["lowerNumerator"]
        assert by_option[(0,)]["lowerDenominator"] == by_option[(1,)]["lowerDenominator"]
    finally:
        battle_finish()
        exact_unload_evaluator_model()
