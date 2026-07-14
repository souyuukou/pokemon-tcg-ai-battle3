from __future__ import annotations

import argparse
import csv
import json
import math
import random
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "sample_submission" / "sample_submission"))

from exact_solver.nnue_v2 import (BELIEF_SCALE, DENSE, HIDDEN, RELATIONS,
                                  WEIGHT_SCALE, QuantizedModel,
                                  export_quantized, manifest_digest,
                                  predict_integer)


@dataclass
class Example:
    replay_id: str
    date: str
    key: str
    target: float
    weight: float
    dense: np.ndarray
    sparse: list[tuple[int, int, int]]


def read_card_ids(path: Path) -> list[int]:
    with path.open(encoding="utf-8-sig", newline="") as source:
        reader = csv.reader(source)
        next(reader)
        ids = {int(row[0]) for row in reader if row and row[0].strip().isdigit()}
    return [0, *sorted(card_id for card_id in ids if card_id != 0)]


def read_attack_tokens() -> list[int]:
    """Use the native registry; this is vocabulary discovery, not feature extraction."""
    try:
        from cg.sim import lib
        attacks = json.loads(lib.AllAttack().decode())
        return [1_000_000 + int(attack["attackId"]) for attack in attacks]
    except Exception as error:
        raise RuntimeError("native attack registry is required to build the complete V2 vocabulary") from error


