"""Python V4 bootstrap / integer residual checks."""
from __future__ import annotations

from pathlib import Path

from exact_solver.nnue_v3 import FeatureRecord, load_quantized, predict_integer
from exact_solver.nnue_v4 import bootstrap_from_v3, export_quantized, predict_integer_v4


ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "sample_submission" / "sample_submission" / "exact-evaluator-v3.bin"


def test_bootstrap_passive_bias_nonzero_for_cards():
    v3 = load_quantized(MODEL)
    v4 = bootstrap_from_v3(v3)
    assert len(v4.passive_bias) == len(v3.tokens)
    assert int((v4.passive_bias != 0).sum()) > 10


def test_v4_predict_matches_v3_when_passive_empty(tmp_path):
    v3 = load_quantized(MODEL)
    v4 = bootstrap_from_v3(v3)
    export_quantized(tmp_path / "exact-evaluator-v4.bin", v4)
    feature = FeatureRecord(
        global_dense=[0] * 16,
        global_sparse=[],
        entities=[],
    )
    assert predict_integer_v4(v4, feature, []) == predict_integer(v3, feature)


def test_v4_passive_difference_is_linear():
    v3 = load_quantized(MODEL)
    v4 = bootstrap_from_v3(v3)
    feature = FeatureRecord(global_dense=[0] * 16, global_sparse=[], entities=[])
    base = predict_integer_v4(v4, feature, [])
    # pick first nonzero card bias
    card = next(int(t) for i, t in enumerate(v4.tokens) if 0 < int(t) < 1_000_000 and v4.passive_bias[i] != 0)
    idx = list(map(int, v4.tokens)).index(card)
    one = predict_integer_v4(v4, feature, [(card, 1)])
    two = predict_integer_v4(v4, feature, [(card, 2)])
    assert one - base == int(v4.passive_bias[idx])
    assert two - base == 2 * int(v4.passive_bias[idx])
