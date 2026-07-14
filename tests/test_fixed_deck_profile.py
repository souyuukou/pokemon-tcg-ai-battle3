from collections import Counter

from exact_solver.profile import load_profile
from main import read_deck_csv
from tools.generate_fixed_deck_turn_end_dataset import _play


EXPECTED = Counter({
    5: 2, 13: 1, 19: 4, 66: 2, 140: 1, 305: 3, 343: 1,
    741: 4, 742: 4, 743: 4, 1079: 3, 1081: 4, 1086: 4,
    1097: 1, 1129: 1, 1152: 4, 1182: 3, 1184: 1, 1197: 3,
    1225: 4, 1231: 4, 1266: 2,
})


def test_bundled_profile_is_the_fixed_training_deck():
    profile = load_profile()
    assert profile.name == "majkel1337-85795098"
    assert len(profile.cards) == 60
    assert Counter(profile.cards) == EXPECTED
    assert Counter(read_deck_csv()) == EXPECTED


def test_fixed_deck_selfplay_produces_native_v3_turn_leaves():
    samples, rewards = _play(123, 456, 0.15, 3_000)
    assert samples
    assert rewards in ([1, -1], [-1, 1], [0, 0])
    assert all(sample["featureSchemaVersion"] == 3 for sample in samples)
    assert all(sample["actor"] in (0, 1) for sample in samples)