def load_examples(path: Path, max_samples: int) -> list[Example]:
    examples: list[Example] = []
    with path.open(encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            if max_samples and len(examples) >= max_samples:
                break
            record = json.loads(line)
            dense = np.asarray(record["dense"], dtype=np.int16)
            if dense.shape != (DENSE,):
                raise ValueError(f"{path}:{line_number}: expected {DENSE} dense features")
            sparse = [(int(a), int(b), int(c)) for a, b, c in record["sparse"]]
            if any(not 0 <= relation < RELATIONS for relation, _, _ in sparse):
                raise ValueError(f"{path}:{line_number}: invalid sparse relation")
            examples.append(Example(
                replay_id=str(record["replayId"]), date=str(record.get("date", "")),
                key=str(record["informationStateKey"]), target=float(record["target"]),
                weight=float(record["lossWeight"]), dense=dense, sparse=sparse,
            ))
    if not examples:
        raise ValueError("dataset contains no accepted turn-end examples")
    return examples


def split_replays(examples: list[Example]) -> dict[str, list[int]]:
    by_replay: dict[str, list[int]] = defaultdict(list)
    dates: dict[str, str] = {}
    for index, example in enumerate(examples):
        by_replay[example.replay_id].append(index)
        dates[example.replay_id] = max(dates.get(example.replay_id, ""), example.date)
    ordered = sorted(by_replay, key=lambda replay_id: (dates[replay_id], replay_id))
    count = len(ordered)
    test_count = max(1, math.ceil(count * 0.10)) if count >= 3 else 0
    validation_count = max(1, math.ceil(count * 0.10)) if count >= 3 else 0
    if test_count + validation_count >= count:
        validation_count = 1 if count >= 3 else 0
        test_count = 1 if count >= 2 else 0
    groups = {
        "train": ordered[:count - validation_count - test_count],
        "validation": ordered[count - validation_count - test_count:count - test_count],
        "test": ordered[count - test_count:] if test_count else [],
    }
    if not groups["train"]:
        raise ValueError("at least three replay IDs are required for leakage-free splitting")
    return {name: [index for replay_id in ids for index in by_replay[replay_id]]
            for name, ids in groups.items()}


def build_sparse_batch(examples: list[Example], indices: np.ndarray,
                       card_index: dict[int, int], device):
    import torch
    sample_index: list[int] = []
    feature_index: list[int] = []
    values: list[float] = []
    for batch_index, example_index in enumerate(indices):
        for relation, card_id, q8 in examples[int(example_index)].sparse:
            sample_index.append(batch_index)
            feature_index.append(relation * len(card_index) + card_index.get(card_id, 0))
            values.append(q8 / BELIEF_SCALE)
    return (torch.tensor(sample_index, dtype=torch.long, device=device),
            torch.tensor(feature_index, dtype=torch.long, device=device),
            torch.tensor(values, dtype=torch.float32, device=device))


def fake_quant(value, multiplier: float, minimum: int, maximum: int):
    quantized = (value * multiplier).round().clamp(minimum, maximum) / multiplier
    return value + (quantized - value).detach()


def main() -> None:
    parser = argparse.ArgumentParser(description="Train the generic sparse V2 evaluator from native turn leaves")
    parser.add_argument("dataset", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--card-table", type=Path, default=ROOT / "EN_Card_Data.csv")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--epochs", type=int, default=12)
    parser.add_argument("--qat-epochs", type=int, default=4)
    parser.add_argument("--batch-size", type=int, default=1024)
    parser.add_argument("--max-samples", type=int, default=0)
    parser.add_argument("--seed", type=int, default=20260714)
    parser.add_argument("--legacy-predictions", type=Path,
                        help="optional JSON mapping InformationStateKey to current V1 prediction")
    parser.add_argument("--require-gates", action="store_true")
    args = parser.parse_args()

    import torch
    torch.manual_seed(args.seed); random.seed(args.seed); np.random.seed(args.seed)
    examples = load_examples(args.dataset, args.max_samples)
    splits = split_replays(examples)
    card_ids = sorted(set(read_card_ids(args.card_table) + read_attack_tokens()))
    card_index = {card_id: index for index, card_id in enumerate(card_ids)}
    device = torch.device("cpu")

    class SparseNnue(torch.nn.Module):
        def __init__(self):
            super().__init__()
            self.dense_weight = torch.nn.Parameter(torch.empty(HIDDEN, DENSE))
            self.sparse_weight = torch.nn.Parameter(torch.zeros(RELATIONS * len(card_ids), HIDDEN))
            self.hidden_bias = torch.nn.Parameter(torch.zeros(HIDDEN))
            self.output_weight = torch.nn.Parameter(torch.empty(HIDDEN))
            self.output_bias = torch.nn.Parameter(torch.zeros(()))
            torch.nn.init.xavier_uniform_(self.dense_weight)
            torch.nn.init.uniform_(self.output_weight, -0.1, 0.1)

        def forward(self, dense, sample_index, feature_index, values, qat=False):
            dw, sw, hb, ow, ob = (self.dense_weight, self.sparse_weight,
                                  self.hidden_bias, self.output_weight, self.output_bias)
            if qat:
                dw = fake_quant(dw, WEIGHT_SCALE / 32, -32768, 32767)
                sw = fake_quant(sw, WEIGHT_SCALE / BELIEF_SCALE, -32768, 32767)
                hb = fake_quant(hb, WEIGHT_SCALE, -(1 << 31), (1 << 31) - 1)
                ow = fake_quant(ow, WEIGHT_SCALE, -32768, 32767)
                ob = fake_quant(ob, WEIGHT_SCALE * WEIGHT_SCALE, -(1 << 63), (1 << 63) - 1)
            accumulator = dense @ dw.T / 32 + hb
            if feature_index.numel():
                additions = sw[feature_index] * values[:, None]
                accumulator = accumulator.index_add(0, sample_index, additions)
            activation = torch.clamp(accumulator, 0.0, 127.0)
            return activation @ ow + ob

    model = SparseNnue().to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=5e-4, weight_decay=1e-6)

    def tensors(indices: np.ndarray):
        dense = torch.from_numpy(np.stack([examples[int(i)].dense for i in indices]).astype(np.float32)).to(device)
        target = torch.tensor([examples[int(i)].target for i in indices], dtype=torch.float32, device=device)
        weight = torch.tensor([examples[int(i)].weight for i in indices], dtype=torch.float32, device=device)
        sparse = build_sparse_batch(examples, indices, card_index, device)
        return dense, target, weight, sparse

    rng = np.random.default_rng(args.seed)
    train_indices = np.asarray(splits["train"], dtype=np.int64)
    for epoch in range(args.epochs):
        permutation = rng.permutation(train_indices)
        qat = epoch >= max(0, args.epochs - args.qat_epochs)
        total_loss = total_weight = 0.0
        model.train()
        for start in range(0, len(permutation), args.batch_size):
            indices = permutation[start:start + args.batch_size]
            dense, target, weight, sparse = tensors(indices)
            prediction = model(dense, *sparse, qat=qat)
            loss = ((prediction - target).square() * weight).sum() / weight.sum()
            optimizer.zero_grad(set_to_none=True); loss.backward(); optimizer.step()
            total_loss += float((loss.detach() * weight.sum()).cpu()); total_weight += float(weight.sum().cpu())
        print(f"epoch={epoch + 1} qat={int(qat)} train_mse={total_loss / total_weight:.7f}")

    def quantized_model() -> QuantizedModel:
        with torch.no_grad():
            dense_weight = np.rint(model.dense_weight.cpu().numpy() * WEIGHT_SCALE / 32).clip(-32768, 32767).astype(np.int16)
            sparse_weight = np.rint(model.sparse_weight.cpu().numpy() * WEIGHT_SCALE / BELIEF_SCALE).clip(-32768, 32767).astype(np.int16)
            sparse_weight = sparse_weight.reshape(RELATIONS, len(card_ids), HIDDEN)
            hidden_bias = np.rint(model.hidden_bias.cpu().numpy() * WEIGHT_SCALE).clip(-(1 << 31), (1 << 31) - 1).astype(np.int32)
            output_weight = np.rint(model.output_weight.cpu().numpy() * WEIGHT_SCALE).clip(-32768, 32767).astype(np.int16)
            output_bias = int(np.rint(float(model.output_bias.cpu()) * WEIGHT_SCALE * WEIGHT_SCALE))
        return QuantizedModel(np.asarray(card_ids, dtype=np.int32), dense_weight, sparse_weight,
                              hidden_bias, output_weight, output_bias,
                              manifest_digest(args.manifest))

    quantized = quantized_model()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    export_quantized(args.output, quantized)

    native_bit_exact = False
    try:
        from cg.api import (exact_evaluate_features_v2, exact_load_evaluator_model,
                            exact_unload_evaluator_model)
        exact_load_evaluator_model(str(args.output.resolve()))
        try:
            native_bit_exact = all(
                exact_evaluate_features_v2(example.dense.tolist(), [list(item) for item in example.sparse])
                == predict_integer(quantized, example.dense, example.sparse)
                for example in examples[:min(256, len(examples))]
            )
        finally:
            exact_unload_evaluator_model()
    except Exception as error:
        print(f"native bit check failed to run: {error}", file=sys.stderr)

    def evaluate(indices: list[int]) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        float_predictions: list[float] = []
        integer_predictions: list[float] = []
        targets: list[float] = []
        model.eval()
        with torch.no_grad():
            for start in range(0, len(indices), args.batch_size):
                batch = np.asarray(indices[start:start + args.batch_size], dtype=np.int64)
                dense, target, _, sparse = tensors(batch)
                float_predictions.extend(model(dense, *sparse, qat=False).cpu().numpy().tolist())
                targets.extend(target.cpu().numpy().tolist())
                integer_predictions.extend(predict_integer(quantized, examples[int(i)].dense,
                                                            examples[int(i)].sparse) / 100_000_000
                                           for i in batch)
        return np.asarray(targets), np.asarray(float_predictions), np.asarray(integer_predictions)

    report: dict[str, object] = {"examples": len(examples), "splits": {k: len(v) for k, v in splits.items()}}
    gates: dict[str, bool] = {"nativeBitExact": native_bit_exact,
                              "hasUnseenTestSplit": bool(splits["test"])}
    report["nativeBitExactSamples"] = min(256, len(examples)) if native_bit_exact else 0
    if splits["test"]:
        target, floating, integer = evaluate(splits["test"])
        float_mse = float(np.mean((floating - target) ** 2))
        integer_mse = float(np.mean((integer - target) ** 2))
        zero_mse = float(np.mean(target ** 2))
        sign_accuracy = float(np.mean(np.sign(integer) == np.sign(target)))
        quantization_delta = (integer_mse - float_mse) / max(float_mse, 1e-12)
        report.update(testFloatMse=float_mse, testQuantizedMse=integer_mse,
                      zeroMse=zero_mse, signAccuracy=sign_accuracy,
                      quantizationMseDelta=quantization_delta)
        gates.update(zeroImprovement15=integer_mse <= zero_mse * .85,
                     signAccuracy70=sign_accuracy >= .70,
                     quantizationWithin1Percent=quantization_delta <= .01)
        if args.legacy_predictions:
            legacy_map = json.loads(args.legacy_predictions.read_text(encoding="utf-8"))
            legacy = np.asarray([float(legacy_map[examples[i].key]) for i in splits["test"]])
            differences = (integer - target) ** 2 - (legacy - target) ** 2
            replay_differences: dict[str, list[float]] = defaultdict(list)
            for difference, example_index in zip(differences, splits["test"]):
                replay_differences[examples[example_index].replay_id].append(float(difference))
            paired = np.asarray([np.mean(values) for values in replay_differences.values()])
            bootstrap = np.random.default_rng(args.seed).choice(
                paired, (2000, len(paired)), replace=True).mean(axis=1)
            upper = float(np.quantile(bootstrap, .975))
            report.update(legacyMse=float(np.mean((legacy - target) ** 2)), pairedBootstrapUpper95=upper)
            gates["beatsLegacyPaired95"] = upper < 0
        else:
            gates["beatsLegacyPaired95"] = False
            report["legacyGate"] = "not evaluated: pass --legacy-predictions"
    report["gates"] = gates
    report["allGatesPassed"] = bool(gates) and all(gates.values())
    print(json.dumps(report, indent=2))
    if args.require_gates and not report["allGatesPassed"]:
        raise SystemExit("model adoption gates failed")


if __name__ == "__main__":
    main()
