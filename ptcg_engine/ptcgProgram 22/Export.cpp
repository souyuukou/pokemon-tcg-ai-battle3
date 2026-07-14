// SPDX-FileCopyrightText: © Pokémon/Nintendo/Creatures/GAME FREAK TM, ®, and character names are trademarks of Nintendo.
// SPDX-License-Identifier: LicenseRef-PTCG-ABC-Competition-Use-Only
// Part of the Pokémon TCG AI Battle Challenge. Provided for Competition use only;
// the full license is in the LICENSES/ folder and incorporates the Competition Rules.
// Competition Rules: https://www.kaggle.com/competitions/pokemon-tcg-ai-battle/rules

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "All.h"
#include <future>
#include <atomic>
#include <mutex>

#ifdef _MSC_VER
#	define GAME_API __declspec(dllexport)
#else
#	define GAME_API __attribute__ ((visibility("default")))
#endif


static JsonBuilder AllCardJson;
static JsonBuilder AllAttackJson;
static void AppendUnsignedLongLong(JsonBuilder& j, unsigned long long value);

extern "C" GAME_API const char8_t* ExactLoadEvaluatorModel(ApiData* data, const char* path) {
  JsonBuilder& j = data->jsonBuilder;
  j.clear(); j.append('{');
  std::string error;
  bool loaded = false;
  auto evaluator = std::make_shared<ExactCpuEvaluator>();
  loaded = path != nullptr && evaluator->load(path, error);
  int schema = loaded ? evaluator->schemaVersion() : 0;
  bool informationSetSafe = loaded && evaluator->informationSetSafe();
  unsigned long long modelHash = loaded ? evaluator->modelHash() : 0;
  if (loaded) data->exactEvaluator = std::move(evaluator);
  j.appendKeyValue("loaded", loaded);
  j.appendCommaKeyValue("schemaVersion", schema);
  j.appendCommaKeyValue("informationSetSafe", informationSetSafe);
  j.appendCommaKey("modelHash"); AppendUnsignedLongLong(j, modelHash);
  j.appendCommaKey("error");
  j.appendDoubleQuote(std::u8string((const char8_t*)error.c_str(), error.size()));
  j.append('}');
  return j.buf.c_str();
}

extern "C" GAME_API void ExactUnloadEvaluatorModel(ApiData* data) {
  if (data != nullptr) data->exactEvaluator.reset();
}

extern "C" GAME_API const char8_t* ExactArithmeticDiagnostics() {
  static thread_local JsonBuilder j; j.clear();
  ExactWeight a(50'063'860ULL), b(321'387'366'339'585ULL);
  ExactWeight product = ExactWeight::multiply(a, b);
  auto division = ExactWeight::divideRemainder(product, a);
  ExactWeight common = ExactWeight::gcd(product, a);
  j.append('{'); j.appendKey("product"); j.appendDoubleQuote(product.text().c_str());
  j.appendCommaKey("quotient"); j.appendDoubleQuote(division.first.text().c_str());
  j.appendCommaKey("remainder"); j.appendDoubleQuote(division.second.text().c_str());
  j.appendCommaKey("gcd"); j.appendDoubleQuote(common.text().c_str());
  j.appendCommaKeyValue("bits", (int)product.bitLength());
  j.appendCommaKeyValue("promoted", product.isLarge()); j.append('}'); return j.buf.c_str();
}

extern "C" GAME_API long long ExactEvaluateFeaturesV2(ApiData* data,
  const short* dense, int denseCount, const int* sparseTriplets, int sparseCount, int* error) {
  if (error != nullptr) *error = 0;
  if (data == nullptr || !data->exactEvaluator || dense == nullptr
    || denseCount != ExactSparseEvaluatorV2::DenseCount || sparseCount < 0
    || (sparseCount != 0 && sparseTriplets == nullptr)) {
    if (error != nullptr) *error = 1; return 0;
  }
  ExactSparseEvaluatorV2::FeatureRecord features;
  for (int i = 0; i < denseCount; ++i) features.dense[i] = dense[i];
  features.sparse.reserve(sparseCount);
  for (int i = 0; i < sparseCount; ++i) {
    int relation = sparseTriplets[i * 3], cardId = sparseTriplets[i * 3 + 1], q8 = sparseTriplets[i * 3 + 2];
    if (relation < 0 || relation >= ExactSparseEvaluatorV2::RelationCount
      || q8 < std::numeric_limits<short>::min() || q8 > std::numeric_limits<short>::max()) {
      if (error != nullptr) *error = 2; return 0;
    }
    features.sparse.push_back({ cardId, (short)relation, (short)q8 });
  }
  long long value = 0;
  if (!data->exactEvaluator->evaluateV2Features(features, value)) {
    if (error != nullptr) *error = 3; return 0;
  }
  return value;
}

extern "C" GAME_API int ExactReplayTraceBegin(ApiData* data) {
  if (data == nullptr || data->apiDataType != 1) return 30;
  data->exactReplayTurnLeaves.clear(); data->exactReplayLastTurn = -1;
  data->exactReplayTraceEnabled = true; data->game.config.pauseAtExactTurnLeaf = true; return 0;
}

extern "C" GAME_API void ExactReplayTraceEnd(ApiData* data) {
  if (data == nullptr) return;
  data->exactReplayTraceEnabled = false; data->game.config.pauseAtExactTurnLeaf = false;
  data->exactReplayTurnLeaves.clear(); data->exactReplayLastTurn = -1;
}

