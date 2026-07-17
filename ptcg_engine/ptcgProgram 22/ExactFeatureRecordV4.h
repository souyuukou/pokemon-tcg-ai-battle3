// SPDX-FileCopyrightText: © Pokémon/Nintendo/Creatures/GAME FREAK TM, ®, and character names are trademarks of Nintendo.
// SPDX-License-Identifier: LicenseRef-PTCG-ABC-Competition-Use-Only
#pragma once

#include "ExactPassivePayloadV4.h"
#include "ExactSparseEvaluatorV3.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

// Explicit V4 feature split: Passive hand identities leave Semantic OwnHand only.
struct SemanticFeaturesV4 {
	ExactSparseEvaluatorV3::FeatureRecord features{};
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

// V4.0: strip Passive copies from OwnHand only. Deck/prize/combo/hidden features that
// share the same card ID must remain — otherwise deck residual information vanishes.
inline FeatureRecordV4 BuildFromV3(
	const ExactSparseEvaluatorV3::FeatureRecord& source,
	const ExactPassivePayloadV4& passive,
	const std::unordered_set<int>* /*passiveIds*/ = nullptr) {
	FeatureRecordV4 out;
	out.semantic.features = source;
	out.passive = passive;
	out.overflow = source.overflow;
	out.semantic.passiveHandTotal = passive.totalCount;

	ExactSparseEvaluatorV3::FixedSparseList<ExactSparseEvaluatorV3::MaxGlobalSparse> kept;
	for (int i = 0; i < source.globalSparse.count; ++i) {
		const auto& item = source.globalSparse.values[i];
		if (item.relation != ExactSparseEvaluatorV3::OwnHand) {
			kept.push(item.token, item.relation, item.value);
			continue;
		}
		const int passiveCopies = passive.countOf(item.token);
		if (passiveCopies <= 0) {
			kept.push(item.token, item.relation, item.value);
			continue;
		}
		int copies = item.value / ExactSparseEvaluatorV3::BeliefScale;
		if (copies <= 0) copies = 1;
		const int keep = copies - passiveCopies;
		if (keep > 0)
			kept.push(item.token, item.relation, keep * ExactSparseEvaluatorV3::BeliefScale);
	}
	out.semantic.features.globalSparse = kept;
	return out;
}

} // namespace ExactFeatureV4
