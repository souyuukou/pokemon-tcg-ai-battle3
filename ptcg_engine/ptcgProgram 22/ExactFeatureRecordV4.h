// SPDX-FileCopyrightText: © Pokémon/Nintendo/Creatures/GAME FREAK TM, ®, and character names are trademarks of Nintendo.
// SPDX-License-Identifier: LicenseRef-PTCG-ABC-Competition-Use-Only
#pragma once

#include "ExactPassivePayloadV4.h"
#include "ExactSparseEvaluatorV3.h"

#include <cstdint>
#include <unordered_set>

// Explicit V4 feature split: Passive identities never enter Semantic trunk.
struct SemanticFeaturesV4 {
	ExactSparseEvaluatorV3::FeatureRecord features{};
	// Identity-free aggregates (ablation candidates).
	std::int32_t passiveHandTotal = 0;
	std::int32_t passivePokemonCount = 0;
	std::int32_t passiveTrainerCount = 0;
	std::int32_t passiveEnergyCount = 0;
	std::int32_t passiveDeckTotal = 0;
};

struct FeatureRecordV4 {
	SemanticFeaturesV4 semantic;
	ExactPassivePayloadV4 passive;
	bool overflow = false;
};

namespace ExactFeatureV4 {

inline bool IsPassiveIdentityRelation(std::int16_t relation) {
	using R = ExactSparseEvaluatorV3::GlobalRelation;
	switch (relation) {
	case R::OwnHand:
	case R::OwnHiddenPool:
	case R::OwnDeckExpected:
	case R::OwnPrizeExpected:
	case R::OwnDeckExists:
	case R::OwnPrizeExists:
	case R::OwnKnownDeck:
	case R::OwnKnownPrize:
	case R::OwnKnownTop0:
	case R::OwnKnownTop1:
	case R::OwnKnownTop2:
	case R::OwnKnownTop3:
	case R::OwnKnownBottom0:
	case R::OwnKnownBottom1:
	case R::OwnKnownBottom2:
	case R::OwnKnownBottom3:
	case R::ComboProbability:
		return true;
	default:
		return false;
	}
}

// Strip Passive card IDs from identity-bearing global sparse relations.
// Tokens listed in `passiveIds` are removed from those relations.
inline FeatureRecordV4 BuildFromV3(
	const ExactSparseEvaluatorV3::FeatureRecord& source,
	const ExactPassivePayloadV4& passive,
	const std::unordered_set<int>* passiveIds = nullptr) {
	FeatureRecordV4 out;
	out.semantic.features = source;
	out.passive = passive;
	out.overflow = source.overflow;
	out.semantic.passiveHandTotal = passive.totalCount;

	ExactSparseEvaluatorV3::FixedSparseList<ExactSparseEvaluatorV3::MaxGlobalSparse> kept;
	for (int i = 0; i < source.globalSparse.count; ++i) {
		const auto& item = source.globalSparse.values[i];
		const bool identityRelation = IsPassiveIdentityRelation(item.relation);
		const bool isPassiveToken = passiveIds != nullptr && passiveIds->contains(item.token);
		const bool isPassiveHand = item.relation == ExactSparseEvaluatorV3::OwnHand
			&& (passive.countOf(item.token) > 0 || isPassiveToken);
		if (identityRelation && (isPassiveToken || isPassiveHand))
			continue;
		if (identityRelation && item.relation != ExactSparseEvaluatorV3::OwnHand
			&& passiveIds != nullptr && isPassiveToken)
			continue;
		kept.push(item.token, item.relation, item.value);
	}
	out.semantic.features.globalSparse = kept;
	return out;
}

} // namespace ExactFeatureV4