extern "C" GAME_API const char8_t* ExactReplayTraceDrain(ApiData* data) {
  if (data == nullptr || data->apiDataType != 1) return nullptr;
  JsonBuilder& j = data->jsonBuilder; j.clear(); j.append('[');
  for (int sampleIndex = 0; sampleIndex < (int)data->exactReplayTurnLeaves.size(); ++sampleIndex) {
    j.comma(sampleIndex);
    State& state = data->exactReplayTurnLeaves[sampleIndex].first; state.game = &data->game;
    int actor = data->exactReplayTurnLeaves[sampleIndex].second;
    std::unordered_map<int, int> profile;
    for (int id : data->game.config.decks[actor].cards) profile[id]++;
    auto features = ExactSparseEvaluatorV2::extractFeatures(state, actor, &profile);
    std::string featureBytes((const char*)features.dense.data(), sizeof(features.dense));
    featureBytes.append((const char*)features.sparse.data(), features.sparse.size() * sizeof(features.sparse[0]));
    unsigned long long lo = ExactSipHash24(featureBytes, 0x4b4e4f574c454447ULL, 0x4553544154454b45ULL);
    unsigned long long hi = ExactSipHash24(featureBytes, 0x494e464f524d4154ULL, 0x494f4e5345545632ULL);
    std::ostringstream key; key << std::hex << std::setfill('0') << std::setw(16) << hi << std::setw(16) << lo;
    j.append('{'); j.appendKeyValue("turn", state.turn); j.appendCommaKeyValue("actor", actor);
    std::string keyText = key.str();
    j.appendCommaKey("informationStateKey"); j.appendDoubleQuote(keyText.c_str());
    j.appendCommaKey("dense"); j.append('[');
    for (int i = 0; i < (int)features.dense.size(); ++i) { j.comma(i); j.append((int)features.dense[i]); }
    j.append(']'); j.appendCommaKey("sparse"); j.append('[');
    for (int i = 0; i < (int)features.sparse.size(); ++i) {
      j.comma(i); j.append('['); j.append((int)features.sparse[i].relation); j.append(',');
      j.append(features.sparse[i].cardId); j.append(','); j.append((int)features.sparse[i].q8); j.append(']');
    }
    j.append(']'); j.append('}');
  }
  j.append(']'); data->exactReplayTurnLeaves.clear(); return j.buf.c_str();
}

static const char8_t* JsonResult(ApiData* data, const SearchInfo& si) {
  SearchReturnJson(data->jsonBuilder, si);
  return data->jsonBuilder.buf.c_str();
}

static void CopyIdPtr(int* src, std::vector<int>& dest, int count, int& error) {
  if (error) {
    return;
  }
  dest.resize(count);
  for (int i : range(count)) {
    dest[i] = src[i];
    if (!CardTable.contains(dest[i])) {
      error = 1;
      break;
    }
  }
}

static void AppendLongLong(JsonBuilder& j, long long value) {
  std::string text = std::to_string(value);
  for (char c : text) j.append(c);
}

static void AppendUnsignedLongLong(JsonBuilder& j, unsigned long long value) {
  std::string text = std::to_string(value);
  for (char c : text) j.append(c);
}

static void AppendExactNumerator(JsonBuilder& j, const ExactFraction& value) {
  for (char c : value.numeratorText()) j.append(c);
}
static void AppendExactDenominator(JsonBuilder& j, const ExactFraction& value) {
  for (char c : value.denominatorText()) j.append(c);
}

