// SPDX-FileCopyrightText: © Pokémon/Nintendo/Creatures/GAME FREAK TM, ®, and character names are trademarks of Nintendo.
// SPDX-License-Identifier: LicenseRef-PTCG-ABC-Competition-Use-Only
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "ExactPassivePayloadV4.h"
#include "ExactSparseEvaluatorV3.h"

// V4 = V3 Semantic Trunk + Passive Residual (bias + context + sparse pairs).
class ExactSparseEvaluatorV4 {
public:
	static constexpr int ModelSchemaVersion = 1;
	static constexpr int FeatureSchemaVersion = 1;
	static constexpr int LivenessSchemaVersion = 1;
	static constexpr int ContextHidden = 32;
	static constexpr int PassiveContextScale = 4096;
	static constexpr char Magic[8] = { 'P','T','C','G','E','V','4','\0' };

	struct SemanticForwardResult {
		long long semanticValue = 0;
		std::array<std::int32_t, ContextHidden> context{};
	};

	bool load(const std::string& path, std::string& error) {
		std::ifstream in(path, std::ios::binary);
		if (!in) { error = "unable to open V4 model"; return false; }
		Header header{};
		in.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (!in || std::memcmp(header.magic, Magic, 8) != 0
			|| header.modelSchema != ModelSchemaVersion
			|| header.featureSchema != FeatureSchemaVersion
			|| header.livenessSchema != LivenessSchemaVersion
			|| header.contextHidden != ContextHidden
			|| header.passiveContextScale != PassiveContextScale
			|| header.tokenCount == 0) {
			error = "invalid V4 model header";
			return false;
		}
		tokens.resize(header.tokenCount);
		in.read(reinterpret_cast<char*>(tokens.data()), (std::streamsize)(tokens.size() * sizeof(std::int32_t)));
		passiveBias.assign(header.tokenCount, 0);
		in.read(reinterpret_cast<char*>(passiveBias.data()), (std::streamsize)(passiveBias.size() * sizeof(std::int32_t)));
		passiveContextWeight.assign(header.tokenCount, {});
		for (std::uint32_t i = 0; i < header.tokenCount; ++i)
			in.read(reinterpret_cast<char*>(passiveContextWeight[i].data()),
				(std::streamsize)(ContextHidden * sizeof(std::int16_t)));
		contextFromGlobal.assign((size_t)ContextHidden * ExactSparseEvaluatorV3::GlobalHiddenCount, 0);
		in.read(reinterpret_cast<char*>(contextFromGlobal.data()),
			(std::streamsize)(contextFromGlobal.size() * sizeof(std::int16_t)));
		contextBias.assign(ContextHidden, 0);
		in.read(reinterpret_cast<char*>(contextBias.data()), (std::streamsize)(ContextHidden * sizeof(std::int32_t)));
		passivePairs.resize(header.pairCount);
		if (header.pairCount)
			in.read(reinterpret_cast<char*>(passivePairs.data()),
				(std::streamsize)(header.pairCount * sizeof(ExactPassivePairWeightV4)));
		if (!in) { error = "truncated V4 model"; return false; }
		rebuildTokenIndex();
		modelHashValue = header.checksum;
		modelPath = path;
		standalone = true;
		loaded = true;
		return true;
	}

	void attachV3Trunk(const ExactSparseEvaluatorV3* trunk) {
		v3Trunk = trunk;
		if (trunk != nullptr) loaded = true;
	}

	void bootstrapPassiveFromV3(const ExactSparseEvaluatorV3& trunk) {
		v3Trunk = &trunk;
		const auto& table = trunk.tokenTable();
		tokens = table;
		rebuildTokenIndex();
		passiveBias.assign(tokens.size(), 0);
		passiveContextWeight.assign(tokens.size(), {});
		contextFromGlobal.assign((size_t)ContextHidden * ExactSparseEvaluatorV3::GlobalHiddenCount, 0);
		contextBias.assign(ContextHidden, 0);
		passivePairs.clear();
		for (size_t i = 0; i < tokens.size(); ++i) {
			const int id = tokens[i];
			if (id <= 0 || id >= ExactSparseEvaluatorV3::AttackTokenBase) continue;
			long long score = trunk.estimateOwnHandLinearScore(id);
			if (score > std::numeric_limits<std::int32_t>::max()) score = std::numeric_limits<std::int32_t>::max();
			if (score < std::numeric_limits<std::int32_t>::min()) score = std::numeric_limits<std::int32_t>::min();
			passiveBias[i] = (std::int32_t)score;
		}
		standalone = false;
		loaded = true;
		modelHashValue = trunk.modelHash() ^ 0x56345F4254ULL; // V4BT
	}

	bool isLoaded() const { return loaded; }
	bool hasV3Trunk() const { return v3Trunk != nullptr; }
	int modelSchemaVersion() const { return ModelSchemaVersion; }
	int featureSchemaVersion() const { return FeatureSchemaVersion; }
	std::uint64_t modelHash() const { return modelHashValue; }
	const std::string& path() const { return modelPath; }

