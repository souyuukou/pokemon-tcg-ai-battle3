#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "ExactSparseEvaluatorV3.h"

// V3 is the sole evaluator format.  Search API names containing "V2" are
// unrelated to the model schema and remain source/ABI compatible.
class ExactCpuEvaluator {
public:
	bool load(const std::string& path, std::string& error) {
		if (!sparseV3.load(path, error)) return false;
		loaded = true; return true;
	}
	bool isLoaded() const { return loaded; }
	const std::string& path() const { return sparseV3.path(); }
	int schemaVersion() const { return loaded ? ExactSparseEvaluatorV3::SchemaVersion : 0; }
	bool informationSetSafe() const { return loaded; }
	std::uint64_t modelHash() const { return loaded ? sparseV3.modelHash() : 0; }
	size_t residentBytes() const { return loaded ? sparseV3.residentBytes() : 0; }
	bool evaluateV3Features(const ExactSparseEvaluatorV3::FeatureRecord& features, long long& value,
		unsigned long long* accumulatorHits = nullptr) const {
		if (!loaded || features.overflow) return false;
		value = sparseV3.evaluate(features, accumulatorHits); return true;
	}
	std::vector<std::int16_t> cardContinuationSignature(int cardId) const {
		return loaded ? sparseV3.cardContinuationSignature(cardId)
			: std::vector<std::int16_t>{ (std::int16_t)(cardId & 0x7fff),
				(std::int16_t)((unsigned)cardId >> 15) };
	}
	long long evaluate(const State& state, int actor,
		const std::unordered_map<int, int>* actorProfile = nullptr,
		const ExactSparseEvaluatorV3::BeliefInput* belief = nullptr) const {
		return sparseV3.evaluate(state, actor, actorProfile, belief);
	}
private:
	bool loaded = false;
	ExactSparseEvaluatorV3 sparseV3;
};
