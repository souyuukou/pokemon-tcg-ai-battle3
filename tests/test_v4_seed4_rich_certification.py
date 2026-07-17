"""V4 Rich Energy progress gate + full certification target.

Full ≤90s certification for Item-heavy decks still needs further Main-DAG
speedups; this gate locks the Passive-integral compression invariants.
"""
from __future__ import annotations

import os
import time
from pathlib import Path

import pytest

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


def test_seed4_v4_passive_draw_compresses_and_keeps_exact_mass():
    os.environ["PTCG_EXACT_EVALUATOR_VERSION"] = "V4"
    os.environ["PTCG_EXACT_V4_PASSIVE_DRAW"] = "1"
    deck, observation, rich = _seed4_rich_energy_main()
    model = ROOT / "sample_submission" / "sample_submission" / "exact-evaluator-v3.bin"
    exact_load_evaluator_model(str(model))
    try:
        result = exact_evaluate_action_v2(
            to_observation_class(observation), deck, [100] * len(deck), 3_000,
            rich, opponent_deck=deck,
        )
        assert result["chanceMassMismatches"] == 0
        assert result["probabilityExact"] is True
        assert result.get("hiddenInformationLeakDetected") is False
        prepared = int(result.get("continuationPreparedOutcomes") or 0)
        assert prepared > 0
        assert prepared < 9184  # legacy full identity draw
        assert int(result.get("passiveCardsIntegrated") or 0) > 0
        assert int(result.get("continuationDrawClasses") or 0) >= 1
    finally:
        battle_finish()
        exact_unload_evaluator_model()
        os.environ.pop("PTCG_EXACT_EVALUATOR_VERSION", None)
        os.environ.pop("PTCG_EXACT_V4_PASSIVE_DRAW", None)


@pytest.mark.skip(reason="Item-heavy Active DAG still ~600s at current speed; track as Phase-6 perf target")
def test_seed4_rich_energy_v4_passive_draw_certifies_in_90s():
    os.environ["PTCG_EXACT_EVALUATOR_VERSION"] = "V4"
    os.environ["PTCG_EXACT_V4_PASSIVE_DRAW"] = "1"
    deck, observation, rich = _seed4_rich_energy_main()
    model = ROOT / "sample_submission" / "sample_submission" / "exact-evaluator-v3.bin"
    exact_load_evaluator_model(str(model))
    try:
        started = time.perf_counter()
        result = exact_evaluate_action_v2(
            to_observation_class(observation), deck, [100] * len(deck), 90_000,
            rich, opponent_deck=deck,
        )
        elapsed = time.perf_counter() - started
        assert result["timedOut"] is False
        assert result["certified"] is True
        assert elapsed <= 90
    finally:
        battle_finish()
        exact_unload_evaluator_model()
        os.environ.pop("PTCG_EXACT_EVALUATOR_VERSION", None)
        os.environ.pop("PTCG_EXACT_V4_PASSIVE_DRAW", None)
