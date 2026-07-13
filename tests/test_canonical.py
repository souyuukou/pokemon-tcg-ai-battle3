from dataclasses import dataclass
from exact_solver.canonical import KeyArena, canonical_bytes, observation_key, siphash24
from exact_solver.transposition import TTEntry, TranspositionTable
from fractions import Fraction


def test_reference_siphash_vector():
    assert siphash24(b"", bytes(range(16))) == 0x726FDB47DD0E0E31


def test_mapping_order_is_irrelevant_but_fields_are_not():
    assert canonical_bytes({"a": 1, "b": 2}) == canonical_bytes({"b": 2, "a": 1})
    assert canonical_bytes({"a": 1}) != canonical_bytes({"a": 2})


def test_observation_excludes_logs_and_alpha_renames_serials():
    ns = {"rules": 1}
    a = {"logs": [1], "current": {"hand": [{"id": 5, "serial": 91}]}}
    b = {"logs": [999], "current": {"hand": [{"id": 5, "serial": 7}]}}
    assert observation_key(a, ns) == observation_key(b, ns)


def test_forced_digest_collision_still_compares_full_key():
    arena = KeyArena(hash_fn=lambda _: (0, 0))
    ka, kb = arena.intern(b"a"), arena.intern(b"b")
    assert not arena.equal(ka, kb)
    tt = TranspositionTable(arena)
    tt.put(TTEntry(ka, "leaf", Fraction(1), Fraction(1)))
    assert tt.get(kb, "leaf") is None

