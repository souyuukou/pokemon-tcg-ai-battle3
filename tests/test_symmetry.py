from math import comb
from exact_solver.symmetry import facedown_prize_actions, quotient_actions, bounded_count_vectors, orbit_allocations


def test_facedown_prizes_collapse_only_when_exchangeable():
    reduced = facedown_prize_actions(6, 2, prizes_exchangeable=True)
    assert len(reduced) == 1 and len(reduced[0].members) == comb(6, 2)
    assert len(facedown_prize_actions(6, 2, prizes_exchangeable=True, known_indices=[0])) == comb(6, 2)
    assert len(facedown_prize_actions(6, 2, prizes_exchangeable=True, lucky_bonus_possible=True)) == comb(6, 2)


def test_equal_successor_actions_form_one_class():
    classes = quotient_actions(((0,), (1,), (2,)), lambda a: "same" if a[0] < 2 else "other")
    assert sorted(len(c.members) for c in classes) == [1, 2]


def test_count_vectors_and_damage_orbits_are_exact():
    assert set(bounded_count_vectors([2, 1], 2, 2)) == {(1, 1), (2, 0)}
    # 3 counters over 2 identical Pokémon: (3,0) and (2,1), not both permutations.
    assert set(orbit_allocations(2, 3, 3)) == {(3, 0), (2, 1)}

