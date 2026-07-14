from cg.api import exact_evaluate_action_v2, to_observation_class
from cg.game import battle_finish, battle_select, battle_start_seeded


MAJKEL_85795098 = [
    5, 5, 13, 19, 19, 19, 19, 66, 66, 140, 305, 305, 305, 343,
    741, 741, 741, 741, 742, 742, 742, 742, 743, 743, 743, 743,
    1079, 1079, 1079, 1081, 1081, 1081, 1081, 1086, 1086, 1086,
    1086, 1097, 1129, 1152, 1152, 1152, 1152, 1182, 1182, 1182,
    1184, 1197, 1197, 1197, 1225, 1225, 1225, 1225, 1231, 1231,
    1231, 1231, 1266, 1266,
]


def _seed0_second_main():
    observation, start = battle_start_seeded(MAJKEL_85795098, MAJKEL_85795098, 0)
    assert observation is not None, (start.errorPlayer, start.errorType)
    for _ in range(20):
        select = observation.get("select")
        current = observation.get("current")
        if select and current and select["type"] == 0 and select["context"] == 0:
            if current["turn"] == 2:
                return observation
            end = next(i for i, option in enumerate(select["option"])
                       if option["type"] == 14)
            observation = battle_select([end])
            continue
        observation = battle_select(MAJKEL_85795098 if select is None
                                    else list(range(select["minCount"])))
    raise AssertionError("seed 0 did not reach turn 2 main")


def test_fixed_deck_xerosic_uses_exact_mass_and_provisional_choice():
    try:
        observation = _seed0_second_main()
        current = observation["current"]
        select = observation["select"]
        me = current["players"][current["yourIndex"]]
        xerosic = next(i for i, option in enumerate(select["option"])
                       if option["type"] == 7
                       and me["hand"][option["index"]]["id"] == 1197)

        result = exact_evaluate_action_v2(
            to_observation_class(observation), MAJKEL_85795098,
            [100] * len(MAJKEL_85795098), 1_000, xerosic,
            opponent_deck=MAJKEL_85795098,
        )

        assert result["selected"] == [xerosic]
        assert result["opaqueNodes"] == 0
        assert result["chanceMassMismatches"] == 0
        assert result["probabilityExact"] is True
        assert result["provisionalOpponentPolicy"] is True
        assert result["provisionalOpponentPolicyNodes"] >= 1
        assert result["opponentPolicyOptimal"] is False
        assert result["certified"] is False
        assert result["rawOutcomes"] > result["groupedOutcomes"] > 0
        assert result["memoryLimitReached"] is False
        assert result["peakRssBytes"] < 3 * 1024**3
    finally:
        battle_finish()