static const char8_t* ExactDecisionJson(ApiData* data, const ExactDecision& decision, long long sessionId = -1) {
  JsonBuilder& j = data->jsonBuilder;
  j.clear(); j.append('{');
  j.appendKey("selected"); j.append('[');
  for (int i : range(decision.score.action)) { j.comma(i); j.append(decision.score.action[i]); }
  j.append(']');
  j.appendCommaKey("lowerNumerator"); AppendExactNumerator(j, decision.score.lower);
  j.appendCommaKey("lowerDenominator"); AppendExactDenominator(j, decision.score.lower);
  j.appendCommaKey("upperNumerator"); AppendExactNumerator(j, decision.score.upper);
  j.appendCommaKey("upperDenominator"); AppendExactDenominator(j, decision.score.upper);
  j.appendCommaKeyValue("certified", decision.score.certified);
  j.appendCommaKey("certificationScope"); j.appendDoubleQuote("exact_evaluator_expectation");
  j.appendCommaKeyValue("probabilityExact", decision.metrics.probabilityExact);
  j.appendCommaKeyValue("informationSetSafe", decision.metrics.informationSetSafe);
  j.appendCommaKeyValue("evaluatorApproximate", true);
  j.appendCommaKeyValue("evaluatorSchemaVersion", data->exactEvaluator ? data->exactEvaluator->schemaVersion() : 0);
  j.appendCommaKey("evaluatorModelHash"); AppendUnsignedLongLong(j, data->exactEvaluator ? data->exactEvaluator->modelHash() : 0);
  j.appendCommaKey("expandedNodes"); AppendUnsignedLongLong(j, decision.metrics.expanded);
  j.appendCommaKey("mergedNodes"); AppendUnsignedLongLong(j, decision.metrics.merged);
  j.appendCommaKeyValue("timedOut", decision.metrics.timedOut);
  j.appendCommaKeyValue("arithmeticOverflow", decision.metrics.arithmeticOverflow);
  j.appendCommaKey("leafNodes"); AppendUnsignedLongLong(j, decision.metrics.leaves);
  j.appendCommaKey("opaqueNodes"); AppendUnsignedLongLong(j, decision.metrics.opaque);
  j.appendCommaKey("exceptionNodes"); AppendUnsignedLongLong(j, decision.metrics.exceptions);
  j.appendCommaKey("lastException");
  j.appendDoubleQuote(std::u8string((const char8_t*)decision.metrics.lastException.c_str(), decision.metrics.lastException.size()));
  j.appendCommaKeyValue("lastPendingDetail", decision.metrics.lastPendingDetail);
  j.appendCommaKeyValue("lastPendingPlayer", decision.metrics.lastPendingPlayer);
  j.appendCommaKeyValue("lastPendingEffectCardId", decision.metrics.lastPendingEffectCardId);
  j.appendCommaKeyValue("lastPendingEffectPlayer", decision.metrics.lastPendingEffectPlayer);
  j.appendCommaKeyValue("lastPendingNullCount", decision.metrics.lastPendingNullCount);
  j.appendCommaKeyValue("lastPendingDeckUnknown", decision.metrics.lastPendingDeckUnknown);
  j.appendCommaKey("unknownOpponentListNodes"); AppendUnsignedLongLong(j, decision.metrics.unknownOpponentList);
  j.appendCommaKey("unsupportedConcreteReferenceNodes"); AppendUnsignedLongLong(j, decision.metrics.unsupportedConcreteReference);
  j.appendCommaKey("interruptedTransitionNodes"); AppendUnsignedLongLong(j, decision.metrics.interruptedTransition);
  j.appendCommaKey("rawOutcomes"); AppendUnsignedLongLong(j, decision.metrics.rawOutcomes);
  j.appendCommaKey("groupedOutcomes"); AppendUnsignedLongLong(j, decision.metrics.groupedOutcomes);
  j.appendCommaKey("depthLimitNodes"); AppendUnsignedLongLong(j, decision.metrics.depthLimitNodes);
  j.appendCommaKeyValue("maxDepth", decision.metrics.maxDepth);
  j.appendCommaKeyValue("lastDepthSelectType", decision.metrics.lastDepthSelectType);
  j.appendCommaKeyValue("lastDepthTurnActionCount", decision.metrics.lastDepthTurnActionCount);
  j.appendCommaKeyValue("rootWorkers", decision.metrics.rootWorkers);
  j.appendCommaKey("policyNodes"); AppendUnsignedLongLong(j, decision.metrics.policyNodes);
  j.appendCommaKey("policyHits"); AppendUnsignedLongLong(j, decision.metrics.policyHits);
  j.appendCommaKey("policyMisses"); AppendUnsignedLongLong(j, decision.metrics.policyMisses);
  j.appendCommaKey("rerootCount"); AppendUnsignedLongLong(j, decision.metrics.rerootCount);
  j.appendCommaKey("conditionedWorldsRemoved"); AppendUnsignedLongLong(j, 0);
  j.appendCommaKey("conditionedMass"); AppendUnsignedLongLong(j, 0);
  j.appendCommaKey("resumedNodes"); AppendUnsignedLongLong(j, decision.metrics.resumedNodes);
  j.appendCommaKey("avoidedExpandedNodes"); AppendUnsignedLongLong(j, decision.metrics.avoidedExpandedNodes);
  j.appendCommaKey("partialDecisionNodes"); AppendUnsignedLongLong(j, decision.metrics.partialDecisionNodes);
  j.appendCommaKey("partialChanceNodes"); AppendUnsignedLongLong(j, decision.metrics.partialChanceNodes);
  j.appendCommaKey("semanticActionRemaps"); AppendUnsignedLongLong(j, decision.metrics.semanticActionRemaps);
  j.appendCommaKey("sessionInvalidations"); AppendUnsignedLongLong(j, decision.metrics.sessionInvalidations);
  j.appendCommaKey("sessionBytes"); AppendUnsignedLongLong(j, decision.metrics.sessionBytes);
  j.appendCommaKey("deadlineOverrunMs"); AppendLongLong(j, decision.metrics.deadlineOverrunMs);
	 j.appendCommaKey("canonicalStateMerges"); AppendUnsignedLongLong(j, decision.metrics.canonicalStateMerges);
	 j.appendCommaKey("successorMerges"); AppendUnsignedLongLong(j, decision.metrics.successorMerges);
	 j.appendCommaKey("distributionMerges"); AppendUnsignedLongLong(j, decision.metrics.distributionMerges);
	 j.appendCommaKey("rootSharedTTHits"); AppendUnsignedLongLong(j, decision.metrics.rootSharedTTHits);
	 j.appendCommaKey("beliefWorldsBefore"); AppendUnsignedLongLong(j, decision.metrics.beliefWorldsBefore);
	 j.appendCommaKey("beliefWorldsAfter"); AppendUnsignedLongLong(j, decision.metrics.beliefWorldsAfter);
	 j.appendCommaKey("largestEquivalenceClass"); AppendUnsignedLongLong(j, decision.metrics.largestEquivalenceClass);
	 j.appendCommaKey("resumedActionCount"); AppendUnsignedLongLong(j, decision.metrics.resumedActionCount);
	 j.appendCommaKey("resumedChanceMass"); AppendUnsignedLongLong(j, decision.metrics.resumedChanceMass);
	 j.appendCommaKey("partialRevealHits"); AppendUnsignedLongLong(j, decision.metrics.partialRevealHits);
	 j.appendCommaKey("enumeratedHiddenWorlds"); AppendUnsignedLongLong(j, decision.metrics.enumeratedHiddenWorlds);
	 j.appendCommaKeyValue("currentRootAction", decision.metrics.currentRootAction);
	 j.appendCommaKey("peakRssBytes"); AppendUnsignedLongLong(j, decision.metrics.peakRssBytes);
	 j.appendCommaKeyValue("memoryLimitReached", decision.metrics.memoryLimitReached);
	 j.appendCommaKey("partialDecisionHits"); AppendUnsignedLongLong(j, decision.metrics.partialDecisionHits);
	 j.appendCommaKey("partialChanceHits"); AppendUnsignedLongLong(j, decision.metrics.partialChanceHits);
	 j.appendCommaKey("partialTableBytes"); AppendUnsignedLongLong(j, decision.metrics.partialTableBytes);
	 j.appendCommaKey("rootRetryKeyMatches"); AppendUnsignedLongLong(j, decision.metrics.rootRetryKeyMatches);
	 j.appendCommaKey("rootRetryKeyMismatches"); AppendUnsignedLongLong(j, decision.metrics.rootRetryKeyMismatches);
	 j.appendCommaKey("beliefNodes"); AppendUnsignedLongLong(j, decision.metrics.beliefNodes);
	 j.appendCommaKey("informationSets"); AppendUnsignedLongLong(j, decision.metrics.informationSets);
	 j.appendCommaKey("strategyFusionPrevented"); AppendUnsignedLongLong(j, decision.metrics.strategyFusionPrevented);
	 j.appendCommaKey("smallWeightOps"); AppendUnsignedLongLong(j, decision.metrics.smallWeightOps);
	 j.appendCommaKey("bigWeightPromotions"); AppendUnsignedLongLong(j, decision.metrics.bigWeightPromotions);
	 j.appendCommaKeyValue("maxWeightBits", (int)decision.metrics.maxWeightBits);
	 j.appendCommaKey("chanceMassMismatches"); AppendUnsignedLongLong(j, decision.metrics.chanceMassMismatches);
	 j.appendCommaKey("illegalInformationSetSplits"); AppendUnsignedLongLong(j, decision.metrics.illegalInformationSetSplits);
	 j.appendCommaKey("rootActions"); j.append('[');
	 for (int ri : range(decision.rootActions)) {
	   j.comma(ri); j.append('{');
	   j.appendKey("selected"); j.append('[');
	   for (int ai : range(decision.rootActions[ri].action)) { j.comma(ai); j.append(decision.rootActions[ri].action[ai]); }
	   j.append(']');
	   j.appendCommaKey("lowerNumerator"); AppendExactNumerator(j, decision.rootActions[ri].lower);
	   j.appendCommaKey("lowerDenominator"); AppendExactDenominator(j, decision.rootActions[ri].lower);
	   j.appendCommaKey("upperNumerator"); AppendExactNumerator(j, decision.rootActions[ri].upper);
	   j.appendCommaKey("upperDenominator"); AppendExactDenominator(j, decision.rootActions[ri].upper);
	   j.appendCommaKeyValue("certified", decision.rootActions[ri].certified); j.append('}');
	 }
	 j.append(']');
  if (sessionId >= 0) { j.appendCommaKey("sessionId"); AppendLongLong(j, sessionId); }
  j.append('}');
  return j.buf.c_str();
}

