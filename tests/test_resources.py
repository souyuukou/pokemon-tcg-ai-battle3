from fractions import Fraction
from exact_solver.resources import MatchBudget, ResourceLimits, current_rss_bytes, solve_root_jobs
from exact_solver.agent_policy import PolicyContext
from exact_solver.solver import SearchResult


def test_hard_limits_and_root_interval_aggregation():
    limits = ResourceLimits()
    assert limits.workers == 2 and limits.rss_limit_bytes == 2_700_000_000
    result = solve_root_jobs([
        ("safe", lambda: SearchResult(Fraction(4), Fraction(4))),
        ("open", lambda: SearchResult(Fraction(3), Fraction(9), certified=False)),
    ])
    assert result.action == "safe" and result.lower == 4 and result.upper == 9
    assert not result.certified
    assert current_rss_bytes() >= 0
    assert MatchBudget(limits).remaining <= 600


def test_self_play_policy_contexts_have_independent_budgets():
    left = PolicyContext()
    right = PolicyContext()
    assert left.budget is not right.budget
    left.budget.charge(10)
    assert right.budget.remaining - left.budget.remaining > 9

