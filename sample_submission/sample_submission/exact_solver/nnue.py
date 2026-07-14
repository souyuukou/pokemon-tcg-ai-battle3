from __future__ import annotations

import json
import struct
from pathlib import Path
from typing import Iterable

INPUTS = 48
HIDDEN = 8
WEIGHT_SCALE = 4096
FEATURE_SCALE = 32
SCORE_SCALE = 10_000_000
MAGIC = b"PTCGEV1\0"


def _clip(value: int) -> int:
    return max(-127, min(127, int(value)))


def _bucket(card_id: int, zone: int) -> int:
    value = ((int(card_id) & 0xFFFFFFFF) * 0x9E3779B1 + zone * 0x85EBCA6B) & 0xFFFFFFFF
    value ^= value >> 16
    value = (value * 0x7FEB352D) & 0xFFFFFFFF
    value ^= value >> 15
    return value % 24


def extract_features(current: dict, actor: int | None = None) -> list[int]:
    """Feature extraction shared conceptually with ExactCpuEvaluator.h.

    Only observation-visible data is used. Hidden card identities therefore
    cannot leak into a learned policy through the leaf evaluator.
    """
    actor = int(current.get("yourIndex", 0) if actor is None else actor)
    players = current["players"]
    me, opp = players[actor], players[1 - actor]
    x = [0] * INPUTS

    def cards(player: dict, name: str) -> list[dict]:
        return [card for card in (player.get(name) or []) if card]

    def count(player: dict, name: str) -> int:
        if name == "hand":
            return int(player.get("handCount", len(player.get("hand") or [])))
        if name == "deck":
            return int(player.get("deckCount", len(player.get("deck") or [])))
        return len(player.get(name) or [])

    def delta(a: int, b: int, scale: int = 1) -> int:
        return _clip((a - b) * scale)

    def energy(player: dict) -> int:
        return sum(len(p.get("energyCards") or []) for p in cards(player, "active") + cards(player, "bench"))

    def damage(player: dict) -> int:
        return sum(max(0, int(p.get("maxHp", 0)) - int(p.get("hp", 0)))
                   for p in cards(player, "active") + cards(player, "bench"))

    x[0] = delta(count(opp, "prize"), count(me, "prize"), 16)
    x[1] = delta(count(me, "hand"), count(opp, "hand"), 4)
    x[2] = delta(count(me, "deck"), count(opp, "deck"), 2)
    x[3] = delta(count(me, "active"), count(opp, "active"), 24)
    x[4] = delta(count(me, "bench"), count(opp, "bench"), 12)
    x[5] = delta(energy(me), energy(opp), 6)
    x[6] = _clip((damage(opp) - damage(me)) // 10)
    x[7] = delta(count(opp, "discard"), count(me, "discard"), 2)
    x[8] = -16 if current.get("supporterPlayed") else 16
    x[9] = -12 if current.get("energyAttached") else 12
    x[10] = -8 if current.get("retreated") else 8
    x[11] = _clip(current.get("turn", 0))
    x[12] = _clip(current.get("turnActionCount", 0) // 4)
    x[13] = delta(int(me.get("benchMax", 5)), int(opp.get("benchMax", 5)), 8)
    x[14] = delta(count(me, "active") + count(me, "bench"),
                  count(opp, "active") + count(opp, "bench"), 8)
    x[15] = _clip(max(0, 5 - count(me, "deck")) * -16)
    x[16] = _clip(max(0, 5 - count(opp, "deck")) * 16)
    x[17] = (-12 if me.get("poisoned") else 0) + (12 if opp.get("poisoned") else 0)
    x[18] = (-10 if me.get("burned") else 0) + (10 if opp.get("burned") else 0)
    x[19] = (-16 if me.get("paralyzed") else 0) + (16 if opp.get("paralyzed") else 0)
    x[20] = (-10 if me.get("asleep") else 0) + (10 if opp.get("asleep") else 0)
    x[21] = (-8 if me.get("confused") else 0) + (8 if opp.get("confused") else 0)
    x[22] = 0
    x[23] = 1
    for player, sign in ((me, 1), (opp, -1)):
        for name, zone in (("hand", 1), ("active", 2), ("bench", 3), ("discard", 4)):
            for card in cards(player, name):
                card_id = int(card.get("id", 0))
                if card_id:
                    index = 24 + _bucket(card_id, zone)
                    x[index] = _clip(x[index] + sign * 8)
    return x


def iter_replay_examples(paths: Iterable[Path], stride: int = 1):
    """Yield (features, outcome) without retaining replay JSON in memory."""
    for path in paths:
        try:
            replay = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        rewards = replay.get("rewards") or [0, 0]
        for step_number, step in enumerate(replay.get("steps") or []):
            if step_number % max(1, stride):
                continue
            for actor, record in enumerate(step[:2]):
                if (record or {}).get("status") != "ACTIVE":
                    continue
                observation = (record or {}).get("observation") or {}
                current = observation.get("current")
                if current and int(current.get("result", -1)) < 0:
                    yield extract_features(current, actor), float(rewards[actor])


def export_quantized(path: str | Path, model) -> None:
    """Export a torch 48x8x1 model to the dependency-free native format."""
    import numpy as np

    first, second = model[0], model[2]
    w1 = np.rint(first.weight.detach().cpu().numpy() * WEIGHT_SCALE / FEATURE_SCALE).clip(-32768, 32767).astype("<i2")
    b1 = np.rint(first.bias.detach().cpu().numpy() * WEIGHT_SCALE).clip(-(2**31), 2**31 - 1).astype("<i4")
    w2 = np.rint(second.weight.detach().cpu().numpy().reshape(-1) * WEIGHT_SCALE).clip(-32768, 32767).astype("<i2")
    b2 = int(round(float(second.bias.detach().cpu()[0]) * WEIGHT_SCALE * WEIGHT_SCALE))
    header = struct.pack("<8sIIIII", MAGIC, 1, INPUTS, HIDDEN, WEIGHT_SCALE, SCORE_SCALE)
    Path(path).write_bytes(header + w1.tobytes() + b1.tobytes() + w2.tobytes() + struct.pack("<q", b2))


def load_quantized(path: str | Path):
    """Load model arrays for tests and offline inspection."""
    import numpy as np

    raw = Path(path).read_bytes()
    header_size = struct.calcsize("<8sIIIII")
    magic, version, inputs, hidden, weight_scale, score_scale = struct.unpack_from("<8sIIIII", raw)
    if (magic, version, inputs, hidden, weight_scale, score_scale) != (
            MAGIC, 1, INPUTS, HIDDEN, WEIGHT_SCALE, SCORE_SCALE):
        raise ValueError("invalid exact evaluator model")
    offset = header_size
    w1 = np.frombuffer(raw, dtype="<i2", count=INPUTS * HIDDEN, offset=offset).reshape(HIDDEN, INPUTS)
    offset += INPUTS * HIDDEN * 2
    b1 = np.frombuffer(raw, dtype="<i4", count=HIDDEN, offset=offset)
    offset += HIDDEN * 4
    w2 = np.frombuffer(raw, dtype="<i2", count=HIDDEN, offset=offset)
    offset += HIDDEN * 2
    b2, = struct.unpack_from("<q", raw, offset)
    offset += 8
    if offset != len(raw):
        raise ValueError("invalid exact evaluator model length")
    return w1, b1, w2, b2