static void MergeExactMetrics(ExactMetrics& into, const ExactMetrics& from) {
  into.expanded += from.expanded; into.merged += from.merged; into.leaves += from.leaves;
  into.opaque += from.opaque; into.exceptions += from.exceptions;
  into.unknownOpponentList += from.unknownOpponentList;
  into.unsupportedConcreteReference += from.unsupportedConcreteReference;
  into.interruptedTransition += from.interruptedTransition;
  into.rawOutcomes += from.rawOutcomes; into.groupedOutcomes += from.groupedOutcomes;
  into.depthLimitNodes += from.depthLimitNodes; into.maxDepth = std::max(into.maxDepth, from.maxDepth);
  into.timedOut = into.timedOut || from.timedOut;
  into.arithmeticOverflow = into.arithmeticOverflow || from.arithmeticOverflow;
  into.policyNodes += from.policyNodes; into.policyHits += from.policyHits;
  into.policyMisses += from.policyMisses; into.rerootCount += from.rerootCount;
  into.resumedNodes += from.resumedNodes; into.avoidedExpandedNodes += from.avoidedExpandedNodes;
  into.partialDecisionNodes += from.partialDecisionNodes; into.partialChanceNodes += from.partialChanceNodes;
  into.semanticActionRemaps += from.semanticActionRemaps;
  into.sessionInvalidations += from.sessionInvalidations;
  into.sessionBytes += from.sessionBytes;
  into.deadlineOverrunMs = std::max(into.deadlineOverrunMs, from.deadlineOverrunMs);
	into.canonicalStateMerges += from.canonicalStateMerges;
	into.successorMerges += from.successorMerges;
	into.distributionMerges += from.distributionMerges;
	into.rootSharedTTHits += from.rootSharedTTHits;
	into.beliefWorldsBefore += from.beliefWorldsBefore;
	into.beliefWorldsAfter += from.beliefWorldsAfter;
	into.largestEquivalenceClass = std::max(into.largestEquivalenceClass, from.largestEquivalenceClass);
	into.resumedActionCount += from.resumedActionCount;
	into.resumedChanceMass += from.resumedChanceMass;
	into.partialRevealHits += from.partialRevealHits;
	into.enumeratedHiddenWorlds += from.enumeratedHiddenWorlds;
	if (from.currentRootAction >= 0) into.currentRootAction = from.currentRootAction;
	into.peakRssBytes = std::max(into.peakRssBytes, from.peakRssBytes);
	into.memoryLimitReached = into.memoryLimitReached || from.memoryLimitReached;
	into.partialDecisionHits += from.partialDecisionHits;
	into.partialChanceHits += from.partialChanceHits;
	into.partialTableBytes += from.partialTableBytes;
	into.rootRetryKeyMatches += from.rootRetryKeyMatches;
	into.rootRetryKeyMismatches += from.rootRetryKeyMismatches;
	into.smallWeightOps += from.smallWeightOps; into.bigWeightPromotions += from.bigWeightPromotions;
	into.maxWeightBits = std::max(into.maxWeightBits, from.maxWeightBits);
	into.chanceMassMismatches += from.chanceMassMismatches;
	into.beliefNodes += from.beliefNodes; into.informationSets += from.informationSets;
	into.strategyFusionPrevented += from.strategyFusionPrevented;
	into.illegalInformationSetSplits += from.illegalInformationSetSplits;
	into.probabilityExact = into.probabilityExact && from.probabilityExact;
	into.informationSetSafe = into.informationSetSafe && from.informationSetSafe;
  if (!from.lastException.empty()) into.lastException = from.lastException;
  if (from.lastPendingDetail != 0) into.lastPendingDetail = from.lastPendingDetail;
  if (from.lastPendingPlayer >= 0) into.lastPendingPlayer = from.lastPendingPlayer;
  if (from.lastPendingEffectCardId != 0) into.lastPendingEffectCardId = from.lastPendingEffectCardId;
  if (from.lastPendingEffectPlayer >= 0) into.lastPendingEffectPlayer = from.lastPendingEffectPlayer;
  if (from.lastPendingNullCount != 0) into.lastPendingNullCount = from.lastPendingNullCount;
  into.lastPendingDeckUnknown = into.lastPendingDeckUnknown || from.lastPendingDeckUnknown;
}

struct ExactTurnSession {
  struct Worker {
    Game game;
    std::unique_ptr<ExactPlanner> planner;
    std::vector<ExactScore> actions;
  };

