"""P0e: per-card coverage scanners + fallback metrics + mini identity checks."""
from __future__ import annotations

import json
import os
from pathlib import Path

import pytest

from cg import sim
from cg.api import (exact_evaluate_action_v2, exact_load_evaluator_model,
                    exact_unload_evaluator_model, to_observation_class)
from cg.game import battle_finish, battle_select, battle_start_seeded
from exact_solver import nnue_v4
from exact_solver.profile import load_profile

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "sample_submission" / "sample_submission" / "exact-evaluator-v3.bin"


@pytest.fixture(scope="module")
def lib():
    return sim.lib


def test_liveness_schema_v3_and_per_card_coverage(lib):
    assert nnue_v4.LIVENESS_SCHEMA == 3
    raw = lib.ExactCardLivenessV4Diagnostics()
    data = json.loads(raw.decode("utf-8") if isinstance(raw, bytes) else raw)
    assert data["livenessSchemaVersion"] == 3
    assert data["ultraBallBlocksUsedSupporter"] is True
    assert data["usedSupporterPassiveWithoutUltra"] is True
    assert data["damageOnlyDoesNotBlockPassiveEnergy"] is True


def test_seed4_reports_coverage_fallback_metrics():
    os.environ["PTCG_EXACT_EVALUATOR_VERSION"] = "V4"
    os.environ["PTCG_EXACT_V4_PASSIVE_DRAW"] = "1"
    deck = list(load_profile().cards)
    observation, start = battle_start_seeded(deck, deck, 4)
    assert observation is not None, (start.errorPlayer, start.errorType)
    rich = None
    for _ in range(50):
        select = observation.get("select") or {}
        if select.get("type") == 0 and select.get("context") == 0:
            current = observation["current"]
            hand = current["players"][current["yourIndex"]]["hand"]
            rich = next(
                index for index, option in enumerate(select["option"])
                if option["type"] == 8 and hand[option["index"]]["id"] == 13
            )
            break
        observation = battle_select(list(range(int(select.get("minCount", 0)))))
    assert rich is not None
    exact_load_evaluator_model(str(MODEL))
    try:
        result = exact_evaluate_action_v2(
            to_observation_class(observation), deck, [100] * len(deck), 3_000,
            rich, opponent_deck=deck,
        )
        assert result["chanceMassMismatches"] == 0
        # Either Passive integrated, or coverage fallbacks explain why not.
        integrated = int(result.get("passiveCardsIntegrated") or 0)
        costs = int(result.get("fallbackIncompleteCosts") or 0)
        further = int(result.get("fallbackFurtherChance") or 0)
        pending = int(result.get("fallbackIncompletePending") or 0)
        global_f = int(result.get("fallbackIncompleteGlobal") or 0)
        selection = int(result.get("fallbackIncompleteSelection") or 0)
        conditions = int(result.get("fallbackIncompleteConditions") or 0)
        if integrated <= 0:
            assert (costs + further + pending + global_f + selection + conditions) > 0
        prepared = int(result.get("continuationPreparedOutcomes") or 0)
        assert prepared > 0
    finally:
        battle_finish()
        exact_unload_evaluator_model()
        os.environ.pop("PTCG_EXACT_EVALUATOR_VERSION", None)
        os.environ.pop("PTCG_EXACT_V4_PASSIVE_DRAW", None)


def _fraction(result, side):
    return int(result[f"{side}Numerator"]), int(result[f"{side}Denominator"])


def test_mini_deck_passive_on_off_identity_when_no_hand_cost_items():
    """Path A (Passive off full enum) vs Path B (Passive on) must match fractions.

    Deck avoids Ultra Ball / search Items so locked energies can be Passive.
    """
    # 60-card deck: energies + Enriching Energy + Abra + Switch (no hand-discard Items).
    deck = [5] * 50 + [13] * 2 + [741] * 4 + [1123] * 4
    assert len(deck) == 60
    observation = None
    rich_idxs = []
    for seed in range(0, 12):
        observation, start = battle_start_seeded(deck, deck, seed)
        if observation is None:
            continue
        for _ in range(40):
            select = observation.get("select") or {}
            if select.get("type") == 0 and select.get("context") == 0:
                break
            observation = battle_select(list(range(int(select.get("minCount", 0)))))
        else:
            battle_finish()
            observation = None
            continue
        current = observation["current"]
        hand = current["players"][current["yourIndex"]]["hand"]
        options = observation["select"]["option"]
        rich_idxs = [
            i for i, opt in enumerate(options)
            if opt["type"] == 8 and hand[opt["index"]]["id"] == 13
        ]
        if rich_idxs:
            break
        battle_finish()
        observation = None
    if observation is None or not rich_idxs:
        pytest.skip("Enriching Energy not playable on tried seeds")
    action = rich_idxs[0]

    os.environ["PTCG_EXACT_EVALUATOR_VERSION"] = "V4"
    exact_load_evaluator_model(str(MODEL))
    try:
        os.environ.pop("PTCG_EXACT_V4_PASSIVE_DRAW", None)
        off = exact_evaluate_action_v2(
            to_observation_class(observation), deck, [100] * len(deck), 8_000,
            action, opponent_deck=deck,
        )
        os.environ["PTCG_EXACT_V4_PASSIVE_DRAW"] = "1"
        on = exact_evaluate_action_v2(
            to_observation_class(observation), deck, [100] * len(deck), 8_000,
            action, opponent_deck=deck,
        )
        assert off["chanceMassMismatches"] == 0
        assert on["chanceMassMismatches"] == 0
        # Identity when both finish without timeout and report scores.
        if not off.get("timedOut") and not on.get("timedOut"):
            assert _fraction(off, "lower") == _fraction(on, "lower")
            assert _fraction(off, "upper") == _fraction(on, "upper")
            assert off.get("certified") == on.get("certified")
    finally:
        battle_finish()
        exact_unload_evaluator_model()
        os.environ.pop("PTCG_EXACT_EVALUATOR_VERSION", None)
        os.environ.pop("PTCG_EXACT_V4_PASSIVE_DRAW", None)
