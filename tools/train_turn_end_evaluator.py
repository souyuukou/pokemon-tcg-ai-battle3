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

from exact_solver.nnue_v3 import (BELIEF_SCALE, ENTITY_DENSE, ENTITY_HIDDEN,
                                  ENTITY_RELATIONS, FEATURE_SCHEMA, GLOBAL_DENSE,
                                  GLOBAL_HIDDEN, GLOBAL_RELATIONS, POOLS,
                                  WEIGHT_SCALE, EntityFeatures, FeatureRecord,
                                  QuantizedModel, export_quantized, fnv1a,
                                  manifest_digest, predict_integer)


@dataclass
class Example:
    replay_id: str
    date: str
    key: str
    target: float
    weight: float
    feature: FeatureRecord


def read_card_ids(path: Path) -> list[int]:
    with path.open(encoding="utf-8-sig", newline="") as source:
        reader = csv.reader(source); next(reader)
        return sorted({int(row[0]) for row in reader if row and row[0].strip().isdigit()})


def native_attack_ids() -> list[int]:
    from cg.sim import lib
    return sorted({int(item["attackId"]) for item in json.loads(lib.AllAttack().decode())})


def vocabulary(card_ids: list[int], attack_ids: list[int]) -> tuple[list[int], list[int], list[int]]:
    effects = list(range(2_000_001, 2_001_001))
    combos = [3_000_000 + card for card in card_ids] + [3_250_000 + card for card in card_ids]
    combos += [3_500_000 + attack for attack in attack_ids]
    tokens = [0, *card_ids, *(1_000_000 + attack for attack in attack_ids), *effects, *combos]
    try:
        from cg.sim import lib
        if hasattr(lib, "ExactEvaluatorTokensV3"):
            tokens.extend(int(value) for value in json.loads(lib.ExactEvaluatorTokensV3().decode()))
    except Exception:
        pass
    return sorted(set(tokens)), effects, sorted(set(combos))


