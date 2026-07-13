// Exact-search boundaries and audited symmetry predicates.
#pragma once

#include "GameProc.h"

// Leaf is after end-of-turn effects and Pokémon Checkup, but before the next
// player's TurnStart/draw.  This predicate does not mutate State.
inline bool IsExactTurnLeaf(const State& state) {
	if (state.phase != GamePhase::PokemonCheckupEnd || state.functionStack.empty()) {
		return false;
	}
	auto it = FunctionIndexTable.find((long long)(void*)TurnStart);
	return it != FunctionIndexTable.end()
		&& state.functionStack.back().functionIndex == it->second;
}

// Returns whether physical prize indices are semantically exchangeable at the
// current selection. The exact solver still has to branch on hidden identity
// and aggregate its integer weight; this only removes meaningless decisions.
inline bool CanQuotientFacedownPrizeSelection(const State& state) {
	if (state.selectType != SelectType::Card
		|| state.selectContext != SelectContext::ToHand
		|| state.selectMin != state.selectMax) {
		return false;
	}
	const PlayerState& ps = state.players.at(state.selectPlayer);
	if (state.options.size() != ps.prize.size()) {
		return false;
	}
	for (CardRef ref : ps.prize) {
		const Card& card = state.getCard(ref);
		if (!card.reverse) {
			return false;
		}
		const Skill* ability = state.getAbility(card, card.getMaster());
		if (ability != nullptr && ability->luckyBonus) {
			return false;
		}
	}
	return true;
}

inline std::vector<int> ExactPrizeRepresentative(const State& state) {
	std::vector<int> selected;
	if (!CanQuotientFacedownPrizeSelection(state)) {
		return selected;
	}
	for (int i = 0; i < state.selectMax; ++i) {
		selected.push_back(i);
	}
	return selected;
}

