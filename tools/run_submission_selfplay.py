from __future__ import annotations

import argparse
from collections import Counter
import json
import os
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "sample_submission" / "sample_submission"))

from cg.game import battle_finish, battle_select, battle_start_seeded
from cg.api import to_observation_class
from exact_solver.agent_policy import PolicyContext, choose_action
from exact_solver.profile import load_profile
from exact_solver.resources import current_rss_bytes


def _setup_action(observation: dict, deck: list[int]) -> list[int]:
    select = observation.get("select")
    if select is None:
        return deck
    minimum = int(select["minCount"])
    if minimum > len(select.get("option") or []):
        raise ValueError("illegal setup selection")
    return list(range(minimum))


def _validate(action: list[int], observation: dict) -> None:
    select = observation.get("select")
    if select is None:
        if len(action) != 60:
            raise ValueError("deck request did not return 60 cards")
        return
    count = len(select.get("option") or [])
    if not int(select["minCount"]) <= len(action) <= int(select["maxCount"]):
        raise ValueError("action cardinality is illegal")
    if len(set(action)) != len(action) or any(index < 0 or index >= count for index in action):
        raise ValueError("action contains an illegal option index")


def play(seed: int, deck: list[int], max_actions: int) -> dict:
    contexts = [PolicyContext(), PolicyContext()]
    observation, start = battle_start_seeded(deck, deck, seed)
    if observation is None or start.errorType:
        raise RuntimeError(f"start failed: {start.errorPlayer}/{start.errorType}")
    started = time.monotonic()
    decisions = certified = blocked = fallbacks = 0
    reasons: Counter[str] = Counter()
    peak_rss = current_rss_bytes()
    try:
        for step in range(max_actions):
            current = observation.get("current") or {}
            result = int(current.get("result", -1))
            if result >= 0:
                return {
                    "seed": seed, "result": result, "actions": step,
                    "decisions": decisions, "certified": certified,
                    "blocked": blocked, "fallbacks": fallbacks,
                    "reasons": dict(sorted(reasons.items())),
                    "elapsedSeconds": time.monotonic() - started,
                    "peakRssBytes": peak_rss,
                    "playerUsedSeconds": [context.budget.used_seconds for context in contexts],
                }
            if int(current.get("turn", 0)) <= 0:
                action = _setup_action(observation, deck)
            else:
                actor = int(current["yourIndex"])
                action, is_certified, reason = choose_action(
                    to_observation_class(observation), context=contexts[actor], opponent_deck=deck)
                decisions += 1
                certified += int(is_certified)
                fallbacks += int(reason.startswith("emergency-policy"))
                reasons[reason] += 1
                native = contexts[actor].last_decision or {}
                blocked += int(native.get("searchStatus") == "blocked")
            _validate(action, observation)
            observation = battle_select(action)
            peak_rss = max(peak_rss, current_rss_bytes())
        raise RuntimeError("self-play action limit reached")
    finally:
        for context in contexts:
            context.reset()
        battle_finish()


def main() -> None:
    parser = argparse.ArgumentParser(description="Run submission policy against itself with closed-world validation")
    parser.add_argument("--first-seed", type=int, default=6)
    parser.add_argument("--games", type=int, default=1)
    parser.add_argument("--max-actions", type=int, default=1000)
    parser.add_argument("--turn-ms", type=int, default=2000)
    parser.add_argument("--selection-ms", type=int, default=250)
    args = parser.parse_args()
    if args.games < 1 or args.turn_ms < 1 or args.selection_ms < 1:
        raise ValueError("invalid self-play limits")
    os.environ["PTCG_EXACT_TURN_MS"] = str(args.turn_ms)
    os.environ["PTCG_EXACT_SELECTION_MS"] = str(args.selection_ms)
    deck = list(load_profile().cards)
    reports = [play(args.first_seed + index, deck, args.max_actions)
               for index in range(args.games)]
    if any(report["peakRssBytes"] >= 3 * 1024**3 for report in reports):
        raise RuntimeError("self-play exceeded 3 GiB RSS")
    if any(max(report["playerUsedSeconds"]) >= 600 for report in reports):
        raise RuntimeError("self-play exceeded a player's 600 second budget")
    print(json.dumps({"games": reports}, indent=2))


if __name__ == "__main__":
    main()
