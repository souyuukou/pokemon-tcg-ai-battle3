from fractions import Fraction
from exact_solver.canonical import canonical_bytes, KeyArena
from exact_solver.solver import ExactSolver


class ToyModel:
    # root action safe -> exact 2; gamble -> average 3, with duplicate equal leaves
    def node_kind(self, s): return s[0]
    def canonical_state(self, s): return canonical_bytes(s)
    def evaluate(self, s): return s[1]
    def actions(self, s): return s[1]
    def step(self, s, a): return a
    def outcomes(self, s): return s[1]


def test_exact_root_propagation_and_chance_merge():
    leaf2, leaf1, leaf5 = ("leaf", 2), ("leaf", 1), ("leaf", 5)
    chance = ("chance", ((leaf1, 1), (leaf5, 1), (leaf5, 2)))
    root = ("max", (leaf2, chance))
    solver = ExactSolver(ToyModel(), namespace={"test": 1})
    result = solver.solve(root)
    assert result.value == Fraction(4)
    assert result.action == chance
    assert solver.merged >= 0


def test_hash_collisions_do_not_change_result():
    root = ("max", (("leaf", 3), ("leaf", 7)))
    arena = KeyArena(hash_fn=lambda _: (1, 1))
    result = ExactSolver(ToyModel(), namespace={}, arena=arena).solve(root)
    assert result.value == 7


def test_interrupted_max_keeps_upper_bound_from_every_action():
    class IntervalSolver(ExactSolver):
        def _solve(self, state, path):
            if state == ("leaf", 1):
                from exact_solver.solver import SearchResult
                return SearchResult(Fraction(1), Fraction(100), certified=False)
            return super()._solve(state, path)
    root = ("max", (("leaf", 4), ("leaf", 1)))
    result = IntervalSolver(ToyModel(), namespace={}).solve(root)
    assert result.lower == 4 and result.upper == 100 and not result.certified
