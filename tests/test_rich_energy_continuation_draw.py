from pathlib import Path

from cg.api import (exact_evaluate_action_v2, exact_load_evaluator_model,
                    exact_unload_evaluator_model, to_observation_class)
from cg.game import battle_finish, battle_select, battle_start_seeded

from exact_solver.profile import load_profile


ROOT = Path(__file__).resolve().parents[1]


def _seed4_rich_energy_main():
    deck = list(load_profile().cards)
    observation, start = battle_start_seeded(deck, deck, 4)
    assert observation is not None, (start.errorPlayer, start.errorType)
    for _ in range(50):
        select = observation.get("select") or {}
        if select.get("type") == 0 and select.get("context") == 0:
            current = observation["current"]
            hand = current["players"][current["yourIndex"]]["hand"]
            rich = next(
                index for index, option in enumerate(select["option"])
                if option["type"] == 8 and hand[option["index"]]["id"] == 13
            )
            return deck, observation, rich
        observation = battle_select(list(range(int(select.get("minCount", 0)))))
    raise AssertionError("seed 4 did not reach the Rich Energy main decision")


def test_rich_energy_draw_four_uses_exact_continuation_class_kernel():
    """Draw four must not fall back to four sequential identity reveals."""
    deck, observation, rich = _seed4_rich_energy_main()
    model = ROOT / "sample_submission" / "sample_submission" / "exact-evaluator-v3.bin"
    exact_load_evaluator_model(str(model))
    try:
        result = exact_evaluate_action_v2(
            to_observation_class(observation), deck, [100] * len(deck), 1_000,
            rich, opponent_deck=deck,
        )
        assert result["selected"] == [rich]
        assert result["continuationDraws"] >= 1
        assert result["continuationDrawClasses"] >= 1
        assert result["continuationClassOutcomes"] >= 1
        assert result["probabilityExact"] is True
        assert result["chanceMassMismatches"] == 0
        assert result["opaqueNodes"] == 0
        assert result["exceptionNodes"] == 0
        # A short diagnostic slice may stop inside the first continuation, but
        # the class kernel itself must preserve exact mass and resumable state.
        assert result["partialChanceNodes"] >= 1
    finally:
        battle_finish()
        exact_unload_evaluator_model()
