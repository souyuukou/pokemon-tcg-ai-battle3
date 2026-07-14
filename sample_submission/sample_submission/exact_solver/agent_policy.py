"""Observation-safe action quotient and deterministic emergency policy."""

from __future__ import annotations
from itertools import combinations
import os
import time
from dataclasses import dataclass, field
from .canonical import canonical_bytes
from .profile import load_profile
from .resources import MatchBudget


_budget = MatchBudget()
_last_turn = None
last_decision = None
_policy_cache = {}
_evaluator_loaded = False


def _ensure_v3_evaluator() -> None:
    global _evaluator_loaded
    if _evaluator_loaded:
        return
    from pathlib import Path
    from cg.api import exact_load_evaluator_model
    candidates = [Path(__file__).resolve().parents[1] / "exact-evaluator-v3.bin",
                  Path("/kaggle_simulations/agent/exact-evaluator-v3.bin")]
    path = next((candidate for candidate in candidates if candidate.exists()), None)
    if path is None:
        raise RuntimeError("V3 evaluator model is missing")
    info = exact_load_evaluator_model(str(path))
    if int(info.get("schemaVersion", 0)) != 3 or not info.get("informationSetSafe"):
        raise RuntimeError("V3 evaluator model was not accepted")
    _evaluator_loaded = True


@dataclass
class PolicyContext:
    """Per-player match budget and native turn-policy ownership."""
    budget: MatchBudget = field(default_factory=MatchBudget)
    last_turn: int | None = None
    session_id: int | None = None
    last_decision: dict | None = None
    budget_turn: int | None = None
    turn_search_seconds: float = 0.0

    def reset(self) -> None:
        if self.session_id is not None:
            try:
                from cg.api import exact_turn_release
                exact_turn_release(self.session_id)
            except (RuntimeError, OSError):
                pass
        self.budget.reset()
        self.last_turn = None
        self.session_id = None
        self.last_decision = None
        self.budget_turn = None
        self.turn_search_seconds = 0.0


_default_context = PolicyContext(_budget)


def option_semantic_key(option):
    data = vars(option).copy()
    data.pop("serial", None)
    return canonical_bytes(data)


def _fallback_score(option) -> int:
    """Small deterministic evaluator used only when no proven native action exists."""
    # OptionType numeric values are part of the public engine API.  Values favor
    # irreversible progress while keeping End as the safe baseline.
    return {
        13: 10_000,  # Attack
        9: 4_000,    # Evolve
        8: 3_000,    # Attach
        10: 2_500,   # Ability
        7: 1_500,    # Play
        12: 500,     # Retreat
        14: 0,       # End
    }.get(int(option.type), 0)


def _turn_slice_milliseconds(ctx: PolicyContext, is_new_turn: bool, usable_ms: int) -> int:
    """Return this call's slice while enforcing one absolute per-turn cap."""
    turn_cap = int(os.environ.get("PTCG_EXACT_TURN_MS", "90000"))
    selection_cap = int(os.environ.get("PTCG_EXACT_SELECTION_MS", "10000"))
    remaining_turn_ms = turn_cap - int(ctx.turn_search_seconds * 1000)
    if remaining_turn_ms <= 0:
        raise RuntimeError("exact turn search budget reached")
    return max(1, min(remaining_turn_ms, turn_cap if is_new_turn else selection_cap, usable_ms))


def choose_action(obs, *, context: PolicyContext | None = None,
                  opponent_deck: list[int] | None = None) -> tuple[list[int], bool, str]:
    """Return action, certification flag, reason.

    Full turn search is only certified when a transition provider can resolve all
    hidden chance variables. The bundled API demands guessed opponent cards, so
    this policy never calls it with fabricated identities.
    """
    ctx = context or _default_context
    observed_turn = obs.current.turn if obs.current is not None else None
    if observed_turn != ctx.budget_turn:
        ctx.budget_turn = observed_turn
        ctx.turn_search_seconds = 0.0
    call_started = time.monotonic()
    def finish(action, certified, reason):
        elapsed = time.monotonic() - call_started
        ctx.budget.charge(elapsed)
        ctx.turn_search_seconds += elapsed
        return action, certified, reason

    select = obs.select
    if select is None: raise ValueError("deck request is not an action")
    if select.minCount == select.maxCount == 0: return finish([], True, "forced-empty")
    option_count = len(select.option)
    if select.minCount > option_count: raise ValueError("invalid observation")
    global _last_turn, last_decision
    try:
        if obs.current is None or obs.current.turn <= 0:
            raise RuntimeError("exact turn search starts after setup")
        if not ctx.budget.can_expand():
            raise RuntimeError("exact search resource reserve reached")
        from cg.api import exact_decide, exact_turn_begin, exact_turn_advance, exact_turn_release
        _ensure_v3_evaluator()
        profile = load_profile()
        values = profile.evaluator.get("hand_values", {})
        hand_values = [int(values.get(str(card_id), values.get("default", 100))) for card_id in profile.cards]
        is_new_turn = obs.current is not None and obs.current.turn != ctx.last_turn
        usable_ms = max(1, int((ctx.budget.remaining - ctx.budget.limits.reserve_seconds) * 1000))
        requested_ms = _turn_slice_milliseconds(ctx, is_new_turn, usable_ms)
        if is_new_turn:
            if ctx.session_id is not None:
                exact_turn_release(ctx.session_id)
                ctx.session_id = None
            try:
                native = exact_turn_begin(obs, list(profile.cards), hand_values,
                                          requested_ms, opponent_deck=opponent_deck)
                ctx.session_id = int(native["sessionId"])
                reason = "native-exact-turn-begin"
            except RuntimeError:
                native = exact_decide(obs, list(profile.cards), hand_values, requested_ms)
                reason = "native-exact-turn-search"
        elif ctx.session_id is not None:
            native = exact_turn_advance(ctx.session_id, obs, requested_ms)
            reason = "exact-policy-reroot" if native.get("policyHits", 0) else "exact-policy-resume"
        else:
            native = exact_decide(obs, list(profile.cards), hand_values, requested_ms)
            reason = "native-exact-turn-search"
        action = [int(index) for index in native["selected"]]
        if select.minCount <= len(action) <= select.maxCount and len(set(action)) == len(action) \
                and all(0 <= index < option_count for index in action):
            ctx.last_turn = obs.current.turn if obs.current is not None else ctx.last_turn
            ctx.last_decision = native
            if context is None:
                _last_turn = ctx.last_turn
                last_decision = native
            return finish(action, bool(native["certified"]), reason)
    except (RuntimeError, ValueError, OSError, KeyError):
        pass
    count = select.maxCount
    actions = combinations(range(option_count), count)
    # Stable semantic tie-break, independent of physical option order where possible.
    best = min(actions, key=lambda action: (
        -sum(_fallback_score(select.option[i]) for i in action),
        tuple(option_semantic_key(select.option[i]) for i in action),
    ))
    return finish(list(best), False, "emergency-policy: native exact chance provider unavailable")

