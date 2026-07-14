from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "sample_submission" / "sample_submission"))

from cg.game import (battle_finish, battle_select, battle_start_ordered,
                     exact_replay_trace_begin, exact_replay_trace_drain,
                     exact_replay_trace_end)

SUPPORTED_ENGINE_VERSIONS = {"1.30.1"}


def _deck_step(replay: dict) -> tuple[int, list[int], list[int]]:
    for index, step in enumerate(replay.get("steps") or []):
        if len(step) >= 2:
            decks = [list((step[player] or {}).get("action") or []) for player in range(2)]
            if all(len(deck) == 60 for deck in decks):
                return index, decks[0], decks[1]
    raise ValueError("replay has no two-deck action")


def _ordered_decks(replay: dict) -> tuple[list[int], list[int]]:
    for step in replay.get("steps") or []:
        for record in step[:2]:
            for frame in (record or {}).get("visualize") or []:
                current = (frame or {}).get("current") or {}
                players = current.get("players") or []
                if len(players) != 2:
                    continue
                decks = [[int(card["id"]) for card in (player.get("deck") or []) if card]
                         for player in players]
                if all(len(deck) == 60 for deck in decks):
                    return decks[0], decks[1]
    raise ValueError("replay has no exact initial shuffled deck order")


def _replay_one(path: Path) -> list[dict]:
    replay = json.loads(path.read_text(encoding="utf-8"))
    if replay.get("module_version") not in SUPPORTED_ENGINE_VERSIONS:
        raise ValueError(f"unsupported engine version {replay.get('module_version')!r}")
    deck_at, submitted0, submitted1 = _deck_step(replay)
    deck0, deck1 = _ordered_decks(replay)
    if sorted(deck0) != sorted(submitted0) or sorted(deck1) != sorted(submitted1):
        raise ValueError("initial ordered deck does not match submitted deck")
    seed = int((replay.get("configuration") or {}).get("seed", 0))
    observation, start = battle_start_ordered(deck0, deck1, seed or 1)
    if not observation or start.errorType:
        raise ValueError(f"BattleStartSeeded failed: {start.errorPlayer}/{start.errorType}")
    try:
        exact_replay_trace_begin()
        steps = replay.get("steps") or []
        action_queue = [[], []]
        for index in range(deck_at + 1, len(steps)):
            previous = steps[index - 1]
            current_step = steps[index]
            for player in range(2):
                if player < len(previous) and (previous[player] or {}).get("status") == "ACTIVE":
                    action_queue[player].append(list((current_step[player] or {}).get("action") or []))
        action_at = [0, 0]
        while True:
            current = observation.get("current") or {}
            if int(current.get("result", -1)) >= 0:
                break
            actor = int(current.get("yourIndex", -1))
            if actor not in (0, 1) or action_at[actor] >= len(action_queue[actor]):
                if all(action_at[player] >= len(action_queue[player]) for player in range(2)):
                    break
                raise ValueError(f"replay is missing player {actor}'s next action")
            action = action_queue[actor][action_at[actor]]
            action_at[actor] += 1
            try:
                observation = battle_select(action)
            except (IndexError, ValueError) as error:
                select = observation.get("select") or {}
                raise ValueError(
                    f"illegal replay action actor={actor} turn={current.get('turn')} "
                    f"action={action} optionCount={len(select.get('option') or [])}"
                ) from error
        if any(action_at[player] != len(action_queue[player]) for player in range(2)):
            raise ValueError("engine finished before consuming every replay action")
        final_result = int((observation.get("current") or {}).get("result", -1))
        if final_result < 0:
            raise ValueError("replay ended before the native game reached a terminal state")
        rewards = [int(value) for value in (replay.get("rewards") or [0, 0])]
        expected_result = 0 if rewards == [1, -1] else 1 if rewards == [-1, 1] else 2 if rewards == [0, 0] else -1
        if final_result != expected_result:
            raise ValueError(f"native winner {final_result} does not match replay rewards {rewards}")
        samples = exact_replay_trace_drain()
        exact_replay_trace_end()
        if not samples:
            return []
        per_game_weight = 1.0 / len(samples)
        replay_id = str(replay.get("id") or (replay.get("info") or {}).get("EpisodeId") or path.stem)
        date = next((part for part in path.parts if len(part) == 10 and part[4:5] == "-"), "")
        for sample in samples:
            actor = int(sample["actor"])
            sample.update({
                "replayId": replay_id,
                "date": date,
                "target": int(rewards[actor]),
                "lossWeight": per_game_weight,
            })
        return samples
    finally:
        try:
            exact_replay_trace_end()
        finally:
            battle_finish()


def main() -> None:
    parser = argparse.ArgumentParser(description="Replay matches and extract exact post-checkup turn-end features")
    parser.add_argument("replays", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--max-files", type=int, default=0)
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()

    paths = sorted(args.replays.rglob("*.json"))
    if args.max_files > 0:
        paths = paths[:args.max_files]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    accepted = rejected = sample_count = 0
    digest = hashlib.sha256()
    with temporary.open("w", encoding="utf-8", newline="\n") as output:
        for path in paths:
            try:
                samples = _replay_one(path)
                for sample in samples:
                    line = json.dumps(sample, ensure_ascii=False, separators=(",", ":"))
                    output.write(line + "\n"); digest.update((line + "\n").encode())
                accepted += 1; sample_count += len(samples)
            except Exception as error:
                rejected += 1
                print(f"reject {path}: {error}", file=sys.stderr)
                if args.strict:
                    raise
    temporary.replace(args.output)
    manifest = {
        "schemaVersion": 2,
        "source": str(args.replays),
        "filesSeen": len(paths),
        "acceptedReplays": accepted,
        "rejectedReplays": rejected,
        "samples": sample_count,
        "sha256": digest.hexdigest(),
    }
    args.output.with_suffix(args.output.suffix + ".manifest.json").write_text(
        json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps(manifest))


if __name__ == "__main__":
    main()
