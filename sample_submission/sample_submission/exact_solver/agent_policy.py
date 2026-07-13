"""Observation-safe action quotient and deterministic emergency policy."""

from __future__ import annotations
from itertools import combinations
import os
from .canonical import canonical_bytes, observation_key
from .profile import load_profile
from .resources import MatchBudget


_budget = MatchBudget()
_last_turn = None
last_decision = None
_policy_cache = {}


def option_semantic_key(option):
    data = vars(option).copy()
    data.pop("serial", None)
    return canonical_bytes(data)


def choose_action(obs) -> tuple[list[int], bool, str]:
    """Return action, certification flag, reason.

    Full turn search is only certified when a transition provider can resolve all
    hidden chance variables. The bundled API demands guessed opponent cards, so
    this policy never calls it with fabricated identities.
    """
    select = obs.select
    if select is None: raise ValueError("deck request is not an action")
    if select.minCount == select.maxCount == 0: return [], True, "forced-empty"
    option_count = len(select.option)
    if select.minCount > option_count: raise ValueError("invalid observation")
    global _last_turn, last_decision
    try:
        if obs.current is None or obs.current.turn <= 0:
            raise RuntimeError("exact turn search starts after setup")
        if not _budget.can_expand():
            raise RuntimeError("exact search resource reserve reached")
        from cg.api import exact_decide
        profile = load_profile()
        key = observation_key(obs, {
            "engine": 1, "canonical": 1, "deck": profile.sha256,
            "evaluator": canonical_bytes(profile.evaluator).hex(), "leaf": "checkup-end-v1",
        })
        cached = _policy_cache.get(key)
        if cached is not None:
            action, certified = cached
            if select.minCount <= len(action) <= select.maxCount and all(0 <= i < option_count for i in action):
                return list(action), certified, "exact-policy-cache"
        values = profile.evaluator.get("hand_values", {})
        hand_values = [int(values.get(str(card_id), values.get("default", 100))) for card_id in profile.cards]
        is_new_turn = obs.current is not None and obs.current.turn != _last_turn
        usable_ms = max(1, int((_budget.remaining - _budget.limits.reserve_seconds) * 1000))
        turn_cap = int(os.environ.get("PTCG_EXACT_TURN_MS", "180000"))
        selection_cap = int(os.environ.get("PTCG_EXACT_SELECTION_MS", "10000"))
        requested_ms = min(turn_cap if is_new_turn else selection_cap, usable_ms)
        native = exact_decide(obs, list(profile.cards), hand_values, requested_ms)
        action = [int(index) for index in native["selected"]]
        if select.minCount <= len(action) <= select.maxCount and len(set(action)) == len(action) \
                and all(0 <= index < option_count for index in action):
            _last_turn = obs.current.turn if obs.current is not None else _last_turn
            last_decision = native
            _policy_cache[key] = (tuple(action), bool(native["certified"]))
            return action, bool(native["certified"]), "native-exact-turn-search"
    except (RuntimeError, ValueError, OSError, KeyError):
        pass
    count = select.maxCount
    actions = combinations(range(option_count), count)
    # Stable semantic tie-break, independent of physical option order where possible.
    best = min(actions, key=lambda action: tuple(option_semantic_key(select.option[i]) for i in action))
    return list(best), False, "emergency-policy: native exact chance provider unavailable"

