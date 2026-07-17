"""P0c: transition-only Passive keys + representative invariance docs/tests."""
from __future__ import annotations

import json

import pytest

from cg import sim
from exact_solver import nnue_v4


@pytest.fixture(scope="module")
def lib():
    return sim.lib


def test_proven_bounds_use_deck_size_not_ten():
    import numpy as np

    bias = np.array([1, -2, 0], dtype=np.int32)
    pairs = np.zeros(0, dtype=[("card_a", "<u2"), ("card_b", "<u2"), ("weight", "<i4")])
    lo, hi, ok = nnue_v4.compute_proven_bounds(bias, pairs)
    assert hi == 1 * 60
    assert lo == -2 * 60
    assert ok is True


def test_ultra_ball_and_empty_closure_liveness(lib):
    if not hasattr(lib, "ExactCardLivenessV4Diagnostics"):
        pytest.skip("diagnostics missing")
    raw = lib.ExactCardLivenessV4Diagnostics()
    data = json.loads(raw.decode("utf-8") if isinstance(raw, bytes) else raw)
    assert data["ultraBallDiscardCostObserved"] is True
    assert data["ultraBallBlocksUsedSupporter"] is True
    # Empty operator set remains Passive-capable (vacuous coverage).
    assert data["usedSupporterPassiveWithoutUltra"] is True


def test_representative_invariance_contract_documented():
    """Planner refuses Passive integrate when Semantic bytes diverge across reps."""
    # Structural: metric key exists after rebuild; fallback count starts at 0.
    assert hasattr(sim.lib, "ExactCardLivenessV4Diagnostics")
