import json
import math
from pathlib import Path

import numpy as np

from exact_solver.nnue_v2 import (DENSE, HIDDEN, RELATIONS, QuantizedModel,
                                  export_quantized, load_quantized,
                                  predict_integer)


def _model(card_ids=(0, 1, 2, 42)) -> QuantizedModel:
    rng = np.random.default_rng(20260714)
    return QuantizedModel(
        card_ids=np.asarray(card_ids, dtype=np.int32),
        dense_weight=rng.integers(-100, 101, (HIDDEN, DENSE), dtype=np.int16),
        sparse_weight=rng.integers(-100, 101, (RELATIONS, len(card_ids), HIDDEN), dtype=np.int16),
        hidden_bias=rng.integers(10_000, 20_001, HIDDEN, dtype=np.int32),
        output_weight=rng.integers(-100, 101, HIDDEN, dtype=np.int16),
        output_bias=123456,
        dataset_hash=bytes(range(32)),
    )


def test_v2_round_trip_has_explicit_collision_free_card_table(tmp_path: Path):
    path = tmp_path / "evaluator-v2.bin"
    export_quantized(path, _model())
    loaded = load_quantized(path)
    assert loaded.card_ids.tolist() == [0, 1, 2, 42]
    assert loaded.sparse_weight.shape == (RELATIONS, 4, HIDDEN)
    dense = [0] * DENSE
    assert predict_integer(loaded, dense, [(0, 1, 256)]) != predict_integer(loaded, dense, [(0, 2, 256)])


def test_python_integer_reference_matches_native_bit_for_bit(tmp_path: Path):
    from cg.api import (exact_evaluate_features_v2, exact_load_evaluator_model,
                        exact_unload_evaluator_model)

    path = tmp_path / "evaluator-v2.bin"
    model = _model(); export_quantized(path, model)
    info = exact_load_evaluator_model(str(path))
    try:
        assert info["schemaVersion"] == 2
        assert info["informationSetSafe"] is True
        rng = np.random.default_rng(91)
        for _ in range(20):
            dense = rng.integers(-127, 128, DENSE).tolist()
            sparse = [[int(rng.integers(0, RELATIONS)), int(rng.choice([1, 2, 42, 9999])),
                       int(rng.integers(-1024, 1025))] for _ in range(12)]
            assert exact_evaluate_features_v2(dense, sparse) == predict_integer(model, dense, sparse)
    finally:
        exact_unload_evaluator_model()


def test_exact_weight_promotes_and_divides_without_loss():
    from cg.sim import lib
    diagnostics = json.loads(lib.ExactArithmeticDiagnostics().decode())
    expected = math.comb(60, 6) * math.comb(54, 20)
    assert diagnostics == {
        "product": str(expected),
        "quotient": str(math.comb(54, 20)),
        "remainder": "0",
        "gcd": str(math.comb(60, 6)),
        "bits": expected.bit_length(),
        "promoted": True,
    }
