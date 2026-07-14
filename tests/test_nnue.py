from pathlib import Path

import pytest

from exact_solver.nnue import INPUTS, extract_features, load_quantized


def _player(**values):
    base = {"active": [], "bench": [], "discard": [], "hand": [], "handCount": 0,
            "deckCount": 40, "prize": [None] * 6, "benchMax": 5,
            "poisoned": False, "burned": False, "paralyzed": False,
            "asleep": False, "confused": False}
    base.update(values)
    return base


def test_features_are_actor_relative_and_ignore_serials():
    card_a = {"id": 42, "serial": 1}
    card_b = {"id": 42, "serial": 999}
    current_a = {"yourIndex": 0, "players": [_player(hand=[card_a], handCount=1), _player()],
                 "turn": 3, "turnActionCount": 4}
    current_b = {"yourIndex": 0, "players": [_player(hand=[card_b], handCount=1), _player()],
                 "turn": 3, "turnActionCount": 4}
    assert extract_features(current_a) == extract_features(current_b)
    assert len(extract_features(current_a)) == INPUTS


def test_committed_quantized_model_has_expected_shape():
    model_path = Path(__file__).parents[1] / "sample_submission" / "sample_submission" / "exact-evaluator.bin"
    if not model_path.exists():
        pytest.skip("deck-specific trained model not present on this branch")
    w1, b1, w2, _ = load_quantized(model_path)
    assert w1.shape == (8, 48)
    assert b1.shape == (8,)
    assert w2.shape == (8,)


def test_native_loader_accepts_model_and_can_detach_it():
    from cg.api import exact_load_evaluator_model, exact_unload_evaluator_model
    model_path = Path(__file__).parents[1] / "sample_submission" / "sample_submission" / "exact-evaluator.bin"
    try:
        assert exact_load_evaluator_model(str(model_path))["loaded"] is True
    finally:
        exact_unload_evaluator_model()
