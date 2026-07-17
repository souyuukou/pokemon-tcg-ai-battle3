"""V4 evaluator: Passive residual on top of a V3 semantic trunk."""
from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

import numpy as np

from exact_solver import nnue_v3

MAGIC = b"PTCGEV4\0"
MODEL_SCHEMA = FEATURE_SCHEMA = LIVENESS_SCHEMA = 1
CONTEXT_HIDDEN = 32
PASSIVE_CONTEXT_SCALE = 4096
HEADER = struct.Struct("<8s7IQ32s")


@dataclass(frozen=True)
class QuantizedModelV4:
    tokens: np.ndarray
    passive_bias: np.ndarray  # [T] int32, token-index aligned
    passive_context_weight: np.ndarray  # [T, CONTEXT] int16
    context_from_global: np.ndarray  # [CONTEXT, GH] int16
    context_bias: np.ndarray  # [CONTEXT] int32
    pairs: np.ndarray  # structured (card_a u2, card_b u2, weight i4)
    v3: nnue_v3.QuantizedModel
    checksum: int = 0

    def validate(self) -> None:
        self.v3.validate()
        t = len(self.tokens)
        if self.passive_bias.shape != (t,):
            raise ValueError("passive_bias shape")
        if self.passive_context_weight.shape != (t, CONTEXT_HIDDEN):
            raise ValueError("passive_context_weight shape")
        if self.context_from_global.shape != (CONTEXT_HIDDEN, nnue_v3.GLOBAL_HIDDEN):
            raise ValueError("context_from_global shape")
        if self.context_bias.shape != (CONTEXT_HIDDEN,):
            raise ValueError("context_bias shape")


def own_hand_linear_score(model: nnue_v3.QuantizedModel, token_index: int) -> int:
    """First-order OwnHand contribution (matches C++ estimateOwnHandLinearScore)."""
    gsw = model.global_sparse_weight[0, token_index].astype(np.int64)
    ow = model.output_weight.astype(np.int64)
    delta = gsw * nnue_v3.BELIEF_SCALE
    act = np.clip(delta, 0, 127 * nnue_v3.WEIGHT_SCALE)
    output = int(np.dot(ow, act))
    divisor = nnue_v3.WEIGHT_SCALE * nnue_v3.WEIGHT_SCALE
    magnitude = abs(output)
    score = (magnitude // divisor) * nnue_v3.SCORE_SCALE + (
        (magnitude % divisor) * nnue_v3.SCORE_SCALE + divisor // 2
    ) // divisor
    if output < 0:
        score = -score
    return max(-nnue_v3.NON_TERMINAL_LIMIT, min(nnue_v3.NON_TERMINAL_LIMIT, score))


def bootstrap_from_v3(v3: nnue_v3.QuantizedModel) -> QuantizedModelV4:
    tokens = np.asarray(v3.tokens, dtype=np.int32)
    bias = np.zeros(len(tokens), dtype=np.int32)
    for i, token in enumerate(tokens):
        tid = int(token)
        if 0 < tid < 1_000_000:  # card ids below AttackTokenBase
            bias[i] = np.int32(own_hand_linear_score(v3, i))
    return QuantizedModelV4(
        tokens=tokens,
        passive_bias=bias,
        passive_context_weight=np.zeros((len(tokens), CONTEXT_HIDDEN), dtype=np.int16),
        context_from_global=np.zeros((CONTEXT_HIDDEN, nnue_v3.GLOBAL_HIDDEN), dtype=np.int16),
        context_bias=np.zeros(CONTEXT_HIDDEN, dtype=np.int32),
        pairs=np.zeros(0, dtype=[("card_a", "<u2"), ("card_b", "<u2"), ("weight", "<i4")]),
        v3=v3,
        checksum=nnue_v3.fnv1a(tokens.tobytes()) ^ 0x56345F4254,
    )


def export_quantized(path: str | Path, model: QuantizedModelV4) -> None:
    model.validate()
    tokens = np.asarray(model.tokens, dtype="<i4")
    header = HEADER.pack(
        MAGIC,
        MODEL_SCHEMA,
        FEATURE_SCHEMA,
        LIVENESS_SCHEMA,
        CONTEXT_HIDDEN,
        PASSIVE_CONTEXT_SCALE,
        len(tokens),
        len(model.pairs),
        int(model.checksum),
        bytes(32),
    )
    payload = [
        tokens.tobytes(order="C"),
        np.asarray(model.passive_bias, dtype="<i4").tobytes(order="C"),
        np.asarray(model.passive_context_weight, dtype="<i2").tobytes(order="C"),
        np.asarray(model.context_from_global, dtype="<i2").tobytes(order="C"),
        np.asarray(model.context_bias, dtype="<i4").tobytes(order="C"),
        np.asarray(model.pairs).tobytes(order="C") if len(model.pairs) else b"",
    ]
    Path(path).write_bytes(header + b"".join(payload))


def predict_integer_v4(
    model: QuantizedModelV4,
    feature: nnue_v3.FeatureRecord,
    passive_counts: Sequence[tuple[int, int]] | None = None,
) -> int:
    """Semantic = V3(feature); Passive = sum n_i * bias[i] (+ pairs)."""
    semantic = nnue_v3.predict_integer(model.v3, feature)
    if not passive_counts:
        return semantic
    index = {int(t): i for i, t in enumerate(model.tokens)}
    value = semantic
    counts = {int(cid): int(n) for cid, n in passive_counts}
    for cid, n in counts.items():
        value += n * int(model.passive_bias[index.get(cid, 0)])
    for pair in model.pairs:
        a, b, w = int(pair["card_a"]), int(pair["card_b"]), int(pair["weight"])
        na, nb = counts.get(a, 0), counts.get(b, 0)
        if a == b:
            value += w * (na * (na - 1) // 2)
        else:
            value += w * na * nb
    return max(-nnue_v3.NON_TERMINAL_LIMIT, min(nnue_v3.NON_TERMINAL_LIMIT, value))
