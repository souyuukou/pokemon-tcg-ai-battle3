from cg.game import battle_finish, battle_select, battle_start
from exact_solver import agent_policy
from exact_solver.profile import load_profile
import main


def test_native_exact_api_returns_legal_action(monkeypatch):
    monkeypatch.setenv("PTCG_EXACT_TURN_MS", "100")
    monkeypatch.setenv("PTCG_EXACT_SELECTION_MS", "50")
    deck = list(load_profile().cards)
    observation, start = battle_start(deck, deck)
    assert observation is not None, (start.errorPlayer, start.errorType)
    seen = False
    try:
        for _ in range(60):
            action = main.agent(observation)
            select = observation.get("select")
            if select is not None:
                assert select["minCount"] <= len(action) <= select["maxCount"]
                assert len(action) == len(set(action))
                assert all(0 <= index < len(select["option"]) for index in action)
            seen |= agent_policy.last_decision is not None
            observation = battle_select(action)
            if seen:
                break
        assert seen
        decision = agent_policy.last_decision
        assert decision["lowerDenominator"] > 0
        assert decision["upperDenominator"] > 0
        assert isinstance(decision["certified"], bool)
        assert decision["expandedNodes"] > 0
    finally:
        battle_finish()

