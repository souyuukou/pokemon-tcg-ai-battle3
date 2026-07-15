from types import SimpleNamespace

from exact_solver.agent_policy import PolicyContext, _turn_owner, choose_action


def test_turn_owner_follows_first_player_and_turn_parity():
    assert _turn_owner(SimpleNamespace(turn=1, firstPlayer=1)) == 1
    assert _turn_owner(SimpleNamespace(turn=2, firstPlayer=1)) == 0
    assert _turn_owner(SimpleNamespace(turn=0, firstPlayer=1)) is None


def test_opponent_turn_selection_is_fixed_and_does_not_start_search(monkeypatch):
    released = []
    monkeypatch.setattr("cg.api.exact_turn_release", released.append)
    current = SimpleNamespace(turn=5, firstPlayer=0, yourIndex=1)
    select = SimpleNamespace(
        minCount=1,
        maxCount=2,
        option=[SimpleNamespace(type=3), SimpleNamespace(type=3), SimpleNamespace(type=3)],
    )
    observation = SimpleNamespace(current=current, select=select)
    context = PolicyContext()
    context.session_id = 77

    action, certified, reason = choose_action(observation, context=context)

    assert action == [0]
    assert certified is False
    assert reason == "fixed-opponent-turn-selection"
    assert released == [77]
    assert context.session_id is None
    assert context.budget.used_seconds < 0.1
