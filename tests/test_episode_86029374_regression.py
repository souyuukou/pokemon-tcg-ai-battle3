import json
from pathlib import Path

import exact_solver.agent_policy as policy
import main


FIXTURE = Path(__file__).parent / "fixtures" / "episode_86029374_step5.json"


def test_episode_86029374_first_main_always_returns_a_legal_action(monkeypatch):
    """Regression for the Linux artifact failure that returned [] at step 5."""
    observation = json.loads(FIXTURE.read_text(encoding="utf-8"))
    monkeypatch.setenv("PTCG_EXACT_TURN_MS", "1000")
    monkeypatch.setenv("PTCG_EXACT_SELECTION_MS", "250")
    policy._default_context.reset()
    policy._evaluator_loaded = False

    selected = main.agent(observation)

    select = observation["select"]
    assert select["minCount"] <= len(selected) <= select["maxCount"]
    assert len(selected) == len(set(selected))
    assert all(0 <= index < len(select["option"]) for index in selected)
    # The external wire state must retain all three root options.  Before the
    # fix, ExactHiddenState shifted the raw State prefix, rootActions was empty,
    # and the competition process eventually returned an invalid [].
    assert policy.last_decision is not None
    assert policy.last_decision["rootWorkers"] == 2
    assert {tuple(item["selected"]) for item in policy.last_decision["rootActions"]} == {
        (0,), (1,), (2,)
    }
