from pathlib import Path

from cg.api import (exact_decide_v2, exact_load_evaluator_model,
                    exact_unload_evaluator_model, to_observation_class)
from cg.game import battle_finish

from exact_solver.profile import load_profile
from tests.test_first_turn_exact_performance import _first_main


ROOT = Path(__file__).resolve().parents[1]


def test_packed_runtime_metrics_report_pool_reuse_without_changing_exact_result():
    deck = list(load_profile().cards)
    observation = _first_main(deck, 6)
    model = ROOT / "sample_submission" / "sample_submission" / "exact-evaluator-v3.bin"
    exact_load_evaluator_model(str(model))
    try:
        result = exact_decide_v2(
            to_observation_class(observation), deck, [100] * len(deck), 30_000,
            opponent_deck=deck,
        )
        assert result["certified"] is True
        assert result["probabilityExact"] is True
        assert result["chanceMassMismatches"] == 0
        assert result["stateCopies"] > 0
        assert result["statePoolReuses"] > 0
        assert result["heapAllocations"] < result["stateCopies"]
        assert result["workerBusyNs"] > 0
        assert result["legacyShadowMismatches"] == 0
    finally:
        battle_finish()
        exact_unload_evaluator_model()


def test_legacy_cow_and_shadow_are_exactly_equivalent(monkeypatch):
    deck = list(load_profile().cards)
    observation = _first_main(deck, 6)
    model = ROOT / "sample_submission" / "sample_submission" / "exact-evaluator-v3.bin"
    exact_load_evaluator_model(str(model))
    try:
        results = {}
        for mode in ("legacy", "cow", "shadow"):
            monkeypatch.setenv("PTCG_EXACT_RUNTIME", mode)
            results[mode] = exact_decide_v2(
                to_observation_class(observation), deck, [100] * len(deck), 30_000,
                opponent_deck=deck,
            )
        identity = lambda result: (
            result["selected"], result["lowerNumerator"], result["lowerDenominator"],
            result["upperNumerator"], result["upperDenominator"], result["certified"],
            result["chanceMassMismatches"], result["opaqueNodes"],
        )
        assert identity(results["legacy"]) == identity(results["cow"]) == identity(results["shadow"])
        assert results["cow"]["runtimeVersion"] == 3
        assert results["cow"]["packedObservationBuilds"] > 0
        # Packed production deliberately uses the pooled bulk restore: on the
        # measured State layout it is faster than page-by-page memcmp. Shadow
        # retains the conservative page-COW audit.
        assert results["cow"]["cowFullCopies"] > 0
        assert results["cow"]["cowCopyBytes"] == results["cow"]["stateCopyBytes"]
        assert results["cow"]["evaluatorCacheHits"] > 0
        assert results["cow"]["mutationMisses"] == 0
        assert results["shadow"]["legacyShadowMismatches"] == 0
        assert results["shadow"]["mutationMisses"] == 0
        assert results["shadow"]["cowPageCopies"] > 0
    finally:
        battle_finish()
        exact_unload_evaluator_model()
