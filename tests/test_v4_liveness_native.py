"""V4 Phase 1 E native diagnostics (requires rebuilt cg.dll)."""
from __future__ import annotations

import json
from fractions import Fraction
from math import comb

import pytest

from cg import sim


@pytest.fixture(scope="module")
def lib():
    return sim.lib


def test_liveness_diagnostics_energy_passive(lib):
    if not hasattr(lib, "ExactCardLivenessV4Diagnostics"):
        pytest.skip("ExactCardLivenessV4Diagnostics not exported")
    raw = lib.ExactCardLivenessV4Diagnostics()
    data = json.loads(raw.decode("utf-8") if isinstance(raw, bytes) else raw)
    assert data["livenessSchemaVersion"] == 4
    # Empty operator closure can still prove energy-once Passive; Active sample may
    # be zero if the chosen Item is turn-locked. At least one classification runs.
    assert data["samplePassive"] + data["sampleActive"] + data["sampleUnknown"] >= 1


def test_passive_expectation_oracle_matches_python(lib):
    if not hasattr(lib, "ExactPassiveExpectationV4Oracle"):
        pytest.skip("ExactPassiveExpectationV4Oracle not exported")

    def cpp_ratio(pool, take, ni, nj, mode):
        raw = lib.ExactPassiveExpectationV4Oracle(pool, take, ni, nj, mode)
        data = json.loads(raw.decode("utf-8") if isinstance(raw, bytes) else raw)
        assert "error" not in data
        return Fraction(int(data["numerator"]), int(data["denominator"]))

    pool, take, ni, nj = 10, 4, 3, 2
    assert cpp_ratio(pool, take, ni, 0, 0) == Fraction(take * ni, pool)
    assert cpp_ratio(pool, take, ni, nj, 1) == Fraction(
        take * (take - 1) * ni * nj, pool * (pool - 1)
    )
    assert cpp_ratio(pool, take, ni, 0, 2) == Fraction(
        comb(take, 2) * comb(ni, 2), comb(pool, 2)
    )
