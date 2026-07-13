import json
from pathlib import Path
from exact_solver.profile import load_profile, PACKAGE_PROFILE, REPOSITORY_PROFILE


def test_profile_is_hash_verified_60_card_deck():
    profile = load_profile()
    assert len(profile.cards) == 60
    assert profile.cards.count(741) == 4
    assert profile.cards.count(1266) == 2
    assert json.loads(PACKAGE_PROFILE.read_text()) == json.loads(REPOSITORY_PROFILE.read_text())
