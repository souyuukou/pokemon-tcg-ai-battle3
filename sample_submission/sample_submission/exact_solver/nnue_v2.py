from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

import numpy as np

MAGIC = b"PTCGEV2\0"
VERSION = 2
DENSE = 40
HIDDEN = 32
RELATIONS = 30
WEIGHT_SCALE = 4096
BELIEF_SCALE = 256
SCORE_SCALE = 100_000_000
NON_TERMINAL_LIMIT = 90_000_000
SCHEMA_VERSION = 2
HEADER = struct.Struct("<8s9IQ32s")


def _fnv1a(data: bytes) -> int:
    value = 1469598103934665603
    for byte in data:
        value ^= byte
        value = (value * 1099511628211) & ((1 << 64) - 1)
    return value


@dataclass(frozen=True)
class QuantizedModel:
    card_ids: np.ndarray
    dense_weight: np.ndarray
    sparse_weight: np.ndarray
    hidden_bias: np.ndarray
    output_weight: np.ndarray
    output_bias: int
    dataset_hash: bytes

    def validate(self) -> None:
        ids = np.asarray(self.card_ids, dtype=np.int32)
        if ids.ndim != 1 or not len(ids) or ids[0] != 0 or np.any(ids[1:] <= ids[:-1]):
            raise ValueError("card IDs must be unique, sorted, and start with UNK=0")
        if np.asarray(self.dense_weight).shape != (HIDDEN, DENSE):
            raise ValueError("invalid dense weight shape")
        if np.asarray(self.sparse_weight).shape != (RELATIONS, len(ids), HIDDEN):
            raise ValueError("invalid sparse weight shape")
        if np.asarray(self.hidden_bias).shape != (HIDDEN,):
            raise ValueError("invalid hidden bias shape")
        if np.asarray(self.output_weight).shape != (HIDDEN,):
            raise ValueError("invalid output weight shape")
        if len(self.dataset_hash) != 32:
            raise ValueError("dataset hash must contain 32 bytes")


def export_quantized(path: str | Path, model: QuantizedModel) -> None:
    model.validate()
    ids = np.asarray(model.card_ids, dtype="<i4")
    dense = np.asarray(model.dense_weight, dtype="<i2")
    sparse = np.asarray(model.sparse_weight, dtype="<i2")
    bias = np.asarray(model.hidden_bias, dtype="<i4")
    output = np.asarray(model.output_weight, dtype="<i2")
    card_bytes = ids.tobytes(order="C")
    header = HEADER.pack(
        MAGIC, VERSION, DENSE, HIDDEN, RELATIONS, len(ids), WEIGHT_SCALE,
        BELIEF_SCALE, SCORE_SCALE, SCHEMA_VERSION, _fnv1a(card_bytes), model.dataset_hash,
    )
    payload = b"".join((header, card_bytes, dense.tobytes(order="C"),
                        sparse.tobytes(order="C"), bias.tobytes(order="C"),
                        output.tobytes(order="C"), struct.pack("<q", int(model.output_bias))))
    Path(path).write_bytes(payload)


def load_quantized(path: str | Path) -> QuantizedModel:
    raw = Path(path).read_bytes()
    if len(raw) < HEADER.size:
        raise ValueError("invalid V2 evaluator length")
    fields = HEADER.unpack_from(raw)
    (magic, version, dense_count, hidden_count, relation_count, card_count,
     weight_scale, belief_scale, score_scale, schema_version, checksum, dataset_hash) = fields
    expected_header = (MAGIC, VERSION, DENSE, HIDDEN, RELATIONS,
                       WEIGHT_SCALE, BELIEF_SCALE, SCORE_SCALE, SCHEMA_VERSION)
    actual_header = (magic, version, dense_count, hidden_count, relation_count,
                     weight_scale, belief_scale, score_scale, schema_version)
    if actual_header != expected_header or card_count < 1:
        raise ValueError("invalid V2 evaluator header")
    offset = HEADER.size

    def take(dtype: str, count: int) -> np.ndarray:
        nonlocal offset
        item_size = np.dtype(dtype).itemsize
        end = offset + item_size * count
        if end > len(raw):
            raise ValueError("truncated V2 evaluator")
        result = np.frombuffer(raw, dtype=dtype, count=count, offset=offset).copy()
        offset = end
        return result

    ids = take("<i4", card_count)
    if _fnv1a(ids.astype("<i4", copy=False).tobytes()) != checksum:
        raise ValueError("V2 card table checksum mismatch")
    dense = take("<i2", HIDDEN * DENSE).reshape(HIDDEN, DENSE)
    sparse = take("<i2", RELATIONS * card_count * HIDDEN).reshape(RELATIONS, card_count, HIDDEN)
    hidden_bias = take("<i4", HIDDEN)
    output_weight = take("<i2", HIDDEN)
    if offset + 8 != len(raw):
        raise ValueError("invalid V2 evaluator payload length")
    output_bias, = struct.unpack_from("<q", raw, offset)
    model = QuantizedModel(ids, dense, sparse, hidden_bias, output_weight,
                           output_bias, dataset_hash)
    model.validate()
    return model


def predict_integer(model: QuantizedModel, dense: Sequence[int],
                    sparse: Iterable[Sequence[int]]) -> int:
    """Bit-compatible reference for the native allocation-free evaluator."""
    model.validate()
    dense_array = np.asarray(dense, dtype=np.int64)
    if dense_array.shape != (DENSE,):
        raise ValueError(f"dense features must have length {DENSE}")
    accumulator = model.hidden_bias.astype(np.int64)
    minimum, maximum = -(1 << 31), (1 << 31) - 1
    for feature in range(DENSE):
        accumulator = np.clip(
            accumulator + model.dense_weight[:, feature].astype(np.int64) * dense_array[feature],
            minimum, maximum,
        )
    index = {int(card_id): at for at, card_id in enumerate(model.card_ids)}
    for relation, card_id, q8 in sparse:
        relation = int(relation)
        if not 0 <= relation < RELATIONS:
            raise ValueError("invalid sparse relation")
        accumulator = np.clip(
            accumulator + model.sparse_weight[relation, index.get(int(card_id), 0)].astype(np.int64) * int(q8),
            minimum, maximum,
        )
    activation = np.clip(accumulator, 0, 127 * WEIGHT_SCALE)
    output = int(model.output_bias) + int(np.dot(model.output_weight.astype(object), activation.astype(object)))
    divisor = WEIGHT_SCALE * WEIGHT_SCALE
    magnitude = abs(output)
    score = (magnitude // divisor) * SCORE_SCALE
    score += ((magnitude % divisor) * SCORE_SCALE + divisor // 2) // divisor
    if output < 0:
        score = -score
    return max(-NON_TERMINAL_LIMIT, min(NON_TERMINAL_LIMIT, score))


def manifest_digest(path: str | Path | None) -> bytes:
    if path is None:
        return bytes(32)
    return hashlib.sha256(Path(path).read_bytes()).digest()
