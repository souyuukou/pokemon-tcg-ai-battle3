"""Gate measurement for skeleton OFF/ON (not a pytest file)."""
from __future__ import annotations

import os
import time
from pathlib import Path

from cg.api import (exact_decide_v2, exact_evaluate_action_v2, exact_load_evaluator_model,
                    exact_unload_evaluator_model, to_observation_class)
from cg.game import battle_finish, battle_select, battle_start_seeded
from exact_solver.profile import load_profile

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "sample_submission" / "sample_submission" / "exact-evaluator-v3.bin"


def first_main(deck, seed):
    observation, start = battle_start_seeded(deck, deck, seed)
    assert observation is not None
    for _ in range(50):
        select = observation.get("select") or {}
        if select.get("type") == 0 and select.get("context") == 0:
            return observation
        observation = battle_select(list(range(int(select.get("minCount", 0)))))
    raise AssertionError("no main")


def seed4_rich():
    deck = list(load_profile().cards)
    observation, start = battle_start_seeded(deck, deck, 4)
    assert observation is not None
    for _ in range(50):
        select = observation.get("select") or {}
        if select.get("type") == 0 and select.get("context") == 0:
            current = observation["current"]
            hand = current["players"][current["yourIndex"]]["hand"]
            rich = next(
                i for i, o in enumerate(select["option"])
                if o["type"] == 8 and hand[o["index"]]["id"] == 13
            )
            return deck, observation, rich
        observation = battle_select(list(range(int(select.get("minCount", 0)))))
    raise AssertionError("no rich")


def summarize(label, result, elapsed):
    keys = [
        "certified", "expandedNodes", "continuationDrawOutcomes", "skeletonClasses",
        "skeletonExpansions", "skeletonSweeps", "skeletonGuardFallbacks", "skeletonNodes",
        "chanceMassMismatches", "opaqueNodes", "exceptionNodes", "partialChanceNodes",
    ]
    print(f"\n=== {label} ({elapsed:.3f}s) ===")
    for k in keys:
        print(f"  {k}: {result.get(k)}")
    if result.get("certified"):
        print(f"  value: {result['lowerNumerator']} / {result['lowerDenominator']}")


def main():
    exact_load_evaluator_model(str(MODEL))
    deck = list(load_profile().cards)
    try:
        for flag in ("0", "1"):
            os.environ["PTCG_EXACT_SKELETON"] = flag
            observation = first_main(deck, 6)
            t0 = time.perf_counter()
            result = exact_decide_v2(
                to_observation_class(observation), deck, [100] * len(deck), 90_000,
                opponent_deck=deck,
            )
            summarize(f"seed6 skeleton={flag}", result, time.perf_counter() - t0)
            battle_finish()

        deck4, obs4, rich = seed4_rich()
        for flag in ("0", "1"):
            os.environ["PTCG_EXACT_SKELETON"] = flag
            t0 = time.perf_counter()
            result = exact_evaluate_action_v2(
                to_observation_class(obs4), deck4, [100] * len(deck4), 5_000,
                rich, opponent_deck=deck4,
            )
            summarize(f"seed4-rich-5s skeleton={flag}", result, time.perf_counter() - t0)
            battle_finish()
    finally:
        os.environ.pop("PTCG_EXACT_SKELETON", None)
        exact_unload_evaluator_model()


if __name__ == "__main__":
    main()