  std::unique_ptr<Game> game;
  std::unique_ptr<ExactPlanner> planner;
	std::unique_ptr<Worker> alternateWorker;
  ExactMetrics discardedMetrics;
  int turn = -1;
  int actor = -1;
	ExactDecision lastDecision;
	std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

  ExactDecision begin(const State& source, const int* deck, const int* handValues, int deckCount,
      const int* opponentDeck, int opponentDeckCount, int budgetMilliseconds,
      std::shared_ptr<const ExactCpuEvaluator> evaluator = nullptr) {
    turn = source.turn; actor = source.selectPlayer;
		started = std::chrono::steady_clock::now();
    ExactDecision decision;
    if (source.selectMin == 1 && source.selectMax == 1 && source.options.size() > 1) {
		auto sharedTable = std::make_shared<ExactSharedTransposition>();
		auto absoluteDeadline = std::chrono::steady_clock::now()
			+ std::chrono::milliseconds(std::max(1, budgetMilliseconds));
		std::vector<int> representative(source.options.size());
		std::unordered_map<std::string, int, ExactStringHasher> successorRepresentative;
		for (int option = 0; option < (int)source.options.size(); ++option) {
			Game probeGame = *source.game;
			State probeState = source; probeState.game = &probeGame;
			ExactPlanner probe(deck, handValues, deckCount, 1,
				opponentDeckCount == 0 ? nullptr : opponentDeck, opponentDeckCount, nullptr, evaluator);
			std::string key = probe.canonicalRootSuccessor(probeState, option);
			auto [found, inserted] = successorRepresentative.emplace(std::move(key), option);
			representative[option] = inserted ? option : found->second;
		}
		std::vector<int> orderedOptions;
		for (int option = 0; option < (int)source.options.size(); ++option)
			if (representative[option] == option && source.options[option].type == SelectOptionType::End) orderedOptions.push_back(option);
		for (int option = 0; option < (int)source.options.size(); ++option)
			if (representative[option] == option && source.options[option].type != SelectOptionType::End) orderedOptions.push_back(option);
      std::array<std::unique_ptr<Worker>, 2> workers;
      auto run = [&](int parity) {
        auto output = std::make_unique<Worker>();
        output->game = *source.game;
        output->planner = std::make_unique<ExactPlanner>(deck, handValues, deckCount, budgetMilliseconds,
          opponentDeckCount == 0 ? nullptr : opponentDeck, opponentDeckCount, sharedTable, evaluator);
		output->actions.resize(source.options.size());
		std::vector<int> assigned;
		for (int position = parity; position < (int)orderedOptions.size(); position += 2)
			assigned.push_back(orderedOptions[position]);
		for (int option : assigned) output->actions[option].action = { option };
		if (source.options.size() <= 2) {
			for (int option : assigned) {
				State local = source; local.game = &output->game;
				output->actions[option] = output->planner->evaluateRootAction(local, option).score;
			}
		} else {
			int fairShare = std::max(50, budgetMilliseconds / std::max(1, (int)assigned.size()));
			int firstRoundSlice = std::min(1'000, fairShare);
			const int sliceMilliseconds = std::min(60'000, fairShare);
			bool firstRound = true;
			while (std::chrono::steady_clock::now() < absoluteDeadline) {
				bool pending = false, attempted = false, resourceStopped = false;
				for (int option : assigned) {
					if (output->actions[option].certified) continue;
					pending = true;
					auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
						absoluteDeadline - std::chrono::steady_clock::now()).count();
					if (remaining <= 0) break;
					int slice = (int)std::min<long long>(remaining, firstRound ? firstRoundSlice : sliceMilliseconds);
					output->planner->setBudgetMilliseconds(std::max(1, slice));
					State local = source; local.game = &output->game;
					ExactScore fresh = output->planner->evaluateRootAction(local, option).score;
					ExactScore& saved = output->actions[option];
					if (saved.action.empty()) saved = fresh;
					else {
						if (ExactCompare(fresh.lower, saved.lower) > 0) saved.lower = fresh.lower;
						if (ExactCompare(fresh.upper, saved.upper) < 0) saved.upper = fresh.upper;
						saved.certified = ExactCompare(saved.lower, saved.upper) == 0;
					}
					attempted = true;
					if (output->planner->resourceStopped()) { resourceStopped = true; break; }
					if (!firstRound) break; // finish one representative before starting the next
				}
				firstRound = false;
				if (!pending || !attempted || resourceStopped) break;
			}
		}
        return output;
      };
      auto future0 = std::async(std::launch::async, run, 0);
      auto future1 = std::async(std::launch::async, run, 1);
      workers[0] = future0.get(); workers[1] = future1.get();
      bool first = true, allCertified = true;
      ExactFraction maxUpper = ExactFraction::integer(-100'000'000);
      int selectedWorker = 0;
	  std::unordered_map<int, ExactScore> representativeScores;
      for (int wi = 0; wi < 2; ++wi) {
        for (const ExactScore& item : workers[wi]->actions) {
		  if (item.action.empty()) continue;
		  representativeScores[item.action.front()] = item;
		  decision.rootActions.push_back({ item.action, item.lower, item.upper, item.certified });
          if (first || ExactCompare(item.lower, decision.score.lower) > 0
              || (ExactCompare(item.lower, decision.score.lower) == 0 && item.action < decision.score.action)) {
            decision.score = item; selectedWorker = wi; first = false;
          }
          if (ExactCompare(item.upper, maxUpper) > 0) maxUpper = item.upper;
          allCertified = allCertified && item.certified;
        }
        MergeExactMetrics(decision.metrics, workers[wi]->planner->currentMetrics());
      }
	  for (int option = 0; option < (int)representative.size(); ++option) {
		if (representative[option] == option) continue;
		auto found = representativeScores.find(representative[option]);
		if (found == representativeScores.end()) continue;
		ExactScore alias = found->second; alias.action = { option };
		decision.rootActions.push_back({ alias.action, alias.lower, alias.upper, alias.certified });
		decision.metrics.successorMerges++;
		decision.metrics.largestEquivalenceClass = std::max<unsigned long long>(decision.metrics.largestEquivalenceClass, 2);
	  }
      decision.metrics.rootWorkers = 2;
      if (!first) {
        decision.score.upper = maxUpper;
        decision.score.certified = allCertified && ExactCompare(decision.score.lower, decision.score.upper) == 0;
      }
      int other = 1 - selectedWorker;
      discardedMetrics = workers[other]->planner->currentMetrics();
	  alternateWorker = std::move(workers[other]);
      game = std::make_unique<Game>(std::move(workers[selectedWorker]->game));
      planner = std::move(workers[selectedWorker]->planner);
    } else {
      game = std::make_unique<Game>(*source.game);
      planner = std::make_unique<ExactPlanner>(deck, handValues, deckCount, budgetMilliseconds,
        opponentDeckCount == 0 ? nullptr : opponentDeck, opponentDeckCount, nullptr, evaluator);
      State local = source; local.game = game.get();
      decision = planner->decide(local);
    }
		lastDecision = decision;
    return decision;
  }

  ExactDecision advance(const State& source, int budgetMilliseconds) {
    ExactDecision decision;
    if (source.turn != turn || source.selectPlayer != actor || planner == nullptr) {
      decision.metrics = discardedMetrics;
      decision.metrics.sessionInvalidations++;
      return decision;
    }
    State local = source; local.game = game.get();
	bool alternatePolicyHit = false;
    if (!planner->lookupPolicy(local, decision)) {
	  if (alternateWorker != nullptr && alternateWorker->planner != nullptr) {
		State alternate = source; alternate.game = &alternateWorker->game;
		alternatePolicyHit = alternateWorker->planner->lookupPolicy(alternate, decision);
	  }
	  if (!alternatePolicyHit) decision = planner->resume(local, budgetMilliseconds);
    }
	ExactMetrics combined = alternatePolicyHit ? planner->currentMetrics() : discardedMetrics;
    MergeExactMetrics(combined, decision.metrics);
    combined.rootWorkers = 2;
    decision.metrics = combined;
		lastDecision = decision;
    return decision;
  }

	long long elapsedMilliseconds() const {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started).count();
	}
};

