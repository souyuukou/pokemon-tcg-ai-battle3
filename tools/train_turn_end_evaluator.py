from __future__ import annotations

import argparse
import copy
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
                                  manifest_digest, predict_integer_many)


@dataclass
class Example:
    replay_id: str
    date: str
    key: str
    target: float
    weight: float
    feature: FeatureRecord


@dataclass
class PackedBatch:
    """A tensorized batch with sparse relations grouped for index_add."""

    global_dense: object
    global_sparse_relation: object
    global_sparse_token: object
    global_sparse_value: object
    global_sparse_sample: object
    entity_dense: object
    entity_pool: object
    entity_sample: object
    entity_sparse_relation: object
    entity_sparse_token: object
    entity_sparse_value: object
    entity_sparse_entity: object
    target: object
    weight: object


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


def fake_round_away(value, scale: float):
    """Match the native signed divide rounding while preserving gradients."""
    magnitude = (value.abs() * scale + .5).floor()
    quantized = value.sign() * magnitude / scale
    return value + (quantized - value).detach()


def pack_batch(examples: list[Example], indices, token_index: dict[int, int], torch) -> PackedBatch:
    global_dense = []
    global_sparse = []
    entity_dense = []
    entity_pool = []
    entity_sample = []
    entity_sparse = []
    targets = []
    weights = []
    for sample, raw_index in enumerate(indices):
        example = examples[int(raw_index)]
        global_dense.append(example.feature.global_dense)
        targets.append(example.target)
        weights.append(example.weight)
        for relation, token, value in example.feature.global_sparse:
            global_sparse.append((int(relation), token_index.get(int(token), 0),
                                  int(value) / BELIEF_SCALE, sample))
        for entity in example.feature.entities:
            entity_index = len(entity_dense)
            entity_dense.append(entity.dense)
            entity_pool.append(int(entity.pool))
            entity_sample.append(sample)
            for relation, token, value in entity.sparse:
                entity_sparse.append((int(relation), token_index.get(int(token), 0),
                                      int(value) / BELIEF_SCALE, entity_index))

    def columns(rows, column_count, dtypes):
        if rows:
            return tuple(torch.tensor([row[column] for row in rows], dtype=dtypes[column])
                         for column in range(column_count))
        return tuple(torch.empty(0, dtype=dtype) for dtype in dtypes)

    gs = columns(global_sparse, 4, (torch.long, torch.long, torch.float32, torch.long))
    es = columns(entity_sparse, 4, (torch.long, torch.long, torch.float32, torch.long))
    return PackedBatch(
        torch.tensor(global_dense, dtype=torch.float32), *gs,
        torch.tensor(entity_dense, dtype=torch.float32).reshape(-1, ENTITY_DENSE),
        torch.tensor(entity_pool, dtype=torch.long),
        torch.tensor(entity_sample, dtype=torch.long), *es,
        torch.tensor(targets, dtype=torch.float32),
        torch.tensor(weights, dtype=torch.float32),
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Train the structured integer V3 turn-end evaluator")
    parser.add_argument("dataset", type=Path); parser.add_argument("output", type=Path)
    parser.add_argument("--card-table", type=Path, default=ROOT / "EN_Card_Data.csv")
    parser.add_argument("--manifest", type=Path); parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--qat-epochs", type=int, default=6); parser.add_argument("--max-samples", type=int, default=0)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--seed", type=int, default=20260714); parser.add_argument("--legacy-predictions", type=Path)
    parser.add_argument("--report", type=Path)
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
            # Dense fields contain raw rule values (HP, turn counters, and card
            # counts), so generic Xavier initialization saturates clipped ReLU
            # before the first update.  Start in the linear range instead.
            torch.nn.init.uniform_(self.edw, -.002, .002)
            torch.nn.init.uniform_(self.gdw, -.002, .002)
            torch.nn.init.uniform_(self.pool, -.01, .01)
            torch.nn.init.uniform_(self.ow, -.01, .01)

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

        def forward(self, batch: PackedBatch, qat=False):
            edw, esw, eb, gdw, gsw, pool, gb, ow, ob = self.parameters_for(qat)
            global_acc = batch.global_dense @ gdw.T + gb
            if batch.global_sparse_relation.numel():
                contribution = gsw[batch.global_sparse_relation, batch.global_sparse_token]
                contribution = contribution * batch.global_sparse_value[:, None]
                global_acc = global_acc.index_add(0, batch.global_sparse_sample, contribution)
            if batch.entity_dense.shape[0]:
                entity_acc = batch.entity_dense @ edw.T + eb
                if batch.entity_sparse_relation.numel():
                    contribution = esw[batch.entity_sparse_relation, batch.entity_sparse_token]
                    contribution = contribution * batch.entity_sparse_value[:, None]
                    entity_acc = entity_acc.index_add(0, batch.entity_sparse_entity, contribution)
                activation = torch.clamp(entity_acc, 0, 127)
                projection = torch.bmm(activation[:, None, :], pool[batch.entity_pool]).squeeze(1)
                if qat:
                    # Native inference divides the int64 entity projection by
                    # WEIGHT_SCALE with half-away-from-zero rounding before it
                    # enters the global accumulator.
                    projection = fake_round_away(projection, WEIGHT_SCALE)
                global_acc = global_acc.index_add(0, batch.entity_sample, projection)
            return torch.clamp(global_acc, 0, 127) @ ow + ob

    model = EntityNnue(); optimizer = torch.optim.AdamW(model.parameters(), lr=5e-4, weight_decay=1e-6)
    if args.batch_size < 1:
        raise ValueError("batch size must be positive")
    train = np.asarray(splits["train"], dtype=np.int64); rng = np.random.default_rng(args.seed)
    training_batches = [pack_batch(examples, train[start:start+args.batch_size], token_index, torch)
                        for start in range(0, len(train), args.batch_size)]
    validation_batches = [pack_batch(examples, splits["validation"][start:start+args.batch_size],
                                     token_index, torch)
                          for start in range(0, len(splits["validation"]), args.batch_size)]
    best_validation = math.inf
    best_state = None
    best_epoch = 0
    for epoch in range(args.epochs):
        qat = epoch >= max(0, args.epochs - args.qat_epochs)
        total = total_weight = 0.0
        for batch_index in rng.permutation(len(training_batches)):
            batch = training_batches[int(batch_index)]
            prediction = model(batch, qat)
            loss = ((prediction-batch.target).square()*batch.weight).sum()/batch.weight.sum()
            optimizer.zero_grad(set_to_none=True); loss.backward(); optimizer.step()
            total += float(loss.detach()*batch.weight.sum()); total_weight += float(batch.weight.sum())
        validation_total = validation_weight = 0.0
        with torch.no_grad():
            for batch in validation_batches:
                loss_sum = (model(batch, qat)-batch.target).square().mul(batch.weight).sum()
                validation_total += float(loss_sum)
                validation_weight += float(batch.weight.sum())
        validation_mse = validation_total / validation_weight if validation_weight else math.nan
        print(f"epoch={epoch+1} qat={int(qat)} train_mse={total/total_weight:.7f} "
              f"validation_mse={validation_mse:.7f}")
        if (qat or args.qat_epochs == 0) and validation_mse < best_validation:
            best_validation = validation_mse
            best_epoch = epoch + 1
            best_state = copy.deepcopy(model.state_dict())

    if best_state is not None:
        model.load_state_dict(best_state)

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
            checked = examples[:min(256,len(examples))]
            references = predict_integer_many(quantized, [example.feature for example in checked])
            native_bit_exact = all(exact_evaluate_features_v3(list(e.feature.global_dense),
                [list(x) for x in e.feature.global_sparse],
                [{"pool": x.pool, "dense": list(x.dense), "sparse": [list(y) for y in x.sparse]}
                 for x in e.feature.entities]) == reference
                for e, reference in zip(checked, references))
        finally: exact_unload_evaluator_model()
    except Exception as error: print(f"native bit check failed: {error}", file=sys.stderr)

    def predictions(indices):
        floating_parts = []
        qat_reference_parts = []
        with torch.no_grad():
            for start in range(0, len(indices), args.batch_size):
                batch = pack_batch(examples, indices[start:start+args.batch_size], token_index, torch)
                floating_parts.append(model(batch, False).numpy())
                qat_reference_parts.append(model(batch, args.qat_epochs > 0).numpy())
        floating = np.concatenate(floating_parts) if floating_parts else np.empty(0)
        qat_reference = np.concatenate(qat_reference_parts) if qat_reference_parts else np.empty(0)
        integer = np.asarray(predict_integer_many(quantized, [examples[i].feature for i in indices]),
                             dtype=np.float64) / 100_000_000
        target = np.asarray([examples[i].target for i in indices])
        return target, floating, qat_reference, integer

    report = {"examples": len(examples), "splits": {k:len(v) for k,v in splits.items()},
              "bestEpoch": best_epoch, "bestValidationMse": best_validation}
    gates = {"nativeBitExact": native_bit_exact, "hasUnseenTestSplit": bool(splits["test"])}
    if splits["test"]:
        target, floating, qat_reference, integer = predictions(splits["test"])
        test_weight = np.asarray([examples[i].weight for i in splits["test"]], dtype=np.float64)
        weighted = lambda values: float(np.sum(values * test_weight) / np.sum(test_weight))
        zero_mse = weighted(target**2)
        float_mse = weighted((floating-target)**2)
        qat_reference_mse = weighted((qat_reference-target)**2)
        integer_mse = weighted((integer-target)**2)
        gates.update(zeroImprovement15=integer_mse <= zero_mse*.85,
                     signAccuracy70=weighted(np.sign(integer)==np.sign(target)) >= .70,
                     quantizationWithin1Percent=abs(integer_mse-qat_reference_mse)
                     / max(qat_reference_mse,1e-12) <= .01)
        report.update(testFloatMse=float_mse, testQatReferenceMse=qat_reference_mse,
                      testQuantizedMse=integer_mse,zeroMse=zero_mse,
                      signAccuracy=weighted(np.sign(integer)==np.sign(target)))
        baseline_by_key = json.loads(args.legacy_predictions.read_text()) if args.legacy_predictions else None
        report["pairedBaseline"] = "legacy-predictions" if baseline_by_key is not None else "constant-zero"
        by_replay=defaultdict(list)
        for prediction,index in zip(integer,splits["test"]):
            e=examples[index]
            baseline = float(baseline_by_key[e.key]) if baseline_by_key is not None else 0.0
            by_replay[e.replay_id].append((prediction-e.target)**2-(baseline-e.target)**2)
        paired=np.asarray([np.mean(v) for v in by_replay.values()])
        bootstrap=rng.choice(paired,(2000,len(paired)),replace=True).mean(1)
        gates["beatsBaselinePaired95"] = float(np.quantile(bootstrap,.975)) < 0
    report["gates"] = gates; report["allGatesPassed"] = bool(gates) and all(gates.values())
    print(json.dumps(report,indent=2))
    report_path = args.report or args.output.with_suffix(args.output.suffix + ".report.json")
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    if args.require_gates and not report["allGatesPassed"]: raise SystemExit("V3 adoption gates failed")


if __name__ == "__main__": main()
