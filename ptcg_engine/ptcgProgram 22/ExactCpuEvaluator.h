#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ExactCardLivenessV4.h"
#include "ExactPassivePayloadV4.h"
#include "ExactSparseEvaluatorV3.h"
#include "ExactSparseEvaluatorV4.h"

enum class ExactEvaluatorVersion : unsigned char { V3 = 0, V4 = 1, Dual = 2 };

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
inline const char* ExactGetenv(const char* name) { return std::getenv(name); }
#ifdef _MSC_VER
#pragma warning(pop)
#endif

inline ExactEvaluatorVersion ExactEvaluatorVersionFromEnvironment() {
	const char* configured = ExactGetenv("PTCG_EXACT_EVALUATOR_VERSION");
	if (configured == nullptr) return ExactEvaluatorVersion::V3;
	std::string value(configured);
	if (value == "V4" || value == "v4") return ExactEvaluatorVersion::V4;
	if (value == "Dual" || value == "dual" || value == "DUAL") return ExactEvaluatorVersion::Dual;
	return ExactEvaluatorVersion::V3;
}

inline bool ExactEnvFlagDisabled(const char* name) {
	const char* configured = ExactGetenv(name);
	return configured != nullptr && (configured[0] == '0' || configured[0] == 'n' || configured[0] == 'N');
}

inline bool ExactEnvFlagEnabled(const char* name) {
	const char* configured = ExactGetenv(name);
	return configured != nullptr && (configured[0] == '1' || configured[0] == 'y' || configured[0] == 'Y');
}

class ExactCpuEvaluator {
public:
	bool load(const std::string& path, std::string& error) {
		const bool wantV4File = path.find("v4") != std::string::npos || path.find("V4") != std::string::npos;
		if (wantV4File && sparseV4.load(path, error)) {
			// V4-only file still needs a V3 trunk for semantic forward.
			std::string v3error;
			std::string v3path = path;
			auto pos = v3path.find("v4");
			if (pos == std::string::npos) pos = v3path.find("V4");
			if (pos != std::string::npos) {
				v3path.replace(pos, 2, "v3");
				if (!sparseV3.load(v3path, v3error)) {
					error = "V4 loaded but V3 trunk missing: " + v3error;
					return false;
				}
			} else if (!sparseV3.load(path, v3error)) {
				error = "V4 semantic trunk unavailable";
				return false;
			}
			sparseV4.attachV3Trunk(&sparseV3);
		} else {
			if (!sparseV3.load(path, error)) return false;
			sparseV4.bootstrapPassiveFromV3(sparseV3);
		}
		version = ExactEvaluatorVersionFromEnvironment();
		if (ExactEnvFlagDisabled("PTCG_EXACT_EVALUATOR_V4_ENABLE_PASSIVE")) sparseV4.setEnablePassive(false);
		if (ExactEnvFlagDisabled("PTCG_EXACT_EVALUATOR_V4_ENABLE_PAIRS")) sparseV4.setEnablePairs(false);
		fallbackToV3 = !ExactEnvFlagDisabled("PTCG_EXACT_EVALUATOR_V4_FALLBACK_TO_V3");
		loaded = true;
		return true;
	}
	bool isLoaded() const { return loaded; }
	const std::string& path() const { return sparseV3.path(); }
	int schemaVersion() const {
		if (!loaded) return 0;
		return usesV4Search() ? ExactSparseEvaluatorV4::ModelSchemaVersion : ExactSparseEvaluatorV3::SchemaVersion;
	}
	ExactEvaluatorVersion evaluatorVersion() const { return version; }
	bool usesV4Search() const { return version == ExactEvaluatorVersion::V4; }
	bool usesV4PassiveStrip() const {
		return version == ExactEvaluatorVersion::V4 || version == ExactEvaluatorVersion::Dual;
	}
	bool informationSetSafe() const { return loaded; }
	std::uint64_t modelHash() const {
		if (!loaded) return 0;
		return usesV4Search() ? sparseV4.modelHash() : sparseV3.modelHash();
	}
	size_t residentBytes() const { return loaded ? sparseV3.residentBytes() : 0; }

	bool evaluateV3Features(const ExactSparseEvaluatorV3::FeatureRecord& features, long long& value,
		unsigned long long* accumulatorHits = nullptr) const {
		if (!loaded || features.overflow) return false;
		value = sparseV3.evaluate(features, accumulatorHits); return true;
	}
	bool evaluateV4Features(const ExactSparseEvaluatorV3::FeatureRecord& features,
		const ExactPassivePayloadV4& passive, long long& value,
		unsigned long long* accumulatorHits = nullptr) const {
		if (!loaded || features.overflow || !sparseV4.isLoaded()) return false;
		value = sparseV4.evaluateV4(features, passive, accumulatorHits); return true;
	}

	// Strip Passive OwnHand tokens into payload; leave Active OwnHand for semantic trunk.
	static void splitOwnHandFeatures(ExactSparseEvaluatorV3::FeatureRecord& features,
		const State& state, int actor, ExactPassivePayloadV4& passive,
		const std::unordered_set<int>* reachable = nullptr) {
		ExactSparseEvaluatorV3::FixedSparseList<ExactSparseEvaluatorV3::MaxGlobalSparse> kept;
		std::unordered_map<int, int> passiveCounts;
		for (int i = 0; i < features.globalSparse.count; ++i) {
			const auto& item = features.globalSparse.values[i];
			if (item.relation != ExactSparseEvaluatorV3::OwnHand) {
				kept.push(item.token, item.relation, item.value);
				continue;
			}
			auto live = ExactCardLivenessV4::ClassifyCardId(state, actor, item.token, reachable);
			if (live.liveness == ExactCardLivenessV4::CardLiveness::Unknown)
				live.liveness = ExactCardLivenessV4::CardLiveness::Active;
			if (live.liveness == ExactCardLivenessV4::CardLiveness::Passive) {
				int copies = item.value / ExactSparseEvaluatorV3::BeliefScale;
				if (copies <= 0) copies = 1;
				passiveCounts[item.token] += copies;
			} else {
				kept.push(item.token, item.relation, item.value);
			}
		}
		features.globalSparse = kept;
		std::vector<std::pair<int, int>> sorted(passiveCounts.begin(), passiveCounts.end());
		passive.setCounts(std::move(sorted), ExactCardLivenessV4::LivenessSchemaVersion);
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
	const ExactSparseEvaluatorV4& v4() const { return sparseV4; }
	bool allowV3Fallback() const { return fallbackToV3; }
private:
	bool loaded = false;
	bool fallbackToV3 = true;
	ExactEvaluatorVersion version = ExactEvaluatorVersion::V3;
	ExactSparseEvaluatorV3 sparseV3;
	ExactSparseEvaluatorV4 sparseV4;
};