static const char8_t* ExactProgressJson(ApiData* data, long long sessionId, const ExactTurnSession& session) {
  const ExactMetrics& metrics = session.lastDecision.metrics;
  JsonBuilder& j = data->jsonBuilder;
  j.clear(); j.append('{');
  j.appendKey("sessionId"); AppendLongLong(j, sessionId);
  j.appendCommaKeyValue("turn", session.turn);
  j.appendCommaKeyValue("actor", session.actor);
  j.appendCommaKeyValue("currentRootAction", metrics.currentRootAction);
  j.appendCommaKeyValue("maxDepth", metrics.maxDepth);
  j.appendCommaKey("expandedNodes"); AppendUnsignedLongLong(j, metrics.expanded);
  j.appendCommaKey("resumedActionCount"); AppendUnsignedLongLong(j, metrics.resumedActionCount);
  j.appendCommaKey("resumedChanceMass"); AppendUnsignedLongLong(j, metrics.resumedChanceMass);
  j.appendCommaKey("largestEquivalenceClass"); AppendUnsignedLongLong(j, metrics.largestEquivalenceClass);
  j.appendCommaKey("canonicalStateMerges"); AppendUnsignedLongLong(j, metrics.canonicalStateMerges);
  j.appendCommaKey("successorMerges"); AppendUnsignedLongLong(j, metrics.successorMerges);
  j.appendCommaKey("distributionMerges"); AppendUnsignedLongLong(j, metrics.distributionMerges);
  j.appendCommaKey("sessionBytes"); AppendUnsignedLongLong(j, metrics.sessionBytes);
	 j.appendCommaKey("peakRssBytes"); AppendUnsignedLongLong(j, metrics.peakRssBytes);
	 j.appendCommaKeyValue("memoryLimitReached", metrics.memoryLimitReached);
  j.appendCommaKey("elapsedMilliseconds"); AppendLongLong(j, session.elapsedMilliseconds());
  j.appendCommaKeyValue("certified", session.lastDecision.score.certified);
  j.appendCommaKeyValue("probabilityExact", metrics.probabilityExact);
  j.appendCommaKeyValue("informationSetSafe", metrics.informationSetSafe);
  j.appendCommaKey("beliefNodes"); AppendUnsignedLongLong(j, metrics.beliefNodes);
  j.appendCommaKey("informationSets"); AppendUnsignedLongLong(j, metrics.informationSets);
  j.appendCommaKey("bigWeightPromotions"); AppendUnsignedLongLong(j, metrics.bigWeightPromotions);
  j.appendCommaKeyValue("maxWeightBits", (int)metrics.maxWeightBits);
  j.append('}');
  return j.buf.c_str();
}

static std::mutex ExactSessionMutex;
static std::unordered_map<ApiData*, std::unordered_map<long long, std::unique_ptr<ExactTurnSession>>> ExactSessions;
static std::atomic<long long> NextExactSessionId{ 1 };

