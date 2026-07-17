"""Phase 3: Passive hypergeometric expectation oracles (ExactFraction).

Compares analytic E[X_i], E[X_i X_j], E[C(X_i,2)] against full enumeration.
Does not require the native DLL.
"""
from __future__ import annotations

from fractions import Fraction
from itertools import combinations
from math import comb


def expected_count(pool: int, take: int, copies: int) -> Fraction:
    if pool <= 0 or take <= 0 or copies <= 0:
        return Fraction(0)
    return Fraction(take * copies, pool)


def expected_product_distinct(pool: int, take: int, ni: int, nj: int) -> Fraction:
    if pool <= 1 or take <= 1 or ni <= 0 or nj <= 0:
        return Fraction(0)
    return Fraction(take * (take - 1) * ni * nj, pool * (pool - 1))


def expected_choose2(pool: int, take: int, ni: int) -> Fraction:
    if pool <= 1 or take <= 1 or ni <= 1:
        return Fraction(0)
    return Fraction(comb(take, 2) * comb(ni, 2), comb(pool, 2))


def enumerate_expected_count(pool_cards: list[int], take: int, card_id: int) -> Fraction:
    n = len(pool_cards)
    total = comb(n, take)
    if total == 0:
        return Fraction(0)
    s = 0
    for drawn in combinations(range(n), take):
        s += sum(1 for i in drawn if pool_cards[i] == card_id)
    return Fraction(s, total)


def enumerate_expected_product(pool_cards: list[int], take: int, a: int, b: int) -> Fraction:
    n = len(pool_cards)
    total = comb(n, take)
    if total == 0:
        return Fraction(0)
    s = 0
    for drawn in combinations(range(n), take):
        counts = {}
        for i in drawn:
            counts[pool_cards[i]] = counts.get(pool_cards[i], 0) + 1
        s += counts.get(a, 0) * counts.get(b, 0)
    return Fraction(s, total)


def enumerate_expected_choose2(pool_cards: list[int], take: int, card_id: int) -> Fraction:
    n = len(pool_cards)
    total = comb(n, take)
    if total == 0:
        return Fraction(0)
    s = 0
    for drawn in combinations(range(n), take):
        c = sum(1 for i in drawn if pool_cards[i] == card_id)
        s += comb(c, 2)
    return Fraction(s, total)


def test_expected_count_matches_enumeration():
    # Pool: three identities with copies 3,2,1
    pool = [1, 1, 1, 2, 2, 3]
    for take in range(0, 5):
        for card, copies in ((1, 3), (2, 2), (3, 1)):
            analytic = expected_count(len(pool), take, copies)
            enum = enumerate_expected_count(pool, take, card)
            assert analytic == enum, (take, card, analytic, enum)


def test_expected_product_distinct_matches_enumeration():
    pool = [1, 1, 1, 2, 2, 3]
    for take in range(0, 5):
        analytic = expected_product_distinct(len(pool), take, 3, 2)
        enum = enumerate_expected_product(pool, take, 1, 2)
        assert analytic == enum, (take, analytic, enum)


def test_expected_choose2_matches_enumeration():
    pool = [1, 1, 1, 2, 2, 3]
    for take in range(0, 5):
        analytic = expected_choose2(len(pool), take, 3)
        enum = enumerate_expected_choose2(pool, take, 1)
        assert analytic == enum, (take, analytic, enum)


def test_mixed_active_passive_takes_mass():
    active, passive, take = 5, 7, 4
    terms = []
    for x in range(take + 1):
        y = take - x
        if x > active or y > passive:
            continue
        w = comb(active, x) * comb(passive, y)
        if w:
            terms.append((x, y, w))
    assert sum(w for _, _, w in terms) == comb(active + passive, take)


def test_passive_residual_linearity_expectation():
    # E[sum X_i r_i] = sum E[X_i] r_i with fixed context values.
    pool = [10, 10, 11, 12, 12, 12]
    take = 3
    values = {10: 5, 11: -2, 12: 7}
    copies = {10: 2, 11: 1, 12: 3}
    analytic = sum(
        expected_count(len(pool), take, copies[cid]) * values[cid] for cid in values
    )
    total = comb(len(pool), take)
    s = 0
    for drawn in combinations(range(len(pool)), take):
        counts = {}
        for i in drawn:
            counts[pool[i]] = counts.get(pool[i], 0) + 1
        s += sum(counts.get(cid, 0) * values[cid] for cid in values)
    assert Fraction(s, total) == analytic