def load_examples(path: Path, max_samples: int) -> list[Example]:
    result: list[Example] = []
    with path.open(encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            if max_samples and len(result) >= max_samples:
                break
            row = json.loads(line)
            if int(row.get("featureSchemaVersion", 0)) != FEATURE_SCHEMA:
                raise ValueError(f"{path}:{line_number}: expected V3 feature record")
            gd = [int(value) for value in row["globalDense"]]
            gs = [[int(value) for value in item] for item in row["globalSparse"]]
            entities = [EntityFeatures(int(entity["pool"]), [int(v) for v in entity["dense"]],
                                       [[int(v) for v in item] for item in entity["sparse"]])
                        for entity in row["entities"]]
            if len(gd) != GLOBAL_DENSE or any(len(e.dense) != ENTITY_DENSE for e in entities):
                raise ValueError(f"{path}:{line_number}: invalid V3 dimensions")
            result.append(Example(str(row["replayId"]), str(row.get("date", "")),
                                  str(row["informationStateKey"]), float(row["target"]),
                                  float(row["lossWeight"]), FeatureRecord(gd, gs, entities)))
    if not result:
        raise ValueError("dataset contains no V3 examples")
    return result


def split_replays(examples: list[Example]) -> dict[str, list[int]]:
    by_replay: dict[str, list[int]] = defaultdict(list); dates: dict[str, str] = {}
    for index, example in enumerate(examples):
        by_replay[example.replay_id].append(index)
        dates[example.replay_id] = max(dates.get(example.replay_id, ""), example.date)
    ordered = sorted(by_replay, key=lambda key: (dates[key], key)); count = len(ordered)
    test = max(1, math.ceil(count * .1)) if count >= 3 else 0
    validation = max(1, math.ceil(count * .1)) if count >= 3 else 0
    while test + validation >= count and validation:
        validation -= 1
    groups = {"train": ordered[:count-validation-test],
              "validation": ordered[count-validation-test:count-test],
              "test": ordered[count-test:] if test else []}
    if not groups["train"]:
        raise ValueError("at least three replay IDs are required")
    return {name: [i for replay in ids for i in by_replay[replay]] for name, ids in groups.items()}


def fake_quant(value, scale: float, minimum: int, maximum: int):
    quantized = (value * scale).round().clamp(minimum, maximum) / scale
    return value + (quantized - value).detach()


def main() -> None:
    parser = argparse.ArgumentParser(description="Train the structured integer V3 turn-end evaluator")
    parser.add_argument("dataset", type=Path); parser.add_argument("output", type=Path)
    parser.add_argument("--card-table", type=Path, default=ROOT / "EN_Card_Data.csv")
    parser.add_argument("--manifest", type=Path); parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--qat-epochs", type=int, default=6); parser.add_argument("--max-samples", type=int, default=0)
    parser.add_argument("--seed", type=int, default=20260714); parser.add_argument("--legacy-predictions", type=Path)
    parser.add_argument("--require-gates", action="store_true")
    args = parser.parse_args()

    import torch
    torch.manual_seed(args.seed); np.random.seed(args.seed); random.seed(args.seed)
    examples = load_examples(args.dataset, args.max_samples); splits = split_replays(examples)
    cards = read_card_ids(args.card_table); attacks = native_attack_ids()
    tokens, effects, combos = vocabulary(cards, attacks); token_index = {token: i for i, token in enumerate(tokens)}

    class EntityNnue(torch.nn.Module):
        def __init__(self):
            super().__init__()
            self.edw = torch.nn.Parameter(torch.empty(ENTITY_HIDDEN, ENTITY_DENSE))
            self.esw = torch.nn.Parameter(torch.zeros(ENTITY_RELATIONS, len(tokens), ENTITY_HIDDEN))
            self.eb = torch.nn.Parameter(torch.zeros(ENTITY_HIDDEN))
            self.gdw = torch.nn.Parameter(torch.empty(GLOBAL_HIDDEN, GLOBAL_DENSE))
            self.gsw = torch.nn.Parameter(torch.zeros(GLOBAL_RELATIONS, len(tokens), GLOBAL_HIDDEN))
            self.pool = torch.nn.Parameter(torch.empty(POOLS, ENTITY_HIDDEN, GLOBAL_HIDDEN))
            self.gb = torch.nn.Parameter(torch.zeros(GLOBAL_HIDDEN))
            self.ow = torch.nn.Parameter(torch.empty(GLOBAL_HIDDEN)); self.ob = torch.nn.Parameter(torch.zeros(()))
            for value in (self.edw, self.gdw, self.pool): torch.nn.init.xavier_uniform_(value)
            torch.nn.init.uniform_(self.ow, -.1, .1)

        def parameters_for(self, qat: bool):
            if not qat: return self.edw, self.esw, self.eb, self.gdw, self.gsw, self.pool, self.gb, self.ow, self.ob
            return (fake_quant(self.edw, WEIGHT_SCALE, -32768, 32767),
                    fake_quant(self.esw, WEIGHT_SCALE / BELIEF_SCALE, -32768, 32767),
                    fake_quant(self.eb, WEIGHT_SCALE, -(1 << 31), (1 << 31)-1),
                    fake_quant(self.gdw, WEIGHT_SCALE, -32768, 32767),
                    fake_quant(self.gsw, WEIGHT_SCALE / BELIEF_SCALE, -32768, 32767),
                    fake_quant(self.pool, WEIGHT_SCALE, -32768, 32767),
                    fake_quant(self.gb, WEIGHT_SCALE, -(1 << 31), (1 << 31)-1),
                    fake_quant(self.ow, WEIGHT_SCALE, -32768, 32767),
                    fake_quant(self.ob, WEIGHT_SCALE * WEIGHT_SCALE, -(1 << 63), (1 << 63)-1))

        def one(self, feature: FeatureRecord, qat: bool):
            edw, esw, eb, gdw, gsw, pool, gb, ow, ob = self.parameters_for(qat)
            gd = torch.tensor(feature.global_dense, dtype=torch.float32)
            global_acc = gdw @ gd + gb
            for relation, token, value in feature.global_sparse:
                global_acc = global_acc + gsw[int(relation), token_index.get(int(token), 0)] * (int(value) / BELIEF_SCALE)
            for entity in feature.entities:
                dense = torch.tensor(entity.dense, dtype=torch.float32)
                acc = edw @ dense + eb
                for relation, token, value in entity.sparse:
                    acc = acc + esw[int(relation), token_index.get(int(token), 0)] * (int(value) / BELIEF_SCALE)
                global_acc = global_acc + torch.clamp(acc, 0, 127) @ pool[int(entity.pool)]
            return torch.clamp(global_acc, 0, 127) @ ow + ob

        def forward(self, indices, qat=False):
            return torch.stack([self.one(examples[int(i)].feature, qat) for i in indices])

    model = EntityNnue(); optimizer = torch.optim.AdamW(model.parameters(), lr=5e-4, weight_decay=1e-6)
    train = np.asarray(splits["train"], dtype=np.int64); rng = np.random.default_rng(args.seed)
    for epoch in range(args.epochs):
        qat = epoch >= max(0, args.epochs - args.qat_epochs); permutation = rng.permutation(train)
        total = total_weight = 0.0
        for start in range(0, len(permutation), 64):
            batch = permutation[start:start+64]
            prediction = model(batch, qat)
            target = torch.tensor([examples[int(i)].target for i in batch])
            weight = torch.tensor([examples[int(i)].weight for i in batch])
            loss = ((prediction-target).square()*weight).sum()/weight.sum()
            optimizer.zero_grad(set_to_none=True); loss.backward(); optimizer.step()
            total += float(loss.detach()*weight.sum()); total_weight += float(weight.sum())
        print(f"epoch={epoch+1} qat={int(qat)} train_mse={total/total_weight:.7f}")

    with torch.no_grad():
        quantized = QuantizedModel(
            np.asarray(tokens, dtype=np.int32),
            np.rint(model.edw.detach().cpu().numpy()*WEIGHT_SCALE).clip(-32768,32767).astype(np.int16),
            np.rint(model.esw.detach().cpu().numpy()*WEIGHT_SCALE/BELIEF_SCALE).clip(-32768,32767).astype(np.int16),
            np.rint(model.eb.detach().cpu().numpy()*WEIGHT_SCALE).clip(-(1<<31),(1<<31)-1).astype(np.int32),
            np.rint(model.gdw.detach().cpu().numpy()*WEIGHT_SCALE).clip(-32768,32767).astype(np.int16),
            np.rint(model.gsw.detach().cpu().numpy()*WEIGHT_SCALE/BELIEF_SCALE).clip(-32768,32767).astype(np.int16),
            np.rint(model.pool.detach().cpu().numpy()*WEIGHT_SCALE).clip(-32768,32767).astype(np.int16),
            np.rint(model.gb.detach().cpu().numpy()*WEIGHT_SCALE).clip(-(1<<31),(1<<31)-1).astype(np.int32),
            np.rint(model.ow.detach().cpu().numpy()*WEIGHT_SCALE).clip(-32768,32767).astype(np.int16),
            int(np.rint(float(model.ob)*WEIGHT_SCALE*WEIGHT_SCALE)), manifest_digest(args.manifest),
            fnv1a(np.asarray(cards,dtype="<i4").tobytes()), fnv1a(np.asarray(effects,dtype="<i4").tobytes()),
            fnv1a(np.asarray(combos,dtype="<i4").tobytes()))
    args.output.parent.mkdir(parents=True, exist_ok=True); export_quantized(args.output, quantized)

    native_bit_exact = False
    try:
        from cg.api import exact_evaluate_features_v3, exact_load_evaluator_model, exact_unload_evaluator_model
        exact_load_evaluator_model(str(args.output.resolve()))
        try:
            native_bit_exact = all(exact_evaluate_features_v3(list(e.feature.global_dense),
                [list(x) for x in e.feature.global_sparse],
                [{"pool": x.pool, "dense": list(x.dense), "sparse": [list(y) for y in x.sparse]}
                 for x in e.feature.entities]) == predict_integer(quantized, e.feature)
                for e in examples[:min(256,len(examples))])
        finally: exact_unload_evaluator_model()
    except Exception as error: print(f"native bit check failed: {error}", file=sys.stderr)

    def predictions(indices):
        with torch.no_grad(): floating = model(np.asarray(indices), False).numpy()
        integer = np.asarray([predict_integer(quantized, examples[i].feature)/100_000_000 for i in indices])
        target = np.asarray([examples[i].target for i in indices]); return target, floating, integer

    report = {"examples": len(examples), "splits": {k:len(v) for k,v in splits.items()}}
    gates = {"nativeBitExact": native_bit_exact, "hasUnseenTestSplit": bool(splits["test"])}
    if splits["test"]:
        target, floating, integer = predictions(splits["test"]); zero_mse = float(np.mean(target**2))
        float_mse = float(np.mean((floating-target)**2)); integer_mse = float(np.mean((integer-target)**2))
        gates.update(zeroImprovement15=integer_mse <= zero_mse*.85,
                     signAccuracy70=float(np.mean(np.sign(integer)==np.sign(target))) >= .70,
                     quantizationWithin1Percent=(integer_mse-float_mse)/max(float_mse,1e-12) <= .01)
        report.update(testFloatMse=float_mse,testQuantizedMse=integer_mse,zeroMse=zero_mse)
        if args.legacy_predictions:
            legacy=json.loads(args.legacy_predictions.read_text()); differences=[]
            by_replay=defaultdict(list)
            for prediction,index in zip(integer,splits["test"]):
                e=examples[index]; by_replay[e.replay_id].append((prediction-e.target)**2-(float(legacy[e.key])-e.target)**2)
            paired=np.asarray([np.mean(v) for v in by_replay.values()]); bootstrap=rng.choice(paired,(2000,len(paired)),replace=True).mean(1)
            gates["beatsLegacyPaired95"] = float(np.quantile(bootstrap,.975)) < 0
        else: gates["beatsLegacyPaired95"] = False
    report["gates"] = gates; report["allGatesPassed"] = bool(gates) and all(gates.values())
    print(json.dumps(report,indent=2))
    if args.require_gates and not report["allGatesPassed"]: raise SystemExit("V3 adoption gates failed")


if __name__ == "__main__": main()
