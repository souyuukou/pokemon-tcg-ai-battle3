from pathlib import Path
import os

from cg.api import (exact_decide_v2, exact_evaluate_action_v2, exact_load_evaluator_model,
                    exact_unload_evaluator_model, to_observation_class)
from cg.game import battle_finish, battle_select, battle_start_seeded

from exact_solver.profile import load_profile


ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "sample_submission" / "sample_submission" / "exact-evaluator-v3.bin"

# Tiny fixed deck: few distinct IDs so multi-draw outcomes certify quickly.
# 40 basic Psychic energy + Enriching Energy + a handful of Basics/Items.
ORACLE_DECK = (
    [5] * 40
    + [13]  # Enriching Energy
    + [741] * 4  # Abra
    + [305] * 4  # Dunsparce
    + [1086] * 4  # Buddy-Buddy Poffin
    + [1225] * 4  # Hilda
    + [1266] * 3  # Nighttime Mine
)


def _fraction(result, side):
    return int(result[f"{side}Numerator"]), int(result[f"{side}Denominator"])


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


def _first_main(deck, seed):
    observation, start = battle_start_seeded(deck, deck, seed)
    assert observation is not None, (start.errorPlayer, start.errorType)
    for _ in range(50):
        select = observation.get("select") or {}
        if select.get("type") == 0 and select.get("context") == 0:
            return observation
        observation = battle_select(list(range(int(select.get("minCount", 0)))))
    raise AssertionError(f"seed {seed} did not reach a Main decision")


def test_skeleton_default_is_off_without_env():
    """Skeleton sharing must stay opt-in; unset env uses the legacy path."""
    os.environ.pop("PTCG_EXACT_SKELETON", None)
    deck = list(load_profile().cards)
    observation = _first_main(deck, 6)
    exact_load_evaluator_model(str(MODEL))
    try:
        result = exact_decide_v2(
            to_observation_class(observation), deck, [100] * len(deck), 30_000,
            opponent_deck=deck,
        )
        assert result["certified"] is True
        assert result.get("skeletonExpansions", 0) == 0
        assert result.get("skeletonClasses", 0) == 0
    finally:
        battle_finish()
        exact_unload_evaluator_model()


def test_oracle_deck_skeleton_parity_unconditional():
    """OFF/ON must certify with identical fractions on a tiny oracle deck."""
    exact_load_evaluator_model(str(MODEL))
    try:
        observation = _first_main(ORACLE_DECK, 0)
        os.environ["PTCG_EXACT_SKELETON"] = "0"
        legacy = exact_decide_v2(
            to_observation_class(observation), ORACLE_DECK, [100] * len(ORACLE_DECK), 15_000,
            opponent_deck=ORACLE_DECK,
        )
        os.environ["PTCG_EXACT_SKELETON"] = "1"
        shared = exact_decide_v2(
            to_observation_class(observation), ORACLE_DECK, [100] * len(ORACLE_DECK), 15_000,
            opponent_deck=ORACLE_DECK,
        )
        assert legacy["certified"] is True, legacy
        assert shared["certified"] is True, shared
        assert legacy["selected"] == shared["selected"]
        assert _fraction(legacy, "lower") == _fraction(shared, "lower")
        assert _fraction(legacy, "upper") == _fraction(shared, "upper")
        assert legacy["chanceMassMismatches"] == 0
        assert shared["chanceMassMismatches"] == 0
        assert shared["opaqueNodes"] == 0
        assert shared["exceptionNodes"] == 0
    finally:
        os.environ.pop("PTCG_EXACT_SKELETON", None)
        battle_finish()
        exact_unload_evaluator_model()


def test_rich_energy_reports_skeleton_diagnostics_when_enabled():
    deck, observation, rich = _seed4_rich_energy_main()
    exact_load_evaluator_model(str(MODEL))
    try:
        os.environ["PTCG_EXACT_SKELETON"] = "1"
        result = exact_evaluate_action_v2(
            to_observation_class(observation), deck, [100] * len(deck), 1_500,
            rich, opponent_deck=deck,
        )
        assert result["selected"] == [rich]
        assert result["continuationDraws"] >= 1
        assert result["probabilityExact"] is True
        assert result["turnInertIdentities"] >= 1
        assert result["chanceMassMismatches"] == 0
        assert result["opaqueNodes"] == 0
        assert result["exceptionNodes"] == 0
        # With skeleton enabled, classing must actually run (or fall back cleanly).
        assert (
            result.get("skeletonClasses", 0) >= 1
            or result.get("skeletonGuardFallbacks", 0) >= 1
            or result.get("skeletonExpansions", 0) >= 1
            or result.get("partialChanceNodes", 0) >= 1
        )
        assert result.get("certificationScope") in (
            "exact_evaluator_expectation", "argmax",
        )
    finally:
        os.environ.pop("PTCG_EXACT_SKELETON", None)
        battle_finish()
        exact_unload_evaluator_model()
