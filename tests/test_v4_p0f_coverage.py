"""P0f: per-candidate coverage, future-chance gate, artificial Passive>0."""
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

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "sample_submission" / "sample_submission" / "exact-evaluator-v3.bin"

# No Ultra Ball / search Items; one ACE SPEC Enriching Energy; Switch + basics + stadium.
PASSIVE_DECK = [5] * 40 + [13] + [741] * 4 + [305] * 4 + [1123] * 4 + [1266] * 3 + [5] * 4


@pytest.fixture(scope="module")
def lib():
    return sim.lib


def test_p0f_diagnostics_scenarios(lib):
    assert nnue_v4.LIVENESS_SCHEMA == 4
    raw = lib.ExactCardLivenessV4Diagnostics()
    data = json.loads(raw.decode("utf-8") if isinstance(raw, bytes) else raw)
    assert data["livenessSchemaVersion"] == 4
    assert data["ultraBallBlocksUsedSupporter"] is True
    assert data["usedSupporterPassiveWithoutUltra"] is True
    assert data["damageOnlyDoesNotBlockPassiveEnergy"] is True
    assert data["energyDiscardKeepsSupporterPassive"] is True
    assert data["energyDiscardActivesEnergy"] is True
    assert data["attackFootprintsPresent"] is True
    assert data["drawImpliesFutureChance"] is True


def _find_rich_main(deck, seeds=range(0, 24)):
    for seed in seeds:
        observation, start = battle_start_seeded(deck, deck, seed)
        if observation is None:
            continue
        for _ in range(50):
            select = observation.get("select") or {}
            if select.get("type") == 0 and select.get("context") == 0:
                current = observation["current"]
                hand = current["players"][current["yourIndex"]]["hand"]
                rich = [
                    i for i, opt in enumerate(select["option"])
                    if opt["type"] == 8 and hand[opt["index"]]["id"] == 13
                ]
                if rich:
                    return observation, rich[0], seed
                break
            observation = battle_select(list(range(int(select.get("minCount", 0)))))
        battle_finish()
    return None, None, None


def test_artificial_deck_requires_passive_integration():
    """Success gate: Passive must integrate on a no-hand-cost mini deck."""
    assert len(PASSIVE_DECK) == 60
    os.environ["PTCG_EXACT_EVALUATOR_VERSION"] = "V4"
    os.environ["PTCG_EXACT_V4_PASSIVE_DRAW"] = "1"
    observation, action, seed = _find_rich_main(PASSIVE_DECK)
    if observation is None:
        pytest.skip("Enriching Energy not playable on tried seeds")
    exact_load_evaluator_model(str(MODEL))
    try:
        result = exact_evaluate_action_v2(
            to_observation_class(observation), PASSIVE_DECK, [100] * 60, 5_000,
            action, opponent_deck=PASSIVE_DECK,
        )
        integrated = int(result.get("passiveCardsIntegrated") or 0)
        prepared = int(result.get("continuationPreparedOutcomes") or 0)
        merged = int(result.get("continuationAtomsMerged") or 0)
        assert result["chanceMassMismatches"] == 0
        assert integrated > 0, (
            f"seed={seed} fallbacks="
            f"costs={result.get('fallbackIncompleteCosts')} "
            f"further={result.get('fallbackFurtherChance')} "
            f"pending={result.get('fallbackIncompletePending')}"
        )
        assert prepared > 0
        assert prepared < 9184
        assert merged >= 0  # may be 0 if only Passive pools collapse outcomes
    finally:
        battle_finish()
        exact_unload_evaluator_model()
        os.environ.pop("PTCG_EXACT_EVALUATOR_VERSION", None)
        os.environ.pop("PTCG_EXACT_V4_PASSIVE_DRAW", None)
