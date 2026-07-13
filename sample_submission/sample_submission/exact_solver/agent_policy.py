"""Observation-safe action quotient and deterministic emergency policy."""

from __future__ import annotations
from itertools import combinations
from .canonical import canonical_bytes


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
    count = select.maxCount
    actions = combinations(range(option_count), count)
    # Stable semantic tie-break, independent of physical option order where possible.
    best = min(actions, key=lambda action: tuple(option_semantic_key(select.option[i]) for i in action))
    return list(best), False, "emergency-policy: native exact chance provider unavailable"

