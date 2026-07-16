import json
import hashlib
import math
from pathlib import Path

import numpy as np

from exact_solver.nnue_v3 import (ENTITY_DENSE, ENTITY_HIDDEN,
                                  ENTITY_RELATIONS, GLOBAL_DENSE,
                                  GLOBAL_HIDDEN, GLOBAL_RELATIONS, POOLS,
                                  EntityFeatures, FeatureRecord, QuantizedModel,
                                  export_quantized, load_quantized,
                                  predict_integer, predict_integer_many)


def _model(tokens=(0, 1, 2, 42, 1_000_007, 2_000_001, 3_000_042)) -> QuantizedModel:
    rng = np.random.default_rng(20260714); count = len(tokens)
    return QuantizedModel(
        np.asarray(tokens, dtype=np.int32),
        rng.integers(-8, 9, (ENTITY_HIDDEN, ENTITY_DENSE), dtype=np.int16),
        rng.integers(-8, 9, (ENTITY_RELATIONS, count, ENTITY_HIDDEN), dtype=np.int16),
        rng.integers(10_000, 20_001, ENTITY_HIDDEN, dtype=np.int32),
        rng.integers(-8, 9, (GLOBAL_HIDDEN, GLOBAL_DENSE), dtype=np.int16),
        rng.integers(-8, 9, (GLOBAL_RELATIONS, count, GLOBAL_HIDDEN), dtype=np.int16),
        rng.integers(-8, 9, (POOLS, ENTITY_HIDDEN, GLOBAL_HIDDEN), dtype=np.int16),
        rng.integers(10_000, 20_001, GLOBAL_HIDDEN, dtype=np.int32),
        rng.integers(-8, 9, GLOBAL_HIDDEN, dtype=np.int16), 123456,
    )


def _record(order=(1, 2)) -> FeatureRecord:
    entities = [EntityFeatures(2, [token] + [0] * (ENTITY_DENSE - 1),
                               [[0, token, 256], [5, 42, 256]]) for token in order]
    return FeatureRecord([0] * GLOBAL_DENSE, [[0, 1, 256], [7, 42, 128]], entities)


def test_v3_round_trip_has_explicit_collision_free_token_table(tmp_path: Path):
    path = tmp_path / "evaluator-v3.bin"; export_quantized(path, _model())
    loaded = load_quantized(path)
    assert loaded.tokens.tolist() == [0, 1, 2, 42, 1_000_007, 2_000_001, 3_000_042]
    assert predict_integer(loaded, _record()) == predict_integer(_model(), _record())


def test_bundled_trained_model_is_v3_and_below_memory_gate():
    path = Path(__file__).resolve().parents[1] / "sample_submission" / "sample_submission" / "exact-evaluator-v3.bin"
    model = load_quantized(path)
    assert path.stat().st_size < 64 * 1024 * 1024
    assert model.tokens[0] == 0
    assert np.count_nonzero(model.entity_sparse_weight) > 0
    assert np.count_nonzero(model.global_sparse_weight) > 0
    assert model.dataset_hash != bytes(32)
    report = json.loads(path.with_suffix(".report.json").read_text(encoding="utf-8"))
    assert hashlib.sha256(path.read_bytes()).hexdigest() == report["modelSha256"]
    assert report["allGatesPassed"] is True


def test_shared_entity_encoder_is_invariant_to_bench_order():
    model = _model()
    assert predict_integer(model, _record((1, 2))) == predict_integer(model, _record((2, 1)))


def test_batched_integer_reference_matches_single_record_evaluation():
    model = _model()
    records = [_record((1, 2)), _record((2, 1))]
    assert predict_integer_many(model, records) == [predict_integer(model, record) for record in records]


def test_python_integer_reference_matches_native_bit_for_bit(tmp_path: Path):
    from cg.api import (exact_evaluate_features_v3, exact_load_evaluator_model,
                        exact_unload_evaluator_model)
    path = tmp_path / "evaluator-v3.bin"; model = _model(); export_quantized(path, model)
    info = exact_load_evaluator_model(str(path))
    try:
        assert info["schemaVersion"] == 3 and info["informationSetSafe"] is True
        assert info["residentBytes"] < 64 * 1024 * 1024
        feature = _record()
        entities = [{"pool": entity.pool, "dense": list(entity.dense),
                     "sparse": [list(item) for item in entity.sparse]} for entity in feature.entities]
        assert exact_evaluate_features_v3(list(feature.global_dense),
                                          [list(item) for item in feature.global_sparse], entities) \
            == predict_integer(model, feature)
    finally:
        exact_unload_evaluator_model()


def test_native_scalar_and_avx2_dispatch_are_bit_exact(tmp_path: Path, monkeypatch):
    from cg.api import (exact_evaluate_features_v3, exact_load_evaluator_model,
                        exact_unload_evaluator_model)
    path = tmp_path / "evaluator-v3.bin"; model = _model(); export_quantized(path, model)
    records = []
    for index in range(64):
        dense = [((index + column * 3) % 11) - 5 for column in range(GLOBAL_DENSE)]
        sparse = [[index % GLOBAL_RELATIONS, 1 + index % 2, 32 + index * 3],
                  [(index + 7) % GLOBAL_RELATIONS, 42, 256]]
        entity = EntityFeatures(index % POOLS,
            [((index * 5 + column) % 9) - 4 for column in range(ENTITY_DENSE)],
            [[index % ENTITY_RELATIONS, 1 + index % 2, 64], [5, 42, 256]])
        records.append(FeatureRecord(dense, sparse, [entity]))

    def native(mode):
        monkeypatch.setenv("PTCG_EVALUATOR_SIMD", mode)
        exact_load_evaluator_model(str(path))
        try:
            return [exact_evaluate_features_v3(
                list(record.global_dense), [list(item) for item in record.global_sparse],
                [{"pool": entity.pool, "dense": list(entity.dense),
                  "sparse": [list(item) for item in entity.sparse]}
                 for entity in record.entities]) for record in records]
        finally:
            exact_unload_evaluator_model()

    scalar = native("scalar")
    avx2 = native("avx2")
    assert scalar == avx2
    assert scalar == [predict_integer(model, record) for record in records]


def test_entity_attachment_relation_changes_value():
    model = _model()
    first = _record()
    changed = FeatureRecord(first.global_dense, first.global_sparse,
        [EntityFeatures(first.entities[0].pool, first.entities[0].dense, [[6, 42, 256]]), first.entities[1]])
    assert predict_integer(model, first) != predict_integer(model, changed)


def test_exact_weight_promotes_and_divides_without_loss():
    from cg.sim import lib
    diagnostics = json.loads(lib.ExactArithmeticDiagnostics().decode())
    expected = math.comb(60, 6) * math.comb(54, 20)
    assert diagnostics["product"] == str(expected)
    assert diagnostics["quotient"] == str(math.comb(54, 20))
    assert diagnostics["remainder"] == "0"
    assert diagnostics["hashPairMatchesScalar"] is True


def test_native_entity_and_hidden_information_invariants():
    from cg.sim import lib
    diagnostics = json.loads(lib.ExactEvaluatorV3Diagnostics().decode())
    assert diagnostics == {
        "initialized": True,
        "benchOrderInvariant": True,
        "attachmentSensitive": True,
        "opponentHiddenInvariant": True,
        "typedEffectSensitive": True,
    }
