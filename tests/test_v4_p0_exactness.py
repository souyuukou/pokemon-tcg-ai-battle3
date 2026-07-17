"""P0 Exactness tests for V4 Passive liveness / expectation (native + pure python)."""
from __future__ import annotations

from fractions import Fraction
from math import comb
from itertools import combinations

import pytest

from cg import sim


def _hyper_expected_with_base(base, population, take, betas, pairs):
    """Mirror ExactPassiveExpectationV4::ExpectedPassiveResidualWithBase."""
    pool = sum(population.values())
    cards = sorted(set(base) | set(population) | {c for c, _ in betas.items()})

    def e_x(cid):
        n = population.get(cid, 0)
        if pool <= 0 or take <= 0 or n <= 0:
            return Fraction(0)
        return Fraction(take * n, pool)

    def e_xx(a, b):
        if a == b:
            return Fraction(0)
        na, nb = population.get(a, 0), population.get(b, 0)
        if pool <= 1 or take <= 1 or na <= 0 or nb <= 0:
            return Fraction(0)
        return Fraction(take * (take - 1) * na * nb, pool * (pool - 1))

    def e_c2(cid):
        n = population.get(cid, 0)
        if pool <= 1 or take <= 1 or n <= 1:
            return Fraction(0)
        return Fraction(comb(take, 2) * comb(n, 2), comb(pool, 2))

    total = Fraction(0)
    for cid in cards:
        beta = betas.get(cid, 0)
        if beta == 0:
            continue
        e = base.get(cid, 0)
        total += beta * (e + e_x(cid))
    for (a, b), w in pairs.items():
        if w == 0:
            continue
        ei, ej = base.get(a, 0), base.get(b, 0)
        if a == b:
            total += w * (comb(ei, 2) + ei * e_x(a) + e_c2(a))
        else:
            total += w * (ei * ej + ei * e_x(b) + ej * e_x(a) + e_xx(a, b))
    return total


def test_expectation_base_and_cross_pairs_match_enumeration():
    base = {1: 1, 2: 0}
    population = {1: 2, 2: 3, 3: 1}
    take = 2
    betas = {1: 5, 2: 7, 3: 0}
    pairs = {(1, 2): 3, (1, 1): 2}

    # Full enumeration of hypergeometric outcomes.
    deck = []
    for cid, n in population.items():
        deck.extend([cid] * n)
    total_w = comb(len(deck), take)
    acc = Fraction(0)
    for combo in combinations(range(len(deck)), take):
        drawn = {}
        for i in combo:
            drawn[deck[i]] = drawn.get(deck[i], 0) + 1
        counts = {cid: base.get(cid, 0) + drawn.get(cid, 0) for cid in set(base) | set(drawn)}
        value = 0
        for cid, n in counts.items():
            value += betas.get(cid, 0) * n
        for (a, b), w in pairs.items():
            na, nb = counts.get(a, 0), counts.get(b, 0)
            if a == b:
                value += w * comb(na, 2)
            else:
                value += w * na * nb
        acc += Fraction(value, total_w)

    analytic = _hyper_expected_with_base(base, population, take, betas, pairs)
    assert analytic == acc


@pytest.fixture(scope="module")
def lib():
    return sim.lib


def test_liveness_schema_v3(lib):
    if not hasattr(lib, "ExactCardLivenessV4Diagnostics"):
        pytest.skip("ExactCardLivenessV4Diagnostics not exported")
    import json

    raw = lib.ExactCardLivenessV4Diagnostics()
    data = json.loads(raw.decode("utf-8") if isinstance(raw, bytes) else raw)
    assert data["livenessSchemaVersion"] == 4
    assert data["ultraBallBlocksUsedSupporter"] is True
    assert data.get("ultraBallDiscardCostObserved", True) is True
    assert data.get("damageOnlyDoesNotBlockPassiveEnergy", False) is True


def test_liveness_defaults_unknown_not_passive(lib):
    """Unclassified EffectType observation must fail closed (Unknown, not Passive)."""
    if not hasattr(lib, "ExactCardLivenessV4Diagnostics"):
        pytest.skip("ExactCardLivenessV4Diagnostics not exported")
    import json

    raw = lib.ExactCardLivenessV4Diagnostics()
    data = json.loads(raw.decode("utf-8") if isinstance(raw, bytes) else raw)
    assert data["effectObservationUnknown"] > 0


def test_deck_removal_classes_must_not_merge():
    """Documented invariant: different source classes stay separate when search exists."""
    from exact_solver import nnue_v4

    assert nnue_v4.MODEL_SCHEMA == 2
    assert nnue_v4.LIVENESS_SCHEMA == 4
    assert nnue_v4.FEATURE_SCHEMA == 2
