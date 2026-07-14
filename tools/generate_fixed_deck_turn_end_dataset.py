from __future__ import annotations

import argparse
import hashlib
import json
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "sample_submission" / "sample_submission"))

from cg.game import (battle_finish, battle_select, battle_start_seeded,
                     exact_replay_trace_begin, exact_replay_trace_drain,
                     exact_replay_trace_end)


MAJKEL_85795098 = [
    5, 5, 13, 19, 19, 19, 19, 66, 66, 140, 305, 305, 305, 343,
    741, 741, 741, 741, 742, 742, 742, 742, 743, 743, 743, 743,
    1079, 1079, 1079, 1081, 1081, 1081, 1081, 1086, 1086, 1086,
    1086, 1097, 1129, 1152, 1152, 1152, 1152, 1182, 1182, 1182,
    1184, 1197, 1197, 1197, 1225, 1225, 1225, 1225, 1231, 1231,
    1231, 1231, 1266, 1266,
]


def _choose_action(observation: dict, rng: random.Random, epsilon: float) -> list[int]:
    select = observation.get("select")
    if select is None:
        return list(MAJKEL_85795098)
    minimum = int(select["minCount"])
    maximum = int(select["maxCount"])
    options = select.get("option") or []
    if maximum == 0:
        return []
    if int(select["type"]) == 0 and int(select["context"]) == 0:
        # Develop the board before attacking, with a small amount of policy
        # noise so the evaluator sees more than one deterministic line.
        priority = {9: 70, 8: 65, 10: 60, 7: 50, 13: 40, 12: 20, 14: 0}
        if rng.random() < epsilon:
            return [rng.randrange(len(options))]
        best = max(priority.get(int(option["type"]), -1) for option in options)
        candidates = [index for index, option in enumerate(options)
                      if priority.get(int(option["type"]), -1) == best]
        return [rng.choice(candidates)]
    count = minimum if minimum == maximum else rng.randint(minimum, maximum)
    return sorted(rng.sample(range(len(options)), count))


def _play(seed: int, policy_seed: int, epsilon: float, max_actions: int) -> tuple[list[dict], list[int]]:
    observation, start = battle_start_seeded(MAJKEL_85795098, MAJKEL_85795098, seed)
    if not observation or start.errorType:
        raise ValueError(f"battle start failed: {start.errorPlayer}/{start.errorType}")
    rng = random.Random(policy_seed)
    try:
        exact_replay_trace_begin()
        for _ in range(max_actions):
            result = int((observation.get("current") or {}).get("result", -1))
            if result >= 0:
                rewards = [1, -1] if result == 0 else [-1, 1] if result == 1 else [0, 0]
                return exact_replay_trace_drain(), rewards
            observation = battle_select(_choose_action(observation, rng, epsilon))
        raise ValueError("action limit reached")
    finally:
        try:
            exact_replay_trace_end()
        finally:
            battle_finish()


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate native fixed-deck turn-end self-play records")
    parser.add_argument("output", type=Path)
    parser.add_argument("--games", type=int, default=5_000)
    parser.add_argument("--first-seed", type=int, default=1)
    parser.add_argument("--policy-seed", type=int, default=20260715)
    parser.add_argument("--epsilon", type=float, default=0.15)
    parser.add_argument("--max-actions", type=int, default=3_000)
    parser.add_argument("--progress-every", type=int, default=250)
    args = parser.parse_args()
    if args.games < 1 or not 0 <= args.epsilon <= 1:
        raise ValueError("invalid generation arguments")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    digest = hashlib.sha256()
    accepted = rejected = sample_count = 0
    with temporary.open("w", encoding="utf-8", newline="\n") as output:
        for game_index in range(args.games):
            seed = args.first_seed + game_index
            try:
                samples, rewards = _play(seed, args.policy_seed + seed, args.epsilon, args.max_actions)
                if not samples:
                    raise ValueError("game produced no non-terminal turn leaves")
                weight = 1.0 / len(samples)
                for sample in samples:
                    actor = int(sample["actor"])
                    sample.update({
                        "replayId": f"fixed-selfplay-{seed:08d}",
                        "date": f"selfplay-{seed:08d}",
                        "target": rewards[actor],
                        "lossWeight": weight,
                    })
                    line = json.dumps(sample, ensure_ascii=False, separators=(",", ":"))
                    output.write(line + "\n")
                    digest.update((line + "\n").encode())
                accepted += 1
                sample_count += len(samples)
            except Exception as error:
                rejected += 1
                print(f"reject seed={seed}: {error}", file=sys.stderr)
            if args.progress_every > 0 and (game_index + 1) % args.progress_every == 0:
                print(f"games={game_index + 1}/{args.games} accepted={accepted} "
                      f"rejected={rejected} samples={sample_count}", file=sys.stderr)
    temporary.replace(args.output)
    manifest = {
        "schemaVersion": 3,
        "source": "native-fixed-selfplay",
        "deck": "majkel1337-85795098",
        "firstSeed": args.first_seed,
        "gamesRequested": args.games,
        "acceptedReplays": accepted,
        "rejectedReplays": rejected,
        "samples": sample_count,
        "epsilon": args.epsilon,
        "policySeed": args.policy_seed,
        "sha256": digest.hexdigest(),
    }
    args.output.with_suffix(args.output.suffix + ".manifest.json").write_text(
        json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps(manifest))


if __name__ == "__main__":
    main()
