from types import SimpleNamespace as NS

from exact_solver.evaluator import evaluate_observation


def player(*, prizes=6, hand=(), deck=40, active=(), bench=()):
    return NS(prize=[None] * prizes, hand=list(hand), handCount=len(hand), deckCount=deck,
              active=list(active), bench=list(bench))


def test_terminal_value_dominates_leaf_features():
    profile = NS(evaluator={"hand_values": {"default": 100}})
    state = NS(yourIndex=0, result=0, players=[player(), player(prizes=0)])
    assert evaluate_observation(state, profile) == 100_000_000
    state.result = 1
    assert evaluate_observation(state, profile) == -100_000_000


def test_nonterminal_integer_leaf_formula():
    profile = NS(evaluator={"hand_values": {"default": 100, "5": 140}})
    card = NS(id=5)
    pokemon = NS(maxHp=100, hp=80, energyCards=[card])
    state = NS(yourIndex=0, result=-1, players=[
        player(prizes=5, hand=[card], deck=3, active=[pokemon]),
        player(prizes=6, hand=[card, card], deck=20, active=[]),
    ])
    # prize + board + energy + hand - opponent hand - deck penalty + damage
    assert evaluate_observation(state, profile) == 1_000_000 + 2_000 + 500 + 140 - 160 - 2_000 - 2_000