extern "C" {

  GAME_API void GameInitialize() {
    InitializeAll();
  }

  GAME_API StartData BattleStart(int* cards) {
    return ApiBattleStart(cards);
  }

  GAME_API StartData BattleStartSeeded(int* cards, unsigned int seed) {
    return ApiBattleStartSeeded(cards, seed, true);
  }

  GAME_API StartData BattleStartOrdered(int* cards, unsigned int seed) {
    return ApiBattleStartOrdered(cards, seed);
  }

  GAME_API ApiData* AgentStart() {
    return ApiAgentStart();
  }

  GAME_API void BattleFinish(ApiData* data) {
    {
      std::lock_guard<std::mutex> lock(ExactSessionMutex);
      ExactSessions.erase(data);
    }
    return ApiBattleFinish(data);
  }

  GAME_API SerialData GetBattleData(ApiData* data) {
    if (data->apiDataType != 1) {
      return {};
    }

    if (data->preGetSelectCount != data->selectCount) {
      const State& state = data->state;
      int index = std::max(state.logIndex[0], state.logIndex[1]);
      const std::vector<int>* selected = nullptr;
      if (data->visData.size() > 0) {
        selected = &data->selected;
      }
      ToJsonVis(data->state, data->jsonBuilder, index, selected);
      data->visData.push_back(data->jsonBuilder.buf);
      data->preGetSelectCount = data->selectCount;
    }

    return ApiGetBattleData(data);
  }

  GAME_API int Select(ApiData* data, int* select, int selectCount) {
    if (data->apiDataType != 1) {
      return 30;
    }
    return ApiSelect(data, select, selectCount);
  }

  GAME_API const char8_t* VisualizeData(ApiData* data) {
    if (data->apiDataType != 1) {
      return nullptr;
    }

    ApiVisualizeData(data->jsonBuilder, data->visData);
    
    return data->jsonBuilder.buf.c_str();
  }

  GAME_API const char8_t* SearchBegin(ApiData* data, const char* serialized, int count, int* myDeck, int* myPrize, int* enemyDeck, int* enemyPrize, int* enemyHand, int* enemyActive, int manualCoin) {
    SearchInfo si;
    if (data->apiDataType != 2) {
      si = SearchInfo::error(30);
    } else {
      SetBattleData(data, serialized, count);

      try {
        const State& state = data->state;
        int myIndex = state.selectPlayer;

        int error = 0;
        SearchStartConfig config;
        if (!state.selectDeck) {
          CopyIdPtr(myDeck, config.myDeck, state.players[myIndex].deck.size(), error);
        }
        CopyIdPtr(myPrize, config.myPrize, state.players[myIndex].prize.size(), error);
        CopyIdPtr(enemyDeck, config.enemyDeck, state.players[1 - myIndex].deck.size(), error);
        CopyIdPtr(enemyPrize, config.enemyPrize, state.players[1 - myIndex].prize.size(), error);
        CopyIdPtr(enemyHand, config.enemyHand, state.players[1 - myIndex].hand.size(), error);
        if (IsActiveNull(state, 1 - myIndex)) {
          CopyIdPtr(enemyActive, config.enemyActive, state.players[1 - myIndex].active.size(), error);
        }
        config.manualCoin = (bool)manualCoin;

        if (error) {
          si = SearchInfo::error(error);
        } else {
          si = ApiSearchBegin(data, config);
        }

      } catch (...) {
        si = SearchInfo::error(99);
      }
    }
    return JsonResult(data, si);
  }

  GAME_API const char8_t* SearchStep(ApiData* data, long long searchId, int* select, int selectCount) {
    SearchInfo si;
    if (data->apiDataType != 2) {
      si = SearchInfo::error(30);
    } else {
      try {
        si = ApiSearchStep(data, searchId, select, selectCount);
      } catch (...) {
        si = SearchInfo::error(99);
      }
    }
    return JsonResult(data, si);
  }

  GAME_API const char8_t* ExactDecide(ApiData* data, const char* serialized, int count,
      int* deck, int* handValues, int deckCount, int budgetMilliseconds) {
    if (data->apiDataType != 2 || deckCount <= 0 || deckCount > DECK_SIZE) {
      data->jsonBuilder.clear();
      data->jsonBuilder.appendStr("{\"error\":30}");
      return data->jsonBuilder.buf.c_str();
    }
    try {
      SetBattleData(data, serialized, count);
      ExactDecision decision;
      const State& root = data->state;
      if (root.selectMin == 1 && root.selectMax == 1 && root.options.size() > 1) {
        struct WorkerResult { std::vector<ExactScore> actions; ExactMetrics metrics; };
        auto worker = [&](int parity) {
          WorkerResult output;
          Game game = data->game;
          ExactPlanner planner(deck, handValues, deckCount, budgetMilliseconds,
            nullptr, 0, nullptr, data->exactEvaluator);
          for (int option = parity; option < (int)root.options.size(); option += 2) {
            State local = root; local.game = &game;
            ExactDecision item = planner.evaluateRootAction(local, option);
            output.actions.push_back(item.score);
            output.metrics = item.metrics;
          }
          return output;
        };
        auto future0 = std::async(std::launch::async, worker, 0);
        auto future1 = std::async(std::launch::async, worker, 1);
        WorkerResult results[2] = { future0.get(), future1.get() };
        decision.metrics.rootWorkers = 2;
        bool first = true, allCertified = true;
        ExactFraction maxUpper = ExactFraction::integer(-100'000'000);
        for (const WorkerResult& wr : results) {
          for (const ExactScore& item : wr.actions) {
          if (first || ExactCompare(item.lower, decision.score.lower) > 0
              || (ExactCompare(item.lower, decision.score.lower) == 0 && item.action < decision.score.action)) {
            decision.score = item; first = false;
          }
          if (ExactCompare(item.upper, maxUpper) > 0) maxUpper = item.upper;
          allCertified = allCertified && item.certified;
          }
          MergeExactMetrics(decision.metrics, wr.metrics);
        }
        if (first) {
          decision.score = {};
        } else {
          decision.score.upper = maxUpper;
          decision.score.certified = allCertified && ExactCompare(decision.score.lower, decision.score.upper) == 0;
        }
      } else {
        ExactPlanner planner(deck, handValues, deckCount, budgetMilliseconds,
          nullptr, 0, nullptr, data->exactEvaluator);
        decision = planner.decide(data->state);
      }
      return ExactDecisionJson(data, decision);
    } catch (...) {
      data->jsonBuilder.clear();
      data->jsonBuilder.appendStr("{\"error\":99}");
      return data->jsonBuilder.buf.c_str();
    }
  }

  GAME_API const char8_t* ExactEvaluateAction(ApiData* data, const char* serialized, int count,
      int* deck, int* handValues, int deckCount, int budgetMilliseconds, int optionIndex) {
    if (data->apiDataType != 2 || deckCount <= 0 || deckCount > DECK_SIZE) {
      data->jsonBuilder.clear(); data->jsonBuilder.appendStr("{\"error\":30}");
      return data->jsonBuilder.buf.c_str();
    }
    try {
      SetBattleData(data, serialized, count);
      ExactPlanner planner(deck, handValues, deckCount, budgetMilliseconds,
        nullptr, 0, nullptr, data->exactEvaluator);
      ExactDecision decision = planner.evaluateRootAction(data->state, optionIndex);
      return ExactDecisionJson(data, decision);
    } catch (...) {
      data->jsonBuilder.clear(); data->jsonBuilder.appendStr("{\"error\":99}");
      return data->jsonBuilder.buf.c_str();
    }
  }

  GAME_API const char8_t* ExactDecideV2(ApiData* data, const char* serialized, int count,
      int* deck, int* handValues, int deckCount, int* opponentDeck, int opponentDeckCount,
      int budgetMilliseconds) {
    if (data->apiDataType != 2 || deckCount <= 0 || deckCount > DECK_SIZE
        || opponentDeckCount < 0 || opponentDeckCount > DECK_SIZE) {
      data->jsonBuilder.clear(); data->jsonBuilder.appendStr("{\"error\":30}");
      return data->jsonBuilder.buf.c_str();
    }
    try {
      SetBattleData(data, serialized, count);
      ExactPlanner planner(deck, handValues, deckCount, budgetMilliseconds,
          opponentDeckCount == 0 ? nullptr : opponentDeck, opponentDeckCount,
          nullptr, data->exactEvaluator);
      return ExactDecisionJson(data, planner.decide(data->state));
    } catch (...) {
      data->jsonBuilder.clear(); data->jsonBuilder.appendStr("{\"error\":99}");
      return data->jsonBuilder.buf.c_str();
    }
  }

  GAME_API const char8_t* ExactTurnBegin(ApiData* data, const char* serialized, int count,
      int* deck, int* handValues, int deckCount, int* opponentDeck, int opponentDeckCount,
      int budgetMilliseconds) {
    if (data->apiDataType != 2 || deckCount <= 0 || deckCount > DECK_SIZE
        || opponentDeckCount < 0 || opponentDeckCount > DECK_SIZE) {
      data->jsonBuilder.clear(); data->jsonBuilder.appendStr("{\"error\":30}");
      return data->jsonBuilder.buf.c_str();
    }
    try {
      SetBattleData(data, serialized, count);
      auto session = std::make_unique<ExactTurnSession>();
      ExactDecision decision = session->begin(data->state, deck, handValues, deckCount,
        opponentDeck, opponentDeckCount, budgetMilliseconds, data->exactEvaluator);
      long long id = NextExactSessionId.fetch_add(1);
      {
        std::lock_guard<std::mutex> lock(ExactSessionMutex);
        ExactSessions[data].clear();
        ExactSessions[data][id] = std::move(session);
      }
      return ExactDecisionJson(data, decision, id);
    } catch (...) {
      data->jsonBuilder.clear(); data->jsonBuilder.appendStr("{\"error\":99}");
      return data->jsonBuilder.buf.c_str();
    }
  }

  GAME_API const char8_t* ExactTurnAdvance(ApiData* data, long long sessionId,
      const char* serialized, int count, int budgetMilliseconds) {
    if (data->apiDataType != 2) {
      data->jsonBuilder.clear(); data->jsonBuilder.appendStr("{\"error\":30}");
      return data->jsonBuilder.buf.c_str();
    }
    try {
      SetBattleData(data, serialized, count);
      ExactTurnSession* session = nullptr;
      {
        std::lock_guard<std::mutex> lock(ExactSessionMutex);
        auto owner = ExactSessions.find(data);
        if (owner != ExactSessions.end()) {
          auto found = owner->second.find(sessionId);
          if (found != owner->second.end()) session = found->second.get();
        }
      }
      if (session == nullptr) {
        data->jsonBuilder.clear(); data->jsonBuilder.appendStr("{\"error\":31}");
        return data->jsonBuilder.buf.c_str();
      }
      return ExactDecisionJson(data, session->advance(data->state, budgetMilliseconds), sessionId);
    } catch (...) {
      data->jsonBuilder.clear(); data->jsonBuilder.appendStr("{\"error\":99}");
      return data->jsonBuilder.buf.c_str();
    }
  }

  GAME_API const char8_t* ExactTurnProgress(ApiData* data, long long sessionId) {
    if (data->apiDataType != 2) {
      data->jsonBuilder.clear(); data->jsonBuilder.appendStr("{\"error\":30}");
      return data->jsonBuilder.buf.c_str();
    }
    std::lock_guard<std::mutex> lock(ExactSessionMutex);
    auto owner = ExactSessions.find(data);
    if (owner == ExactSessions.end() || !owner->second.contains(sessionId)) {
      data->jsonBuilder.clear(); data->jsonBuilder.appendStr("{\"error\":31}");
      return data->jsonBuilder.buf.c_str();
    }
    return ExactProgressJson(data, sessionId, *owner->second.at(sessionId));
  }

  GAME_API void ExactTurnRelease(ApiData* data, long long sessionId) {
    std::lock_guard<std::mutex> lock(ExactSessionMutex);
    auto owner = ExactSessions.find(data);
    if (owner == ExactSessions.end()) return;
    owner->second.erase(sessionId);
    if (owner->second.empty()) ExactSessions.erase(owner);
  }

  GAME_API void SearchEnd(ApiData* data) {
    if (data->apiDataType != 2) {
      return;
    }
    ApiSearchEnd(data);
  }

  GAME_API void SearchRelease(ApiData* data, long long searchId) {
    if (data->apiDataType != 2) {
      return;
    }
    ApiSearchRelease(data, searchId);
  }

  GAME_API const char8_t* AllCard() {
    if (AllCardJson.buf.empty()) {
      ApiAllCard(AllCardJson);
    }
    return AllCardJson.buf.c_str();
  }

  GAME_API const char8_t* AllAttack() {
    if (AllAttackJson.buf.empty()) {
      ApiAllAttack(AllAttackJson);
    }
    return AllAttackJson.buf.c_str();
  }
}
