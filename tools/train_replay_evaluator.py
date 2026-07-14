from __future__ import annotations

import argparse
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "sample_submission" / "sample_submission"))

from exact_solver.nnue import FEATURE_SCALE, HIDDEN, INPUTS, export_quantized, iter_replay_examples


def main() -> None:
    parser = argparse.ArgumentParser(description="Train the exact-search 48x8 CPU evaluator from replay outcomes")
    parser.add_argument("replays", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--max-files", type=int, default=20_000)
    parser.add_argument("--stride", type=int, default=2)
    parser.add_argument("--epochs", type=int, default=4)
    parser.add_argument("--max-examples", type=int, default=2_000_000)
    parser.add_argument("--seed", type=int, default=20260714)
    args = parser.parse_args()

    import torch

    random.seed(args.seed)
    torch.manual_seed(args.seed)
    paths = sorted(args.replays.rglob("*.json"))
    random.shuffle(paths)
    paths = paths[:args.max_files]
    import numpy as np
    feature_cache = np.empty((args.max_examples, INPUTS), dtype=np.int8)
    target_cache = np.empty(args.max_examples, dtype=np.int8)
    example_count = 0
    for features, target in iter_replay_examples(paths, args.stride):
        if example_count == args.max_examples:
            break
        feature_cache[example_count] = features
        target_cache[example_count] = int(target)
        example_count += 1
    if not example_count:
        raise SystemExit("no replay observations found")
    feature_cache = feature_cache[:example_count]
    target_cache = target_cache[:example_count]
    validation_count = max(1, example_count // 10)
    train_count = example_count - validation_count
    model = torch.nn.Sequential(torch.nn.Linear(INPUTS, HIDDEN), torch.nn.Hardtanh(0.0, 127.0), torch.nn.Linear(HIDDEN, 1))
    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-3, weight_decay=1e-5)
    for epoch in range(args.epochs):
        generator = np.random.default_rng(args.seed + epoch)
        permutation = generator.permutation(train_count)
        total, seen = 0.0, 0
        for start in range(0, train_count, 4096):
            index = permutation[start:start + 4096]
            x = torch.from_numpy(feature_cache[index].astype(np.float32))
            y = torch.from_numpy(target_cache[index].astype(np.float32)).unsqueeze(1)
            prediction = model(x / FEATURE_SCALE)
            loss = torch.nn.functional.mse_loss(prediction, y)
            optimizer.zero_grad(set_to_none=True); loss.backward(); optimizer.step()
            total += float(loss.detach()) * len(index); seen += len(index)
        with torch.no_grad():
            vx = torch.from_numpy(feature_cache[train_count:].astype(np.float32))
            vy = torch.from_numpy(target_cache[train_count:].astype(np.float32)).unsqueeze(1)
            validation = float(torch.nn.functional.mse_loss(model(vx / FEATURE_SCALE), vy))
        print(f"epoch={epoch + 1} examples={seen} train_mse={total / seen:.6f} validation_mse={validation:.6f}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    export_quantized(args.output, model)
    print(f"wrote {args.output} ({args.output.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
