from fractions import Fraction
from exact_solver.chance import exact_draw_outcomes, normalize_integer_weights


def test_draw_three_is_exact_and_compressed():
    outcomes = list(exact_draw_outcomes(["a"] * 4 + ["b"] * 2, 3))
    assert sum(o.weight for o in outcomes) == 20
    assert {o.value: o.weight for o in outcomes} == {
        (("a", 1), ("b", 2)): 4,
        (("a", 2), ("b", 1)): 12,
        (("a", 3),): 4,
    }
    assert sum((o.probability for o in outcomes), Fraction()) == 1


def test_belief_weights_normalize_by_gcd():
    assert normalize_integer_weights((("x", 6), ("y", 9))) == (("x", 2), ("y", 3))
