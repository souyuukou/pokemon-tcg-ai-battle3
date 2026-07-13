"""Proven-safe action and outcome symmetry reductions."""

from __future__ import annotations
from collections import defaultdict
from dataclasses import dataclass
from itertools import combinations
from typing import Callable, Hashable, Iterable, Sequence


@dataclass(frozen=True, slots=True)
class ActionClass:
    representative: tuple[int, ...]
    members: tuple[tuple[int, ...], ...]


def selections(option_count: int, minimum: int, maximum: int):
    for count in range(minimum, maximum + 1):
        yield from combinations(range(option_count), count)


def quotient_actions(actions: Iterable[tuple[int, ...]], semantic_key: Callable[[tuple[int, ...]], Hashable]) -> list[ActionClass]:
    groups: dict[Hashable, list[tuple[int, ...]]] = defaultdict(list)
    for action in actions: groups[semantic_key(action)].append(action)
    return [ActionClass(min(v), tuple(sorted(v))) for v in groups.values()]


def facedown_prize_actions(option_count: int, take: int, *, prizes_exchangeable: bool,
                           known_indices: Sequence[int] = (), lucky_bonus_possible: bool = False):
    """Collapse prize indices only under the audited face-down/exchangeable rule."""
    all_actions = list(combinations(range(option_count), take))
    if not prizes_exchangeable or known_indices or lucky_bonus_possible or not all_actions:
        return [ActionClass(a, (a,)) for a in all_actions]
    return [ActionClass(all_actions[0], tuple(all_actions))]


SAFE_CONTEXTS = frozenset({"TO_HAND", "DISCARD_ENERGY", "DAMAGE_COUNTER", "EVOLVE", "BENCH"})


def context_is_audited(context: object) -> bool:
    name = getattr(context, "name", str(context)).upper()
    return name in SAFE_CONTEXTS

