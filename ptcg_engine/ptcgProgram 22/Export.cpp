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

static const char8_t* ExactDecisionJson(ApiData* data, const ExactDecision& decision, long long sessionId = -1) {
  JsonBuilder& j = data->jsonBuilder;
  j.clear(); j.append('{');
  j.appendKey("selected"); j.append('[');
  for (int i : range(decision.score.action)) { j.comma(i); j.append(decision.score.action[i]); }
  j.append(']');
  j.appendCommaKey("lowerNumerator"); AppendLongLong(j, decision.score.lower.numerator);
  j.appendCommaKey("lowerDenominator"); AppendUnsignedLongLong(j, decision.score.lower.denominator);
  j.appendCommaKey("upperNumerator"); AppendLongLong(j, decision.score.upper.numerator);
  j.appendCommaKey("upperDenominator"); AppendUnsignedLongLong(j, decision.score.upper.denominator);
  j.appendCommaKeyValue("certified", decision.score.certified);
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
  ExactMetrics discardedMetrics;
  int turn = -1;
  int actor = -1;

  ExactDecision begin(const State& source, const int* deck, const int* handValues, int deckCount,
      const int* opponentDeck, int opponentDeckCount, int budgetMilliseconds) {
    turn = source.turn; actor = source.selectPlayer;
    ExactDecision decision;
    if (source.selectMin == 1 && source.selectMax == 1 && source.options.size() > 1) {
      std::array<std::unique_ptr<Worker>, 2> workers;
      auto run = [&](int parity) {
        auto output = std::make_unique<Worker>();
        output->game = *source.game;
        output->planner = std::make_unique<ExactPlanner>(deck, handValues, deckCount, budgetMilliseconds,
          opponentDeckCount == 0 ? nullptr : opponentDeck, opponentDeckCount);
        for (int option = parity; option < (int)source.options.size(); option += 2) {
          State local = source; local.game = &output->game;
          output->actions.push_back(output->planner->evaluateRootAction(local, option).score);
        }
        return output;
      };
      auto future0 = std::async(std::launch::async, run, 0);
      auto future1 = std::async(std::launch::async, run, 1);
      workers[0] = future0.get(); workers[1] = future1.get();
      bool first = true, allCertified = true;
      ExactFraction maxUpper = ExactFraction::integer(-100'000'000);
      int selectedWorker = 0;
      for (int wi = 0; wi < 2; ++wi) {
        for (const ExactScore& item : workers[wi]->actions) {
          if (first || ExactCompare(item.lower, decision.score.lower) > 0
              || (ExactCompare(item.lower, decision.score.lower) == 0 && item.action < decision.score.action)) {
            decision.score = item; selectedWorker = wi; first = false;
          }
          if (ExactCompare(item.upper, maxUpper) > 0) maxUpper = item.upper;
          allCertified = allCertified && item.certified;
        }
        MergeExactMetrics(decision.metrics, workers[wi]->planner->currentMetrics());
      }
      decision.metrics.rootWorkers = 2;
      if (!first) {
        decision.score.upper = maxUpper;
        decision.score.certified = allCertified && ExactCompare(decision.score.lower, decision.score.upper) == 0;
      }
      int other = 1 - selectedWorker;
      discardedMetrics = workers[other]->planner->currentMetrics();
      discardedMetrics.sessionBytes = 0;
      game = std::make_unique<Game>(std::move(workers[selectedWorker]->game));
      planner = std::move(workers[selectedWorker]->planner);
      decision.metrics.sessionBytes = planner->currentMetrics().sessionBytes;
    } else {
      game = std::make_unique<Game>(*source.game);
      planner = std::make_unique<ExactPlanner>(deck, handValues, deckCount, budgetMilliseconds,
        opponentDeckCount == 0 ? nullptr : opponentDeck, opponentDeckCount);
      State local = source; local.game = game.get();
      decision = planner->decide(local);
    }
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
    if (!planner->lookupPolicy(local, decision)) {
      decision = planner->resume(local, budgetMilliseconds);
    }
    ExactMetrics combined = discardedMetrics;
    MergeExactMetrics(combined, decision.metrics);
    combined.rootWorkers = 2;
    decision.metrics = combined;
    return decision;
  }
};

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
          ExactPlanner planner(deck, handValues, deckCount, budgetMilliseconds);
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
          decision.metrics.expanded += wr.metrics.expanded;
          decision.metrics.merged += wr.metrics.merged;
          decision.metrics.leaves += wr.metrics.leaves;
          decision.metrics.opaque += wr.metrics.opaque;
          decision.metrics.exceptions += wr.metrics.exceptions;
          decision.metrics.unknownOpponentList += wr.metrics.unknownOpponentList;
          decision.metrics.unsupportedConcreteReference += wr.metrics.unsupportedConcreteReference;
          decision.metrics.interruptedTransition += wr.metrics.interruptedTransition;
          decision.metrics.rawOutcomes += wr.metrics.rawOutcomes;
          decision.metrics.groupedOutcomes += wr.metrics.groupedOutcomes;
          decision.metrics.depthLimitNodes += wr.metrics.depthLimitNodes;
          decision.metrics.maxDepth = std::max(decision.metrics.maxDepth, wr.metrics.maxDepth);
          if (wr.metrics.lastDepthSelectType != 0) decision.metrics.lastDepthSelectType = wr.metrics.lastDepthSelectType;
          if (wr.metrics.lastDepthTurnActionCount != 0) decision.metrics.lastDepthTurnActionCount = wr.metrics.lastDepthTurnActionCount;
          decision.metrics.timedOut = decision.metrics.timedOut || wr.metrics.timedOut;
          decision.metrics.arithmeticOverflow = decision.metrics.arithmeticOverflow || wr.metrics.arithmeticOverflow;
          if (!wr.metrics.lastException.empty()) decision.metrics.lastException = wr.metrics.lastException;
          if (wr.metrics.lastPendingDetail != 0) decision.metrics.lastPendingDetail = wr.metrics.lastPendingDetail;
          if (wr.metrics.lastPendingPlayer >= 0) decision.metrics.lastPendingPlayer = wr.metrics.lastPendingPlayer;
          if (wr.metrics.lastPendingEffectCardId != 0) decision.metrics.lastPendingEffectCardId = wr.metrics.lastPendingEffectCardId;
          if (wr.metrics.lastPendingEffectPlayer >= 0) decision.metrics.lastPendingEffectPlayer = wr.metrics.lastPendingEffectPlayer;
          if (wr.metrics.lastPendingNullCount != 0) decision.metrics.lastPendingNullCount = wr.metrics.lastPendingNullCount;
          decision.metrics.lastPendingDeckUnknown = decision.metrics.lastPendingDeckUnknown || wr.metrics.lastPendingDeckUnknown;
        }
        if (first) {
          decision.score = {};
        } else {
          decision.score.upper = maxUpper;
          decision.score.certified = allCertified && ExactCompare(decision.score.lower, decision.score.upper) == 0;
        }
      } else {
        ExactPlanner planner(deck, handValues, deckCount, budgetMilliseconds);
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
      ExactPlanner planner(deck, handValues, deckCount, budgetMilliseconds);
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
          opponentDeckCount == 0 ? nullptr : opponentDeck, opponentDeckCount);
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
        opponentDeck, opponentDeckCount, budgetMilliseconds);
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