	void setEnablePassive(bool enabled) { enablePassive = enabled; }
	void setEnablePairs(bool enabled) { enablePairs = enabled; }
	bool passiveEnabled() const { return enablePassive; }

	SemanticForwardResult forwardSemantic(const ExactSparseEvaluatorV3::FeatureRecord& features,
		unsigned long long* accumulatorHits = nullptr) const {
		SemanticForwardResult out;
		if (v3Trunk != nullptr && !features.overflow)
			out.semanticValue = v3Trunk->evaluate(features, accumulatorHits);
		out.context.fill(0);
		for (int i = 0; i < ContextHidden && i < (int)contextBias.size(); ++i)
			out.context[(size_t)i] = contextBias[(size_t)i];
		(void)contextFromGlobal;
		return out;
	}

	long long passiveCardValue(int cardId, const std::array<std::int32_t, ContextHidden>& context) const {
		const int index = indexFor(cardId);
		long long value = index < (int)passiveBias.size() ? passiveBias[(size_t)index] : 0;
		if (index < (int)passiveContextWeight.size()) {
			const auto& row = passiveContextWeight[(size_t)index];
			long long dot = 0;
			for (int i = 0; i < ContextHidden; ++i)
				dot += (long long)row[(size_t)i] * (long long)context[(size_t)i];
			value += dot / PassiveContextScale;
		}
		return value;
	}

	long long evaluatePassiveResidual(const ExactPassivePayloadV4& passive,
		const std::array<std::int32_t, ContextHidden>& context) const {
		if (!enablePassive || passive.empty()) return 0;
		long long value = 0;
		for (const auto& item : passive.counts)
			value += (long long)item.second * passiveCardValue(item.first, context);
		if (enablePairs) {
			for (const auto& pair : passivePairs) {
				const int a = passive.countOf(pair.cardA);
				const int b = passive.countOf(pair.cardB);
				if (a <= 0 || b <= 0) continue;
				if (pair.cardA == pair.cardB)
					value += (long long)pair.weight * ((long long)a * (a - 1) / 2);
				else
					value += (long long)pair.weight * (long long)a * (long long)b;
			}
		}
		return value;
	}

	long long evaluateV4(const ExactSparseEvaluatorV3::FeatureRecord& features,
		const ExactPassivePayloadV4& passive,
		unsigned long long* accumulatorHits = nullptr) const {
		const auto semantic = forwardSemantic(features, accumulatorHits);
		long long value = semantic.semanticValue + evaluatePassiveResidual(passive, semantic.context);
		if (value > ExactSparseEvaluatorV3::NonTerminalLimit) value = ExactSparseEvaluatorV3::NonTerminalLimit;
		if (value < -ExactSparseEvaluatorV3::NonTerminalLimit) value = -ExactSparseEvaluatorV3::NonTerminalLimit;
		return value;
	}

	std::vector<std::pair<int, long long>> passiveValueTable(
		const std::array<std::int32_t, ContextHidden>& context) const {
		std::vector<std::pair<int, long long>> out;
		out.reserve(tokens.size());
		for (size_t i = 0; i < tokens.size(); ++i) {
			const int id = tokens[i];
			if (id <= 0 || id >= ExactSparseEvaluatorV3::AttackTokenBase) continue;
			out.push_back({ id, passiveCardValue(id, context) });
		}
		return out;
	}

	const std::vector<ExactPassivePairWeightV4>& pairs() const { return passivePairs; }

private:
#pragma pack(push, 1)
	struct Header {
		char magic[8];
		std::uint32_t modelSchema, featureSchema, livenessSchema;
		std::uint32_t contextHidden, passiveContextScale, tokenCount, pairCount;
		std::uint64_t checksum;
		std::uint8_t reserved[32];
	};
#pragma pack(pop)

	void rebuildTokenIndex() {
		tokenIndex.clear();
		int maximum = 0;
		for (int token : tokens) if (token > maximum) maximum = token;
		tokenIndex.assign((size_t)maximum + 1, 0);
		for (size_t i = 0; i < tokens.size(); ++i)
			if (tokens[i] >= 0 && tokens[i] < (int)tokenIndex.size()) tokenIndex[(size_t)tokens[i]] = (int)i;
	}

	int indexFor(int token) const {
		return token >= 0 && token < (int)tokenIndex.size() ? tokenIndex[(size_t)token] : 0;
	}

	bool loaded = false;
	bool standalone = false;
	bool enablePassive = true;
	bool enablePairs = true;
	std::uint64_t modelHashValue = 0;
	std::string modelPath;
	const ExactSparseEvaluatorV3* v3Trunk = nullptr;
	std::vector<std::int32_t> tokens;
	std::vector<int> tokenIndex;
	std::vector<std::int32_t> passiveBias;
	std::vector<std::array<std::int16_t, ContextHidden>> passiveContextWeight;
	std::vector<std::int16_t> contextFromGlobal;
	std::vector<std::int32_t> contextBias;
	std::vector<ExactPassivePairWeightV4> passivePairs;
};
