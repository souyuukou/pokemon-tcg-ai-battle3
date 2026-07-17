"""P0d: representative invariance serialization + no fake coverage seal."""
from __future__ import annotations

import json

import pytest

from cg import sim
from exact_solver import nnue_v4


@pytest.fixture(scope="module")
def lib():
    return sim.lib


def test_feature_schema_version_is_v2():
    # ExactFeatureV4::SchemaVersion — must stay in sync with SerializeSemanticFeatures.
    assert nnue_v4.FEATURE_SCHEMA == 2


def test_ultra_ball_still_blocks_used_supporter(lib):
    if not hasattr(lib, "ExactCardLivenessV4Diagnostics"):
        pytest.skip("diagnostics missing")
    raw = lib.ExactCardLivenessV4Diagnostics()
    data = json.loads(raw.decode("utf-8") if isinstance(raw, bytes) else raw)
    assert data["ultraBallDiscardCostObserved"] is True
    assert data["ultraBallBlocksUsedSupporter"] is True
    # Empty reachable remains Passive-capable (vacuous coverage only).
    assert data["usedSupporterPassiveWithoutUltra"] is True


def test_proven_bounds_use_deck_size_cap():
    import numpy as np

    bias = np.array([3], dtype=np.int32)
    pairs = np.zeros(0, dtype=[("card_a", "<u2"), ("card_b", "<u2"), ("weight", "<i4")])
    lo, hi, ok = nnue_v4.compute_proven_bounds(bias, pairs)
    assert hi == 3 * 60
    assert ok is True
