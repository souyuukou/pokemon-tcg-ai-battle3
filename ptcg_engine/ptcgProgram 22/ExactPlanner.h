#pragma once

#include "ExactSearchHooks.h"
#include "ExactCanonicalState.h"
#include "ExactCpuEvaluator.h"
#include "ExactBigRational.h"

#include <chrono>
#include <numeric>
#include <memory>
#include <mutex>
#include <array>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#else
#include <sys/resource.h>
#endif

inline unsigned long long ExactResidentBytes() {
#ifdef _WIN32
	PROCESS_MEMORY_COUNTERS_EX counters{};
	if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&counters, sizeof(counters)))
		return (unsigned long long)counters.WorkingSetSize;
	return 0;
#else
	struct rusage usage{};
	if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
	return (unsigned long long)usage.ru_maxrss * 1024ULL;
#endif
}

struct ExactFraction {
	long long numerator = 0;
	unsigned long long denominator = 1;
	bool valid = true;
	std::shared_ptr<const ExactBigRational> big;

	static ExactFraction integer(long long value) { return { value, 1, true }; }

	void normalize() {
		if (big) return;
		if (!valid || denominator == 0) { valid = false; return; }
		unsigned long long magnitude = numerator < 0 ? (unsigned long long)(-(numerator + 1)) + 1 : (unsigned long long)numerator;
		auto g = std::gcd(magnitude, denominator);
		if (g > 1) { numerator /= (long long)g; denominator /= g; }
	}

	static bool checkedMul(long long value, unsigned long long factor, long long& out) {
		if (factor == 0 || value == 0) { out = 0; return true; }
		if (factor > (unsigned long long)std::numeric_limits<long long>::max()) return false;
		long long f = (long long)factor;
		if (value > 0 && value > std::numeric_limits<long long>::max() / f) return false;
		if (value < 0 && value < std::numeric_limits<long long>::min() / f) return false;
		out = value * f;
		return true;
	}

	static bool checkedMulU(unsigned long long a, unsigned long long b, unsigned long long& out) {
		if (a != 0 && b > std::numeric_limits<unsigned long long>::max() / a) return false;
		out = a * b; return true;
	}

	ExactFraction scaled(unsigned long long weight, unsigned long long total) const {
		if (!valid || total == 0) return { 0, 1, false };
		unsigned long long originalWeight = weight, originalTotal = total;
		if (big) {
			auto value = std::make_shared<ExactBigRational>(*big); value->scale(weight, total);
			ExactFraction out; out.big = std::move(value); return out;
		}
		ExactFraction result = *this;
		auto g1 = std::gcd(weight, total); weight /= g1; total /= g1;
		unsigned long long magnitude = result.numerator < 0 ? (unsigned long long)(-(result.numerator + 1)) + 1 : (unsigned long long)result.numerator;
		auto g2 = std::gcd(magnitude, total); result.numerator /= (long long)g2; total /= g2;
		auto g3 = std::gcd(result.denominator, weight); result.denominator /= g3; weight /= g3;
		long long n; unsigned long long d;
		if (!checkedMul(result.numerator, weight, n) || !checkedMulU(result.denominator, total, d)) {
			auto value = std::make_shared<ExactBigRational>(numerator, denominator); value->scale(originalWeight, originalTotal);
			ExactFraction out; out.big = std::move(value); return out;
		}
		result.numerator = n; result.denominator = d; result.normalize(); return result;
	}

	ExactFraction scaled(const ExactWeight& weight, const ExactWeight& total) const {
		if (!valid || total.zero()) return { 0, 1, false };
		if (weight.fitsUnsignedLongLong() && total.fitsUnsignedLongLong())
			return scaled(weight.unsignedLongLong(), total.unsignedLongLong());
		ExactBigRational value = big ? *big : ExactBigRational(numerator, denominator);
		value.scale(weight, total);
		ExactFraction out; out.big = std::make_shared<ExactBigRational>(std::move(value)); return out;
	}

	static ExactFraction add(const ExactFraction& a, const ExactFraction& b) {
		if (!a.valid || !b.valid) return { 0, 1, false };
		if (a.big || b.big) {
			ExactBigRational av = a.big ? *a.big : ExactBigRational(a.numerator, a.denominator);
			ExactBigRational bv = b.big ? *b.big : ExactBigRational(b.numerator, b.denominator);
			ExactFraction out; out.big = std::make_shared<ExactBigRational>(ExactBigRational::add(av, bv)); return out;
		}
		auto g = std::gcd(a.denominator, b.denominator);
		unsigned long long am = b.denominator / g, bm = a.denominator / g;
		long long an, bn;
		unsigned long long d;
		if (!checkedMul(a.numerator, am, an) || !checkedMul(b.numerator, bm, bn)) {
			ExactFraction out; out.big = std::make_shared<ExactBigRational>(ExactBigRational::add(
				ExactBigRational(a.numerator, a.denominator), ExactBigRational(b.numerator, b.denominator))); return out;
		}
		if ((bn > 0 && an > std::numeric_limits<long long>::max() - bn)
			|| (bn < 0 && an < std::numeric_limits<long long>::min() - bn)) {
			ExactFraction out; out.big = std::make_shared<ExactBigRational>(ExactBigRational::add(
				ExactBigRational(a.numerator, a.denominator), ExactBigRational(b.numerator, b.denominator))); return out;
		}
		if (!checkedMulU(a.denominator, am, d)) {
			ExactFraction out; out.big = std::make_shared<ExactBigRational>(ExactBigRational::add(
				ExactBigRational(a.numerator, a.denominator), ExactBigRational(b.numerator, b.denominator))); return out;
		}
		ExactFraction result{ an + bn, d, true }; result.normalize(); return result;
	}

	std::string numeratorText() const { return big ? big->numerator.text() : std::to_string(numerator); }
	std::string denominatorText() const { return big ? big->denominatorText() : std::to_string(denominator); }
};

inline int ExactCompare(const ExactFraction& a, const ExactFraction& b) {
	if (a.big || b.big) {
		ExactBigRational av = a.big ? *a.big : ExactBigRational(a.numerator, a.denominator);
		ExactBigRational bv = b.big ? *b.big : ExactBigRational(b.numerator, b.denominator);
		return ExactBigRational::compare(av, bv);
	}
	if (a.numerator < 0 && b.numerator >= 0) return -1;
	if (a.numerator >= 0 && b.numerator < 0) return 1;
	auto magnitude = [](long long n) { return n < 0 ? (unsigned long long)(-(n + 1)) + 1 : (unsigned long long)n; };
	unsigned long long ah, al, bh, bl;
#ifdef _MSC_VER
	al = _umul128(magnitude(a.numerator), b.denominator, &ah);
	bl = _umul128(magnitude(b.numerator), a.denominator, &bh);
#else
	unsigned __int128 av = (unsigned __int128)magnitude(a.numerator) * b.denominator;
	unsigned __int128 bv = (unsigned __int128)magnitude(b.numerator) * a.denominator;
	ah = (unsigned long long)(av >> 64); al = (unsigned long long)av;
	bh = (unsigned long long)(bv >> 64); bl = (unsigned long long)bv;
#endif
	int cmp = ah != bh ? (ah < bh ? -1 : 1) : (al == bl ? 0 : (al < bl ? -1 : 1));
	return a.numerator < 0 ? -cmp : cmp;
}

inline unsigned long long ExactRotl64(unsigned long long value, int bits) {
	return (value << bits) | (value >> (64 - bits));
}

inline unsigned long long ExactSipHash24(const std::string& bytes, unsigned long long k0, unsigned long long k1) {
	unsigned long long v0 = 0x736f6d6570736575ULL ^ k0, v1 = 0x646f72616e646f6dULL ^ k1;
	unsigned long long v2 = 0x6c7967656e657261ULL ^ k0, v3 = 0x7465646279746573ULL ^ k1;
	auto rounds = [&](int count) {
		for (int i = 0; i < count; ++i) {
			v0 += v1; v1 = ExactRotl64(v1, 13); v1 ^= v0; v0 = ExactRotl64(v0, 32);
			v2 += v3; v3 = ExactRotl64(v3, 16); v3 ^= v2;
			v0 += v3; v3 = ExactRotl64(v3, 21); v3 ^= v0;
			v2 += v1; v1 = ExactRotl64(v1, 17); v1 ^= v2; v2 = ExactRotl64(v2, 32);
		}
	};
	size_t offset = 0;
	for (; offset + 8 <= bytes.size(); offset += 8) {
		unsigned long long word = 0;
		for (int i = 0; i < 8; ++i) word |= (unsigned long long)(unsigned char)bytes[offset + i] << (8 * i);
		v3 ^= word; rounds(2); v0 ^= word;
	}
	unsigned long long tail = (unsigned long long)bytes.size() << 56;
	for (size_t i = offset; i < bytes.size(); ++i) tail |= (unsigned long long)(unsigned char)bytes[i] << (8 * (i - offset));
	v3 ^= tail; rounds(2); v0 ^= tail; v2 ^= 0xff; rounds(4);
	return v0 ^ v1 ^ v2 ^ v3;
}

struct ExactStringHasher {
	size_t operator()(const std::string& bytes) const noexcept {
		unsigned long long lo = ExactSipHash24(bytes, 0x7766554433221100ULL, 0xffeeddccbbaa9988ULL);
		unsigned long long hi = ExactSipHash24(bytes, 0x8899aabbccddeeffULL, 0x0011223344556677ULL);
		return (size_t)(lo ^ ExactRotl64(hi, 1));
	}
};

struct ExactScore {
	ExactFraction lower = ExactFraction::integer(-100'000'000);
	ExactFraction upper = ExactFraction::integer(100'000'000);
	std::vector<int> action;
	bool certified = false;
};

struct ExactRootActionValue {
	std::vector<int> action;
	ExactFraction lower;
	ExactFraction upper;
	bool certified = false;
};

// Completed exact entries are immutable, so they can safely be shared by the
// two root workers.  The full canonical byte string remains the equality key;
// SipHash only chooses a bucket/shard.
class ExactSharedTransposition {
public:
	bool find(const std::string& key, ExactScore& value) const {
		size_t hash = ExactStringHasher{}(key);
		Shard& shard = shards[hash & (ShardCount - 1)];
		std::lock_guard<std::mutex> lock(shard.mutex);
		auto found = shard.buckets.find(hash);
		if (found == shard.buckets.end()) return false;
		for (const Entry& entry : found->second) if (entry.key == key) {
			value = entry.value;
			return true;
		}
		return false;
	}

	bool store(std::string key, const ExactScore& value) {
		const size_t entryBytes = key.size() + sizeof(ExactScore) + 96;
		if (entryCount.load(std::memory_order_relaxed) >= MaxEntries) return false;
		size_t prior = byteCount.fetch_add(entryBytes, std::memory_order_relaxed);
		if (prior + entryBytes > MaxBytes) {
			byteCount.fetch_sub(entryBytes, std::memory_order_relaxed);
			return false;
		}
		size_t hash = ExactStringHasher{}(key);
		Shard& shard = shards[hash & (ShardCount - 1)];
		std::lock_guard<std::mutex> lock(shard.mutex);
		auto& bucket = shard.buckets[hash];
		for (const Entry& entry : bucket) if (entry.key == key) {
			byteCount.fetch_sub(entryBytes, std::memory_order_relaxed);
			return false;
		}
		bucket.push_back({ std::move(key), value });
		entryCount.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	size_t bytes() const { return byteCount.load(std::memory_order_relaxed); }
	size_t size() const { return entryCount.load(std::memory_order_relaxed); }

private:
	static constexpr size_t ShardCount = 64;
	static constexpr size_t MaxEntries = 500'000;
	static constexpr size_t MaxBytes = 400ULL * 1024ULL * 1024ULL;
	struct Entry { std::string key; ExactScore value; };
	struct Shard {
		mutable std::mutex mutex;
		std::unordered_map<size_t, std::vector<Entry>> buckets;
	};
	mutable std::array<Shard, ShardCount> shards;
	std::atomic<size_t> byteCount{ 0 };
	std::atomic<size_t> entryCount{ 0 };
};

struct ExactMetrics {
	unsigned long long expanded = 0;
	unsigned long long merged = 0;
	bool timedOut = false;
	bool arithmeticOverflow = false;
	unsigned long long leaves = 0;
	unsigned long long opaque = 0;
	unsigned long long exceptions = 0;
	std::string lastException;
	int lastPendingDetail = 0;
	unsigned long long unknownOpponentList = 0;
	unsigned long long unsupportedConcreteReference = 0;
	unsigned long long interruptedTransition = 0;
	unsigned long long rawOutcomes = 0;
	unsigned long long groupedOutcomes = 0;
	unsigned long long depthLimitNodes = 0;
	int maxDepth = 0;
	int lastDepthSelectType = 0;
	int lastDepthTurnActionCount = 0;
	int rootWorkers = 1;
	int lastPendingPlayer = -1;
	int lastPendingEffectCardId = 0;
	int lastPendingEffectPlayer = -1;
	int lastPendingNullCount = 0;
	bool lastPendingDeckUnknown = false;
	unsigned long long policyNodes = 0;
	unsigned long long policyHits = 0;
	unsigned long long policyMisses = 0;
	unsigned long long rerootCount = 0;
	unsigned long long resumedNodes = 0;
	unsigned long long avoidedExpandedNodes = 0;
	unsigned long long partialDecisionNodes = 0;
	unsigned long long partialChanceNodes = 0;
	unsigned long long semanticActionRemaps = 0;
	unsigned long long sessionInvalidations = 0;
	unsigned long long sessionBytes = 0;
	long long deadlineOverrunMs = 0;
	unsigned long long canonicalStateMerges = 0;
	unsigned long long successorMerges = 0;
	unsigned long long distributionMerges = 0;
	unsigned long long rootSharedTTHits = 0;
	unsigned long long beliefWorldsBefore = 0;
	unsigned long long beliefWorldsAfter = 0;
	unsigned long long largestEquivalenceClass = 0;
	unsigned long long resumedActionCount = 0;
	unsigned long long resumedChanceMass = 0;
	int currentRootAction = -1;
	unsigned long long peakRssBytes = 0;
	bool memoryLimitReached = false;
	unsigned long long partialDecisionHits = 0;
	unsigned long long partialChanceHits = 0;
	unsigned long long partialRevealHits = 0;
	unsigned long long enumeratedHiddenWorlds = 0;
	unsigned long long partialTableBytes = 0;
	unsigned long long rootRetryKeyMatches = 0;
	unsigned long long rootRetryKeyMismatches = 0;
	unsigned long long smallWeightOps = 0;
	unsigned long long bigWeightPromotions = 0;
	unsigned maxWeightBits = 0;
	unsigned long long chanceMassMismatches = 0;
	unsigned long long beliefNodes = 0;
	unsigned long long informationSets = 0;
	unsigned long long strategyFusionPrevented = 0;
	unsigned long long illegalInformationSetSplits = 0;
	unsigned long long attackPreviewExactCount = 0;
	unsigned long long attackPreviewUnavailableCount = 0;
	unsigned long long entityFeatureCount = 0;
	unsigned long long comboFeatureCount = 0;
	unsigned long long provisionalOpponentPolicyNodes = 0;
	bool hiddenInformationLeakDetected = false;
	bool probabilityExact = true;
	bool informationSetSafe = true;
};

struct ExactDecision {
	ExactScore score;
	ExactMetrics metrics;
	std::vector<ExactRootActionValue> rootActions;
};

// The lossless search/TT identity.  EvaluatorRecordV3 is deliberately not
// used here because its Q8 belief projection may merge distinct beliefs.
struct PlayerInformationStateV3 {
	static constexpr int SchemaVersion = 3;
	int observer = 0;
	std::uint64_t environmentPriorId = 0;
	int evaluatorSchema = 0;
	std::uint64_t evaluatorModel = 0;
	std::vector<std::string> normalizedWorlds;
	std::string canonicalKey() const {
		std::string key = "BELIEF-V3|INFORMATION-V3|CANONICAL-V1|RULES-V1|";
		auto append = [&](long long value) { key += std::to_string(value); key.push_back(';'); };
		append(observer); append((long long)environmentPriorId); append(evaluatorSchema); append((long long)evaluatorModel);
		for (const std::string& world : normalizedWorlds) { append((long long)world.size()); key += world; }
		return key;
	}
};

class ExactPlanner {
public:
	ExactPlanner(const int* deck, const int* handValues, int deckCount, int budgetMilliseconds,
		const int* opponentDeck = nullptr, int opponentDeckCount = 0,
		std::shared_ptr<ExactSharedTransposition> sharedTable = nullptr,
		std::shared_ptr<const ExactCpuEvaluator> cpuEvaluator = nullptr)
		: deadline(std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(1, budgetMilliseconds))),
		transposition(sharedTable ? sharedTable : std::make_shared<ExactSharedTransposition>()),
		usingSharedTable(sharedTable != nullptr), evaluator(std::move(cpuEvaluator)) {
		for (int i = 0; i < deckCount; ++i) {
			actorProfileCount[deck[i]]++;
			handValue[deck[i]] = handValues == nullptr ? 100 : handValues[i];
		}
		for (int i = 0; opponentDeck != nullptr && i < opponentDeckCount; ++i) opponentProfileCount[opponentDeck[i]]++;
		std::vector<int> priorCards;
		for (int i = 0; opponentDeck != nullptr && i < opponentDeckCount; ++i) priorCards.push_back(opponentDeck[i]);
		std::sort(priorCards.begin(), priorCards.end());
		environmentPriorId = 1469598103934665603ULL;
		for (int id : priorCards) for (int shift = 0; shift < 32; shift += 8) {
			environmentPriorId ^= (unsigned char)((unsigned)id >> shift); environmentPriorId *= 1099511628211ULL;
		}
	}

	void setBudgetMilliseconds(int budgetMilliseconds) {
		deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(1, budgetMilliseconds));
		metrics.timedOut = false;
	}

	ExactDecision decide(State root) {
		rootActionValues.clear();
		canonicalMainEnabled = root.selectType == SelectType::Main && root.options.size() > 2;
		nodeQuantumDeadline = std::numeric_limits<unsigned long long>::max();
		actor = root.selectPlayer;
		initializeHidden(root);
		root.game->config.manualCoin = true;
		root.exact.enabled = true;
		root.exact.actor = (signed char)actor;
		ExactDecision result;
		result.score = solveOwned(std::make_unique<State>(std::move(root)));
		result.rootActions = rootActionValues;
		applyEvaluatorSafety(result);
		result.metrics = metrics;
		return result;
	}

	ExactDecision evaluateRootAction(State root, int optionIndex) {
		rootActionValues.clear();
		canonicalMainEnabled = root.selectType == SelectType::Main && root.options.size() > 2;
		nodeQuantumDeadline = canonicalMainEnabled ? metrics.expanded + 20'000ULL
			: std::numeric_limits<unsigned long long>::max();
		metrics.currentRootAction = optionIndex;
		actor = root.selectPlayer;
		initializeHidden(root);
		root.game->config.manualCoin = true;
		root.exact.enabled = true;
		root.exact.actor = (signed char)actor;
		ExactDecision result;
		State child = root;
		if (optionIndex < 0 || optionIndex >= (int)root.options.size() || !advance(child, { optionIndex })) {
			result.score = unknown();
		} else {
			std::string retryKey = keyFor(child);
			auto prior = rootSuccessorKeys.find(optionIndex);
			if (prior == rootSuccessorKeys.end()) {
				rootSuccessorKeys.emplace(optionIndex, std::move(retryKey));
			}
			else if (prior->second == retryKey) metrics.rootRetryKeyMatches++;
			else metrics.rootRetryKeyMismatches++;
			result.score = child.exact.pending == ExactPendingType::RevealDeck
				? revealAndReplay(root, child.exact, { optionIndex }) : solveOwned(std::make_unique<State>(std::move(child)));
			result.score.action = { optionIndex };
		}
		applyEvaluatorSafety(result);
		result.metrics = metrics;
		result.rootActions.push_back({ { optionIndex }, result.score.lower, result.score.upper, result.score.certified });
		return result;
	}

	std::string canonicalRootSuccessor(State root, int optionIndex) {
		actor = root.selectPlayer;
		initializeHidden(root);
		root.game->config.manualCoin = true;
		root.exact.enabled = true;
		root.exact.actor = (signed char)actor;
		if (optionIndex < 0 || optionIndex >= (int)root.options.size() || !advance(root, { optionIndex })) return {};
		return keyFor(root);
	}

	// Re-root a completed turn policy at the currently observed decision.  The
	// key deliberately contains only information exposed by ToJsonApi; hidden
	// materializations therefore cannot leak into a later action.
	bool lookupPolicy(const State& observed, ExactDecision& result) {
		std::string key = observationKeyFor(observed);
		auto it = policy.find(key);
		if (it == policy.end() || it->second.empty()) { metrics.policyMisses++; return false; }
		const ExactPolicyEntry* selected = &it->second.front();
		for (const ExactPolicyEntry& candidate : it->second) {
			if (candidate.actionTokens != selected->actionTokens
				|| ExactCompare(candidate.score.lower, selected->score.lower) != 0
				|| ExactCompare(candidate.score.upper, selected->score.upper) != 0) {
				// The same observation was reached with a different belief.  Until
				// those beliefs are conditioned by a unique history, fail closed.
				metrics.policyMisses++; return false;
			}
		}
		std::vector<int> remapped;
		if (!remapAction(observed, selected->actionTokens, remapped)) { metrics.policyMisses++; return false; }
		result.score = selected->score;
		result.score.action = std::move(remapped);
		metrics.policyHits++;
		metrics.rerootCount++;
		metrics.semanticActionRemaps++;
		metrics.avoidedExpandedNodes += selected->subtreeExpanded;
		result.metrics = metrics;
		return selected->score.certified;
	}

	ExactDecision resume(State root, int budgetMilliseconds) {
		setBudgetMilliseconds(budgetMilliseconds);
		unsigned long long before = metrics.expanded;
		ExactDecision result = decide(std::move(root));
		metrics.resumedNodes += metrics.expanded - before;
		metrics.resumedActionCount++;
		result.metrics = metrics;
		return result;
	}

	const ExactMetrics& currentMetrics() const { return metrics; }
	bool resourceStopped() const { return metrics.memoryLimitReached; }

private:
	struct ExactKnowledgeState {
		// deckKnown[target] means this observer has seen the complete current
		// multiset of target's deck.  It is deliberately separate from the deck
		// profile supplied to closed-world validation.
		std::array<bool, 2> deckKnown{};
		std::string publicFacts;
		std::array<std::vector<int>, 2> knownTop;
		std::array<std::vector<int>, 2> knownBottom;
		std::array<std::map<int, int>, 2> knownDeckCounts;
		std::array<std::map<int, int>, 2> knownPrizeCounts;
		std::array<std::map<int, std::pair<int, int>>, 2> countBounds;
		std::uint64_t observationSequence = 0;
	};
	struct BeliefWorld {
		std::unique_ptr<State> state;
		ExactWeight weight;
		std::array<ExactKnowledgeState, 2> knowledge;
	};
	static void appendKnowledgeFact(ExactKnowledgeState& knowledge, char type, int cardId) {
		knowledge.publicFacts.push_back(type); appendSemantic(knowledge.publicFacts, cardId);
		knowledge.observationSequence++;
	}
	static void appendKnowledgeKey(std::string& key, const ExactKnowledgeState& knowledge) {
		for (bool known : knowledge.deckKnown) key.push_back(known ? '1' : '0');
		appendSemantic(key, knowledge.observationSequence);
		appendSemantic(key, (long long)knowledge.publicFacts.size()); key += knowledge.publicFacts;
		auto appendSequence = [&](const auto& lists) {
			for (const auto& list : lists) { appendSemantic(key, (long long)list.size()); for (int id : list) appendSemantic(key, id); }
		};
		auto appendMapArray = [&](const auto& maps) {
			for (const auto& values : maps) { appendSemantic(key, (long long)values.size());
				for (const auto& item : values) { appendSemantic(key, item.first);
					if constexpr (std::is_same_v<std::decay_t<decltype(item.second)>, std::pair<int, int>>) {
						appendSemantic(key, item.second.first); appendSemantic(key, item.second.second);
					} else appendSemantic(key, item.second);
				}
			}
		};
		appendSequence(knowledge.knownTop); appendSequence(knowledge.knownBottom);
		appendMapArray(knowledge.knownDeckCounts); appendMapArray(knowledge.knownPrizeCounts);
		appendMapArray(knowledge.countBounds);
	}
	std::array<ExactKnowledgeState, 2> initialKnowledge() const {
		std::array<ExactKnowledgeState, 2> result{};
		for (const auto& item : actorProfileCount)
			result[actor].countBounds[actor][item.first] = { item.second, item.second };
		int opponent = 1 - actor;
		for (const auto& item : opponentProfileCount)
			result[opponent].countBounds[opponent][item.first] = { item.second, item.second };
		return result;
	}
	struct ExactPolicyEntry {
		ExactScore score;
		std::vector<std::string> actionTokens;
		unsigned long long subtreeExpanded = 0;
	};
	struct PartialDecisionEntry {
		std::unordered_map<std::string, ExactScore, ExactStringHasher> actionBounds;
		size_t resumeOrdinal = 0;
		size_t accountedBytes = 0;
	};
	struct PartialChanceEntry {
		std::unordered_map<int, ExactScore> completedOutcomes;
		size_t accountedBytes = 0;
	};
	struct BoundedCompositionCursor {
		std::vector<int> bounds, values, nextTake, left;
		int depth = 0;
		bool initialized = false, complete = false;

		void reset(const std::vector<int>& newBounds, int total) {
			bounds = newBounds; values.assign(bounds.size(), 0);
			nextTake.assign(bounds.size(), 0); left.assign(bounds.size() + 1, 0);
			left[0] = total; depth = 0; initialized = true; complete = false;
		}

		bool next(std::vector<int>& output) {
			if (!initialized || complete) return false;
			const int count = (int)bounds.size();
			while (true) {
				if (depth == count) {
					bool valid = left[depth] == 0;
					if (valid) output = values;
					if (depth == 0) complete = true; else --depth;
					if (valid) return true;
					continue;
				}
				int maximum = std::min(bounds[depth], left[depth]);
				if (nextTake[depth] <= maximum) {
					int take = nextTake[depth]++;
					values[depth] = take; left[depth + 1] = left[depth] - take;
					++depth;
					if (depth < count) nextTake[depth] = 0;
					continue;
				}
				if (depth == 0) { complete = true; return false; }
				--depth;
			}
		}
	};
	struct PartialRevealEntry {
		BoundedCompositionCursor prizeCursor, handCursor;
		std::vector<int> prizeCounts, handCounts;
		ExactFraction completedLower = ExactFraction::integer(0);
		ExactFraction completedUpper = ExactFraction::integer(0);
		ExactWeight totalWeight, processedWeight, pendingWeight;
		bool initialized = false, handActive = false, pendingWorld = false;
		size_t accountedBytes = 0;
	};
	struct PartialBeliefRevealEntry {
		BoundedCompositionCursor prizeCursor, handCursor;
		std::vector<int> bounds, handBounds, prizeCounts, handCounts;
		std::vector<BeliefWorld> worlds;
		ExactWeight expected, generated;
		bool initialized = false, handActive = false, completed = false;
		size_t accountedBytes = 0;
	};
	struct FixedTurnRevealAllocation {
		std::vector<int> prizeCounts;
		ExactWeight weight;
	};
	struct PartialFixedTurnRevealEntry {
		std::vector<FixedTurnRevealAllocation> allocations;
		size_t index = 0;
		ExactFraction completedLower = ExactFraction::integer(0);
		ExactFraction completedUpper = ExactFraction::integer(0);
		ExactWeight totalWeight, processedWeight;
		size_t accountedBytes = 0;
	};
	struct PartialMultiDrawEntry {
		BoundedCompositionCursor cursor;
		std::vector<int> bounds, counts;
		ExactFraction completedLower = ExactFraction::integer(0);
		ExactFraction completedUpper = ExactFraction::integer(0);
		ExactWeight totalWeight, processedWeight, pendingWeight;
		bool initialized = false, pending = false;
		size_t accountedBytes = 0;
	};
	std::unordered_map<int, int> actorProfileCount;
	std::unordered_map<int, int> opponentProfileCount;
	std::unordered_map<int, int> handValue;
	std::uint64_t environmentPriorId = 0;
	std::shared_ptr<const ExactCpuEvaluator> evaluator;
	std::chrono::steady_clock::time_point deadline;
	ExactMetrics metrics;
	int actor = 0;
	// Two fixed SipHash-2-4 digests index the table; std::string equality still
	// compares every canonical byte, so a digest collision cannot merge states.
	std::shared_ptr<ExactSharedTransposition> transposition;
	std::unordered_map<std::string, ExactScore, ExactStringHasher> localTransposition;
	bool usingSharedTable = false;
	std::vector<ExactRootActionValue> rootActionValues;
	std::unordered_map<int, std::string> rootSuccessorKeys;
	std::unordered_map<std::string, std::vector<ExactPolicyEntry>, ExactStringHasher> policy;
	std::unordered_map<std::string, PartialDecisionEntry, ExactStringHasher> partialDecisions;
	std::unordered_map<std::string, PartialChanceEntry, ExactStringHasher> partialChances;
	std::unordered_map<std::string, PartialRevealEntry, ExactStringHasher> partialReveals;
	std::unordered_map<std::string, PartialBeliefRevealEntry, ExactStringHasher> partialBeliefReveals;
	// std::map keeps the outer reveal entry stable when a searched card starts a
	// nested fixed-turn reveal and inserts another resumable entry.
	std::map<std::string, PartialFixedTurnRevealEntry> partialFixedTurnReveals;
	std::map<std::string, PartialMultiDrawEntry> partialMultiDraws;
	std::unordered_map<std::string, ExactScore, ExactStringHasher> beliefTransposition;
	mutable std::unordered_map<std::string, long long, ExactStringHasher> evaluationCache;
	// Fixed-deck turn-one search quotient.  The key retains the complete public
	// state, semantic search options, and every deck count that can affect a later
	// turn-one search, while omitting identities irrelevant until the next turn.
	std::unordered_map<std::string, ExactScore, ExactStringHasher> fixedFirstTurnRevealScores;
	std::unordered_map<std::string, ExactScore, ExactStringHasher> fixedFirstTurnMainScores;
	size_t beliefTranspositionBytes = 0;
	static constexpr size_t MaxPolicyEntries = 100'000;
	static constexpr size_t MaxPolicyBytes = 64ULL * 1024ULL * 1024ULL;
	static constexpr size_t MaxLocalTranspositionEntries = 250'000;
	static constexpr size_t MaxLocalTranspositionBytes = 200ULL * 1024ULL * 1024ULL;
	static constexpr size_t MaxPartialEntries = 50'000;
	static constexpr size_t MaxPartialBytes = 64ULL * 1024ULL * 1024ULL;
	size_t localTranspositionBytes = 0;
	size_t partialBytes = 0;
	size_t policyBytes = 0;
	int recursionDepth = 0;
	bool canonicalMainEnabled = false;
	// Full own-deck reveals produce singleton information sets.  Inside one of
	// those worlds, retaining canonical TT/policy keys costs substantially more
	// than the shallow first-turn subtree and cannot merge with another hidden
	// allocation.  The outer exact reveal cursor remains resumable.
	bool singletonRevealStreaming = false;
	unsigned long long resourceCheckCounter = 0;
	unsigned long long nodeQuantumDeadline = std::numeric_limits<unsigned long long>::max();

	bool expired() {
		if (metrics.memoryLimitReached) return true;
		if (metrics.expanded >= nodeQuantumDeadline) return true;
		if ((++resourceCheckCounter & 4095ULL) == 0) {
			unsigned long long rss = ExactResidentBytes();
			metrics.peakRssBytes = std::max(metrics.peakRssBytes, rss);
			if (rss >= 2'700ULL * 1024ULL * 1024ULL) {
				metrics.memoryLimitReached = true;
				return true;
			}
		}
		if (std::chrono::steady_clock::now() < deadline) return false;
		metrics.timedOut = true;
		metrics.deadlineOverrunMs = std::max<long long>(metrics.deadlineOverrunMs,
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - deadline).count());
		return true;
	}

	std::string keyFor(const State& input) const {
		return ExactCanonicalState::Build(input);
	}

	std::string fixedTurnOneMainKey(const State& state) const {
		if (!singletonRevealStreaming || state.turn != 1 || state.selectPlayer != actor
			|| state.selectType != SelectType::Main || state.exact.deckUnknown[actor]
			|| !isMajkelFixedProfile(actorProfileCount)) return {};
		// Enriching Energy can draw four cards later in this turn, so identities
		// outside the search-target quotient still affect its exact transition.
		for (CardRef ref : state.players[actor].hand)
			if (!ref.isNull() && state.getCard(ref).cardId == 13) return {};
		std::string key = "FIXED-TURN1-MAIN\x1f";
		key += observationKeyFor(state, actor, nullptr, false);
		static constexpr std::array<int, 6> relevantIds{ 66, 305, 343, 741, 742, 743 };
		std::array<int, relevantIds.size()> counts{};
		for (CardRef ref : state.players[actor].deck) if (!ref.isNull()) {
			int id = state.getCard(ref).cardId;
			for (int i = 0; i < (int)relevantIds.size(); ++i) if (id == relevantIds[i]) {
				counts[i]++; break;
			}
		}
		for (int count : counts) appendSemantic(key, count);
		return key;
	}

	PartialDecisionEntry* partialDecisionFor(const std::string& key) {
		auto found = partialDecisions.find(key);
		if (found != partialDecisions.end()) { metrics.partialDecisionHits++; return &found->second; }
		if (partialDecisions.size() + partialChances.size() + partialReveals.size() >= MaxPartialEntries
			|| partialBytes + key.size() + 128 > MaxPartialBytes) return nullptr;
		size_t bytes = key.size() + 128;
		partialBytes += bytes;
		auto [inserted, _] = partialDecisions.emplace(key, PartialDecisionEntry{});
		inserted->second.accountedBytes = bytes;
		return &inserted->second;
	}

	PartialChanceEntry* partialChanceFor(const std::string& key) {
		auto found = partialChances.find(key);
		if (found != partialChances.end()) { metrics.partialChanceHits++; return &found->second; }
		if (partialDecisions.size() + partialChances.size() + partialReveals.size() >= MaxPartialEntries
			|| partialBytes + key.size() + 96 > MaxPartialBytes) return nullptr;
		size_t bytes = key.size() + 96;
		partialBytes += bytes;
		auto [inserted, _] = partialChances.emplace(key, PartialChanceEntry{});
		inserted->second.accountedBytes = bytes;
		return &inserted->second;
	}

	PartialRevealEntry* partialRevealFor(const std::string& key, int typeCount) {
		auto found = partialReveals.find(key);
		if (found != partialReveals.end()) { metrics.partialRevealHits++; return &found->second; }
		size_t bytes = key.size() + 256 + (size_t)typeCount * sizeof(int) * 10;
		if (partialDecisions.size() + partialChances.size() + partialReveals.size() >= MaxPartialEntries
			|| partialBytes + bytes > MaxPartialBytes) return nullptr;
		partialBytes += bytes;
		auto [inserted, _] = partialReveals.emplace(key, PartialRevealEntry{});
		inserted->second.accountedBytes = bytes;
		return &inserted->second;
	}

	static void appendSemantic(std::string& out, long long value) {
		out += std::to_string(value); out.push_back(';');
	}

	std::string cardToken(const State& state, CardRef ref, bool pokemon) const {
		if (ref.isNull()) return "?";
		const Card& card = state.getCard(ref);
		std::string token;
		appendSemantic(token, card.cardId);
		appendSemantic(token, card.playerIndex);
		if (!pokemon) return token;
		appendSemantic(token, state.getHp(card));
		appendSemantic(token, state.getMaxHp(card));
		appendSemantic(token, card.appear ? 1 : 0);
		auto& energyTypes = state.game->energyList;
		state.getEnergies(card.playerIndex, ref, energyTypes);
		std::vector<int> types;
		for (EnergyType type : energyTypes) types.push_back(EnergyTypeIndex(type));
		std::sort(types.begin(), types.end());
		for (int type : types) appendSemantic(token, type);
		token.push_back('|');
		auto& cards = state.game->cardList;
		state.getEnergyCards(ref, cards);
		std::vector<int> ids;
		for (CardRef child : cards) ids.push_back(state.getCard(child).cardId);
		auto tools = state.getAttachedToolRef(card);
		for (CardRef child : tools) ids.push_back(state.getCard(child).cardId + 100000);
		auto evolutions = state.getPreEvolutions(card);
		for (CardRef child : evolutions) ids.push_back(state.getCard(child).cardId + 200000);
		std::sort(ids.begin(), ids.end());
		for (int id : ids) appendSemantic(token, id);
		return token;
	}

	template<class List>
	void appendCardList(std::string& out, const State& state, const List& list,
		bool pokemon, bool unordered, bool hidden) const {
		appendSemantic(out, list.size());
		if (hidden) return;
		std::vector<std::string> tokens;
		for (CardRef ref : list) tokens.push_back(cardToken(state, ref, pokemon));
		if (unordered) std::sort(tokens.begin(), tokens.end());
		for (const std::string& token : tokens) {
			appendSemantic(out, (long long)token.size()); out += token;
		}
	}

	std::string optionSemanticToken(const State& state, const SelectOption& option) const {
		std::string token;
		appendSemantic(token, (int)option.type);
		auto appendPosition = [&](AreaType area, int index, int player) {
			appendSemantic(token, (int)area); appendSemantic(token, player);
			try {
				CardRef ref = state.getCardRef(area, index, player);
				std::string card = cardToken(state, ref, area == AreaType::Active || area == AreaType::Bench);
				appendSemantic(token, (long long)card.size()); token += card;
			} catch (...) { appendSemantic(token, index); }
		};
		switch (option.type) {
		case SelectOptionType::Card:
		case SelectOptionType::ToolCard:
		case SelectOptionType::EnergyCard:
		case SelectOptionType::Energy:
			appendPosition((AreaType)option.param0, option.param1, option.param2);
			appendSemantic(token, option.param3); appendSemantic(token, option.param4); break;
		case SelectOptionType::Play:
			appendPosition(AreaType::Hand, option.param0, state.selectPlayer); break;
		case SelectOptionType::Attach:
		case SelectOptionType::Evolve:
			appendPosition((AreaType)option.param0, option.param1, state.selectPlayer);
			appendPosition((AreaType)option.param2, option.param3, state.selectPlayer); break;
		case SelectOptionType::Ability:
		case SelectOptionType::Discard:
			appendPosition((AreaType)option.param0, option.param1, state.selectPlayer); break;
		case SelectOptionType::Skill:
			appendSemantic(token, option.param0); break; // serial is deliberately omitted
		default:
			appendSemantic(token, option.param0); appendSemantic(token, option.param1);
			appendSemantic(token, option.param2); appendSemantic(token, option.param3);
			appendSemantic(token, option.param4); break;
		}
		return token;
	}

	std::string observationKeyFor(const State& state, int requestedObserver = -1,
		const ExactKnowledgeState* knowledge = nullptr, bool includeKnownDeck = true) const {
		const int observer = requestedObserver >= 0 ? requestedObserver : state.selectPlayer;
		std::string key;
		appendSemantic(key, state.turn); appendSemantic(key, state.turnActionCount);
		appendSemantic(key, (int)state.phase); appendSemantic(key, (int)state.gameResult);
		appendSemantic(key, state.firstPlayer); appendSemantic(key, state.turnState);
		appendSemantic(key, (int)state.selectType); appendSemantic(key, (int)state.selectContext);
		appendSemantic(key, state.selectPlayer); appendSemantic(key, state.selectMin);
		appendSemantic(key, state.selectMax); appendSemantic(key, state.remainDamageCounter);
		appendSemantic(key, state.remainEnergyCost);
		appendCardList(key, state, state.stadium, false, true, false);
		for (int player = 0; player < 2; ++player) {
			const PlayerState& ps = state.players[player];
			appendCardList(key, state, ps.active, true, false, false);
			appendCardList(key, state, ps.bench, true, true, false);
			appendSemantic(key, state.benchCapacity(player));
			appendCardList(key, state, ps.trash, false, true, false);
			appendCardList(key, state, ps.prize, false, true, true);
			appendCardList(key, state, ps.hand, false, true, player != observer);
			appendSemantic(key, ps.deck.size());
			if (includeKnownDeck && ((state.selectDeck && state.selectPlayer == observer && player == observer)
				|| (knowledge != nullptr && knowledge->deckKnown[player])))
				appendCardList(key, state, ps.deck, false, true, false);
			appendSemantic(key, ps.poisonDamageCounter); appendSemantic(key, (int)ps.badStatus);
			appendSemantic(key, ps.burned ? 1 : 0);
		}
		std::vector<std::string> options;
		for (const SelectOption& option : state.options) options.push_back(optionSemanticToken(state, option));
		std::sort(options.begin(), options.end());
		for (const std::string& option : options) { appendSemantic(key, (long long)option.size()); key += option; }
		if (!state.contextCard.isNull()) { key += "C"; key += cardToken(state, state.contextCard, false); }
		if (state.onEffect()) { key += "E"; key += cardToken(state, state.getEffectCard().card, false); }
		if (knowledge != nullptr) { key += "K"; appendKnowledgeKey(key, *knowledge); }
		return key;
	}

	std::vector<std::string> semanticAction(const State& state, const std::vector<int>& action) const {
		std::vector<std::string> tokens;
		for (int index : action) tokens.push_back(optionSemanticToken(state, state.options.at(index)));
		std::sort(tokens.begin(), tokens.end());
		return tokens;
	}

	bool remapAction(const State& state, const std::vector<std::string>& wanted, std::vector<int>& result) const {
		std::vector<bool> used(state.options.size(), false);
		for (const std::string& token : wanted) {
			int found = -1;
			for (int i = 0; i < (int)state.options.size(); ++i) if (!used[i]
				&& optionSemanticToken(state, state.options[i]) == token) { found = i; break; }
			if (found < 0) return false;
			used[found] = true; result.push_back(found);
		}
		std::sort(result.begin(), result.end());
		return (int)result.size() >= state.selectMin && (int)result.size() <= state.selectMax;
	}

	void rememberPolicy(const State& state, const ExactScore& score, unsigned long long subtreeExpanded) {
		if (score.action.empty() && state.selectMin != 0) return;
		if (policy.size() >= MaxPolicyEntries || policyBytes >= MaxPolicyBytes) return;
		std::string key = observationKeyFor(state);
		ExactPolicyEntry entry{ score, semanticAction(state, score.action), subtreeExpanded };
		size_t bytes = key.size() + sizeof(ExactPolicyEntry) + 64;
		for (const std::string& token : entry.actionTokens) bytes += token.size();
		if (policyBytes + bytes > MaxPolicyBytes) return;
		auto& bucket = policy[key];
		for (const ExactPolicyEntry& existing : bucket) {
			if (existing.actionTokens == entry.actionTokens
				&& ExactCompare(existing.score.lower, entry.score.lower) == 0
				&& ExactCompare(existing.score.upper, entry.score.upper) == 0) return;
		}
		policyBytes += bytes; bucket.push_back(std::move(entry));
		metrics.policyNodes++; metrics.sessionBytes = transposition->bytes() + localTranspositionBytes
			+ policyBytes + partialBytes + beliefTranspositionBytes + evaluationCache.size() * 128ULL;
	}

	void initializeHidden(State& state) {
		state.exact = {};
		state.exact.enabled = true;
		state.exact.actor = (signed char)actor;
		for (int player = 0; player < 2; ++player) {
			const auto& profile = player == actor ? actorProfileCount : opponentProfileCount;
			if (profile.empty()) continue;
			state.exact.profileKnown[player] = true;
			auto remaining = profile;
			for (const Card& card : state.allCard) {
				auto it = remaining.find(card.cardId);
				if (card.cardId != 0 && card.playerIndex == player && it != remaining.end() && it->second > 0) it->second--;
			}
			for (const auto& [id, count] : remaining) {
				if (count <= 0) continue;
				int index = state.exact.typeCount[player]++;
				state.exact.cardId[player][index] = id;
				state.exact.cardCount[player][index] = (unsigned char)count;
			}
		}
		auto hasNull = [](const auto& list) { for (CardRef ref : list) if (ref.isNull()) return true; return false; };
		for (int p = 0; p < 2; ++p) {
			state.exact.deckUnknown[p] = hasNull(state.players[p].deck);
			state.exact.prizeExchangeable[p] = hasNull(state.players[p].prize);
		}
	}

	long long evaluate(const State& state) {
		if (state.isFinish()) {
			int winner = state.winPlayer();
			return winner == actor ? 100'000'000 : (winner == 2 ? 0 : -100'000'000);
		}
		if (evaluator && evaluator->isLoaded()) {
			auto features = ExactSparseEvaluatorV3::extractFeatures(state, actor, &actorProfileCount);
			long long result = 0;
			if (!evaluator->evaluateV3Features(features, result)) {
				metrics.informationSetSafe = false; return 0;
			}
			return result;
		}
		// A missing model is not silently replaced by the retired V1 heuristic.
		// Zero is useful for structural tests, but applyEvaluatorSafety prevents it
		// from being reported as a certified evaluator result.
		return 0;
	}

	static int beliefQ8(const ExactWeight& numerator, const ExactWeight& denominator) {
		ExactWeight scaled = ExactWeight::multiply(numerator, ExactWeight(ExactSparseEvaluatorV3::BeliefScale));
		auto division = ExactWeight::divideRemainder(scaled, denominator);
		ExactWeight twiceRemainder = ExactWeight::multiply(division.second, ExactWeight(2));
		if (twiceRemainder >= denominator) division.first += ExactWeight(1);
		if (!division.first.fitsUnsignedLongLong()) throw std::overflow_error("belief Q8 overflow");
		return (int)division.first.unsignedLongLong();
	}

	long long evaluateBeliefInformationState(const std::vector<BeliefWorld>& worlds,
		const ExactWeight& total) {
		if (worlds.empty() || total.zero()) throw std::runtime_error("empty leaf belief");
		const State& representative = *worlds.front().state;
		if (representative.isFinish()) return evaluate(representative);
		std::unordered_map<int, ExactWeight> deckMass, prizeMass, deckExistsMass, prizeExistsMass, comboMass;
		for (const BeliefWorld& world : worlds) {
			std::unordered_map<int, int> deckCount, prizeCount;
			std::unordered_set<int> worldCombos;
			for (CardRef ref : world.state->players[actor].deck) if (!ref.isNull())
				deckCount[world.state->getCard(ref).cardId]++;
			for (CardRef ref : world.state->players[actor].prize) if (!ref.isNull())
				prizeCount[world.state->getCard(ref).cardId]++;
			for (const auto& item : deckCount) deckMass[item.first] +=
				ExactWeight::multiply(world.weight, ExactWeight(item.second));
			for (const auto& item : prizeCount) prizeMass[item.first] +=
				ExactWeight::multiply(world.weight, ExactWeight(item.second));
			for (const auto& item : deckCount) if (item.second > 0) deckExistsMass[item.first] += world.weight;
			for (const auto& item : prizeCount) if (item.second > 0) prizeExistsMass[item.first] += world.weight;
			// Evolution correlations are exact events over this concrete world.
			for (const auto& profile : actorProfileCount) {
				auto foundMaster = CardTable.find(profile.first);
				if (foundMaster == CardTable.end() || foundMaster->second.evolutionType == EvolutionType::Basic
					|| foundMaster->second.evolutionType == EvolutionType::NoEvolutionType
					|| deckCount[profile.first] <= 0) continue;
				bool hasPre = false;
				for (const auto& candidate : actorProfileCount) {
					auto preMaster = CardTable.find(candidate.first);
					if (preMaster != CardTable.end() && deckCount[candidate.first] > 0
						&& (preMaster->second.name == foundMaster->second.evolvesFrom
							|| preMaster->second.nameEn == foundMaster->second.evolvesFrom)) { hasPre = true; break; }
				}
				if (hasPre) worldCombos.insert(ExactSparseEvaluatorV3::ComboTokenBase + profile.first);
				if (hasPre && foundMaster->second.evolutionType == EvolutionType::Stage2) {
					bool hasBasic = false;
					for (const auto& stageCandidate : actorProfileCount) { auto stage = CardTable.find(stageCandidate.first);
						if (stage == CardTable.end() || deckCount[stageCandidate.first] <= 0
							|| !(stage->second.name == foundMaster->second.evolvesFrom || stage->second.nameEn == foundMaster->second.evolvesFrom)) continue;
						for (const auto& basicCandidate : actorProfileCount) { auto basic = CardTable.find(basicCandidate.first);
							if (basic != CardTable.end() && deckCount[basicCandidate.first] > 0
								&& (basic->second.name == stage->second.evolvesFrom || basic->second.nameEn == stage->second.evolvesFrom)) {
								hasBasic = true; break;
							}
						}
						if (hasBasic) break;
					}
					if (hasBasic) worldCombos.insert(ExactSparseEvaluatorV3::ComboTokenBase + 250'000 + profile.first);
				}
			}
			// Energy supply event: attaching every compatible energy remaining in
			// the deck must make the attack payable. Adding energy cannot invalidate
			// an attack, so this is an exact existence test, not a sample.
			auto attackSupply = [&](CardRef pokemonRef) {
				if (pokemonRef.isNull()) return;
				const State& s = *world.state; const Card& pokemon = s.getCard(pokemonRef);
				auto& existing = s.game->energyList; s.getEnergies(actor, pokemonRef, existing);
				SetAttackEnergy(s, pokemon, existing, true);
				std::unordered_map<int, int> before;
				for (const AttackEnergy& ae : s.game->attackEnergyList) before[ae.attack->attackId] = ae.insufficientEnergy;
				auto all = existing;
				for (CardRef energyRef : s.players[actor].deck) if (!energyRef.isNull()) {
					const Card& energy = s.getCard(energyRef);
					if (!IsEnergy(energy.getMaster().cardType)) continue;
					EnergyInfo info = s.getEnergyInfo(energy, pokemonRef);
					for (int n = 0; n < info.count; ++n) all.push_back(info.type);
				}
				SetAttackEnergy(s, pokemon, all, true);
				for (const AttackEnergy& ae : s.game->attackEnergyList) if (before[ae.attack->attackId] > 0 && ae.insufficientEnergy <= 0)
					worldCombos.insert(ExactSparseEvaluatorV3::ComboTokenBase + 500'000 + ae.attack->attackId);
			};
			for (CardRef ref : world.state->players[actor].active) attackSupply(ref);
			for (CardRef ref : world.state->players[actor].bench) attackSupply(ref);
			for (int token : worldCombos) comboMass[token] += world.weight;
		}
		std::unordered_map<int, int> deckQ8, prizeQ8, deckExistsQ8, prizeExistsQ8, comboQ8;
		for (const auto& item : deckMass) deckQ8[item.first] = beliefQ8(item.second, total);
		for (const auto& item : prizeMass) prizeQ8[item.first] = beliefQ8(item.second, total);
		for (const auto& item : deckExistsMass) deckExistsQ8[item.first] = beliefQ8(item.second, total);
		for (const auto& item : prizeExistsMass) prizeExistsQ8[item.first] = beliefQ8(item.second, total);
		for (const auto& item : comboMass) comboQ8[item.first] = beliefQ8(item.second, total);
		ExactSparseEvaluatorV3::BeliefInput belief{ &deckQ8, &prizeQ8, &deckExistsQ8, &prizeExistsQ8, &comboQ8,
			&worlds.front().knowledge[actor].knownDeckCounts[actor], &worlds.front().knowledge[actor].knownPrizeCounts[actor],
			&worlds.front().knowledge[actor].knownTop, &worlds.front().knowledge[actor].knownBottom };
		if (evaluator && evaluator->isLoaded()) {
			auto features = ExactSparseEvaluatorV3::extractFeatures(representative, actor, &actorProfileCount, &belief);
			metrics.entityFeatureCount += features.entityCount; metrics.comboFeatureCount += comboQ8.size();
			for (int ei = 0; ei < features.entityCount; ++ei) for (int si = 0; si < features.entities[ei].sparse.count; ++si)
				if (features.entities[ei].sparse.values[si].relation == ExactSparseEvaluatorV3::AttackUnavailable)
					metrics.attackPreviewUnavailableCount++;
				else if (features.entities[ei].sparse.values[si].relation == ExactSparseEvaluatorV3::AttackExactDamage)
					metrics.attackPreviewExactCount++;
			long long value = 0;
			if (!evaluator->evaluateV3Features(features, value)) { metrics.informationSetSafe = false; return 0; }
			return value;
		}
		return evaluate(representative);
	}

	void applyEvaluatorSafety(ExactDecision& decision) {
		if (!evaluator || !evaluator->isLoaded() || evaluator->schemaVersion() != ExactSparseEvaluatorV3::SchemaVersion
			|| !evaluator->informationSetSafe()) {
			metrics.informationSetSafe = false;
			decision.score.certified = false;
			for (ExactRootActionValue& action : decision.rootActions) action.certified = false;
		}
	}

	ExactScore unknown() const { return {}; }

	template<class Callback>
	bool forEachLegalAction(const State& state, Callback&& callback) {
		// Evaluate End first so an interrupted main node always has a real leaf
		// lower bound.  The regular generator skips that one duplicate.
		int endIndex = -1;
		if (state.selectType == SelectType::Main && state.selectMin <= 1 && state.selectMax >= 1) {
			for (int i = 0; i < (int)state.options.size(); ++i) {
				if (state.options[i].type == SelectOptionType::End) { endIndex = i; break; }
			}
			if (endIndex >= 0 && !callback(std::vector<int>{ endIndex })) return false;
		}
		struct OptionGroup { std::string key; std::vector<int> index; };
		std::vector<OptionGroup> groups;
		std::unordered_map<std::string, size_t, ExactStringHasher> groupByKey;
		for (int i = 0; i < (int)state.options.size(); ++i) {
			std::string key = actionEquivalenceKey(state, { i });
			auto [found, inserted] = groupByKey.emplace(key, groups.size());
			if (inserted) groups.push_back({ std::move(key), {} });
			groups[found->second].index.push_back(i);
		}
		std::vector<int> current;
		std::function<bool(int, int)> choose = [&](int group, int left) {
			if (expired()) return false;
			if (left == 0) {
				std::vector<int> action = current; std::sort(action.begin(), action.end());
				if (action.size() == 1 && action[0] == endIndex) return true;
				return callback(action);
			}
			if (group >= (int)groups.size()) return true;
			int remainingCapacity = 0;
			for (int i = group; i < (int)groups.size(); ++i) remainingCapacity += (int)groups[i].index.size();
			if (remainingCapacity < left) return true;
			int maximum = std::min(left, (int)groups[group].index.size());
			for (int take = 0; take <= maximum; ++take) {
				for (int i = 0; i < take; ++i) current.push_back(groups[group].index[i]);
				if (!choose(group + 1, left - take)) return false;
				for (int i = 0; i < take; ++i) current.pop_back();
			}
			return true;
		};
		for (int count = state.selectMin; count <= state.selectMax; ++count) {
			if (!choose(0, count)) return false;
		}
		return true;
	}

	bool advance(State& state, const std::vector<int>& action) {
		try {
			state.selected = action;
			if (state.checkPlayerSelect() != 0) return false;
			state.step();
			while (!state.isFinish() && !IsExactTurnLeaf(state)
				&& state.exact.pending == ExactPendingType::None && state.selectType == SelectType::None) {
				state.selected.clear(); state.step();
			}
			return true;
		} catch (const std::exception& error) {
			metrics.exceptions++;
			metrics.lastException = error.what();
			state.exact.pending = ExactPendingType::Opaque;
			state.exact.blockReason = ExactBlockReason::Exception;
			return true;
		} catch (...) {
			metrics.exceptions++;
			metrics.lastException = "unknown";
			state.exact.pending = ExactPendingType::Opaque;
			state.exact.blockReason = ExactBlockReason::Exception;
			return true;
		}
	}

	CardRef materialize(State& state, int player, AreaType area, int areaIndex, int cardId) {
		int freeIndex = -1;
		for (int i = 3; i < (int)state.allCard.size(); ++i) if (state.allCard[i].cardId == 0) { freeIndex = i; break; }
		if (freeIndex < 0) throw std::runtime_error("no exact card slot");
		CardRef ref(freeIndex);
		Card& card = state.allCard[freeIndex]; card = {}; card.init(cardId, state.moveCounter++, player); card.area = area;
		PlayerState& ps = state.players[player];
		switch (area) {
		case AreaType::Deck: ps.deck.at(areaIndex) = ref; break;
		case AreaType::Hand: ps.hand.at(areaIndex) = ref; break;
		case AreaType::Prize: ps.prize.at(areaIndex) = ref; break;
		default: throw std::runtime_error("unsupported exact materialization area");
		}
		return ref;
	}

	std::string actionEquivalenceKey(const State& state, const std::vector<int>& action) const {
		std::vector<std::string> tokens;
		tokens.reserve(action.size());
		for (int selectedIndex : action) {
			const SelectOption& option = state.options[selectedIndex];
			std::string token;
			if (option.type == SelectOptionType::Card) {
				CardPosition pos = option.getCardPosition();
				bool exchangeable = (pos.area == AreaType::Deck && state.exact.deckExchangeable[pos.playerIndex])
					|| (pos.area == AreaType::Prize && state.exact.prizeExchangeable[pos.playerIndex]);
				if (exchangeable) {
					CardRef ref = state.getCardRef(pos);
					int id = ref.isNull() ? 0 : state.getCard(ref).cardId;
					token = (pos.area == AreaType::Deck ? "D:" : "P:")
						+ std::to_string(pos.playerIndex) + ":" + std::to_string(id);
				}
			}
			if (token.empty()) {
				token = "O:" + std::to_string((int)option.type) + ":" + std::to_string(option.param0)
					+ ":" + std::to_string(option.param1) + ":" + std::to_string(option.param2)
					+ ":" + std::to_string(option.param3) + ":" + std::to_string(option.param4);
			}
			tokens.push_back(std::move(token));
		}
		std::sort(tokens.begin(), tokens.end());
		std::string result;
		for (const std::string& token : tokens) { result += std::to_string(token.size()); result += ':'; result += token; }
		return result;
	}

	void decrementPool(State& state, int player, int cardId) {
		for (int i = 0; i < state.exact.typeCount[player]; ++i) if (state.exact.cardId[player][i] == cardId) {
			if (state.exact.cardCount[player][i] == 0) throw std::runtime_error("empty hidden card type");
			state.exact.cardCount[player][i]--; return;
		}
		throw std::runtime_error("unknown hidden card type");
	}

	static ExactWeight chooseCount(int n, int k) {
		if (n < 0 || n > DECK_SIZE || k < 0 || k > n) return ExactWeight();
		static const auto table = [] {
			std::array<std::array<unsigned long long, DECK_SIZE + 1>, DECK_SIZE + 1> value{};
			for (int row = 0; row <= DECK_SIZE; ++row) {
				value[row][0] = value[row][row] = 1;
				for (int column = 1; column < row; ++column)
					value[row][column] = value[row - 1][column - 1] + value[row - 1][column];
			}
			return value;
		}();
		return ExactWeight(table[n][k]);
	}

	void noteWeight(const ExactWeight& value, bool operation = true) {
		if (operation) {
			if (value.isLarge()) metrics.bigWeightPromotions++;
			else metrics.smallWeightOps++;
		}
		metrics.maxWeightBits = std::max(metrics.maxWeightBits, value.bitLength());
	}

	void materializeUnknownZones(State& state, int player, const std::vector<int>& prizeCounts,
		const std::vector<int>& handCounts) {
		PlayerState& ps = state.players[player];
		std::vector<int> prizeIds, handIds, deckIds;
		for (int i = 0; i < state.exact.typeCount[player]; ++i) {
			int p = prizeCounts[i], h = handCounts[i];
			for (int n = 0; n < p; ++n) prizeIds.push_back(state.exact.cardId[player][i]);
			for (int n = 0; n < h; ++n) handIds.push_back(state.exact.cardId[player][i]);
			for (int n = p + h; n < state.exact.cardCount[player][i]; ++n) deckIds.push_back(state.exact.cardId[player][i]);
		}
		int prizeAt = 0, handAt = 0, deckAt = 0;
		for (int i = 0; i < ps.prize.size(); ++i) if (ps.prize[i].isNull()) {
			if (prizeAt >= (int)prizeIds.size()) throw std::runtime_error("prize materialization mismatch");
			materialize(state, player, AreaType::Prize, i, prizeIds[prizeAt++]);
		}
		for (int i = 0; i < ps.hand.size(); ++i) if (ps.hand[i].isNull()) {
			if (handAt >= (int)handIds.size()) throw std::runtime_error("hand materialization mismatch");
			materialize(state, player, AreaType::Hand, i, handIds[handAt++]);
		}
		for (int i = 0; i < ps.deck.size(); ++i) if (ps.deck[i].isNull()) {
			if (deckAt >= (int)deckIds.size()) throw std::runtime_error("deck materialization mismatch");
			materialize(state, player, AreaType::Deck, i, deckIds[deckAt++]);
		}
		if (prizeAt != (int)prizeIds.size() || handAt != (int)handIds.size() || deckAt != (int)deckIds.size()) throw std::runtime_error("hidden zone size mismatch");
		state.exact.deckUnknown[player] = false;
		state.exact.deckExchangeable[player] = true;
		state.exact.prizeExchangeable[player] = true;
		state.exact.clearPending();
	}

	bool isMajkelFixedProfile(const std::unordered_map<int, int>& profile) const {
		static const std::array<std::pair<int, int>, 22> expected = {{
			{ 5, 2 }, { 13, 1 }, { 19, 4 }, { 66, 2 }, { 140, 1 }, { 305, 3 },
			{ 343, 1 }, { 741, 4 }, { 742, 4 }, { 743, 4 }, { 1079, 3 }, { 1081, 4 },
			{ 1086, 4 }, { 1097, 1 }, { 1129, 1 }, { 1152, 4 }, { 1182, 3 }, { 1184, 1 },
			{ 1197, 3 }, { 1225, 4 }, { 1231, 4 }, { 1266, 2 }
		}};
		if (profile.size() != expected.size()) return false;
		for (const auto& item : expected) {
			auto found = profile.find(item.first);
			if (found == profile.end() || found->second != item.second) return false;
		}
		return true;
	}

	static AreaType pendingArea(const ExactHiddenState& request) {
		int detail = request.pendingDetail;
		if (request.pendingPlayer >= 0) detail -= 100 * request.pendingPlayer;
		return (AreaType)detail;
	}

	void materializeUnknownHand(State& state, int player, const std::vector<int>& handCounts) {
		std::vector<int> ids;
		for (int i = 0; i < state.exact.typeCount[player]; ++i)
			for (int n = 0; n < handCounts[i]; ++n) ids.push_back(state.exact.cardId[player][i]);
		std::sort(ids.begin(), ids.end());
		int at = 0;
		for (int i = 0; i < state.players[player].hand.size(); ++i) {
			if (!state.players[player].hand[i].isNull()) continue;
			if (at >= (int)ids.size()) throw std::runtime_error("hand-only materialization mismatch");
			materialize(state, player, AreaType::Hand, i, ids[at++]);
		}
		if (at != (int)ids.size()) throw std::runtime_error("hand-only materialization overflow");
		state.exact.clearPending();
	}

	bool selectProvisionalXerosicDiscard(State& state, int player,
		std::unordered_map<int, int>& discarded) {
		if (state.selectPlayer != player || state.selectType != SelectType::Card
			|| state.selectMin != state.selectMax || state.selectMin <= 0) return false;
		std::vector<std::pair<int, int>> candidates;
		for (int optionIndex = 0; optionIndex < (int)state.options.size(); ++optionIndex) {
			const SelectOption& option = state.options[optionIndex];
			if (option.type != SelectOptionType::Card) continue;
			CardPosition position = option.getCardPosition();
			if (position.playerIndex != player || position.area != AreaType::Hand) continue;
			CardRef ref = state.getCardRef(position);
			if (ref.isNull()) return false;
			candidates.push_back({ state.getCard(ref).cardId, optionIndex });
		}
		if ((int)candidates.size() < state.selectMin) return false;
		// Stable bootstrap policy: discard the highest card IDs first.  This is
		// deliberately simple and is never reported as a minimax certificate.
		std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
			if (left.first != right.first) return left.first > right.first;
			return left.second < right.second;
		});
		std::vector<int> selected;
		for (int i = 0; i < state.selectMin; ++i) {
			selected.push_back(candidates[i].second);
			discarded[candidates[i].first]++;
		}
		std::sort(selected.begin(), selected.end());
		return advance(state, selected);
	}

	void anonymizeXerosicHand(State& state, int player, const std::vector<int>& originalBounds,
		const std::unordered_map<int, int>& discarded) {
		PlayerState& ps = state.players[player];
		for (int i = 0; i < ps.hand.size(); ++i) {
			CardRef ref = ps.hand[i];
			if (ref.isNull()) continue;
			state.allCard[ref.cardIndex] = {};
			ps.hand[i] = CardRef(0);
		}
		for (int i = 0; i < state.exact.typeCount[player]; ++i) {
			int count = originalBounds[i];
			auto found = discarded.find(state.exact.cardId[player][i]);
			if (found != discarded.end()) count -= found->second;
			if (count < 0 || count > 255) throw std::runtime_error("Xerosic hidden pool mismatch");
			state.exact.cardCount[player][i] = (unsigned char)count;
		}
		state.exact.deckUnknown[player] = true;
		state.exact.prizeExchangeable[player] = true;
		state.exact.provisionalOpponentPolicy = true;
		state.exact.clearPending();
	}

	bool expandProvisionalXerosicBelief(const State& parent, const ExactHiddenState& request,
		const std::vector<int>& action, const ExactWeight& baseWeight,
		const std::array<ExactKnowledgeState, 2>& baseKnowledge, std::vector<BeliefWorld>& output) {
		const int player = request.pendingPlayer;
		if (request.pendingEffectCardId != 1197 || pendingArea(request) != AreaType::Hand
			|| !isMajkelFixedProfile(actorProfileCount) || !isMajkelFixedProfile(opponentProfileCount)) return false;
		int handSize = 0;
		for (CardRef ref : parent.players[player].hand) {
			if (!ref.isNull()) return false; // fixed-deck bootstrap supports a fully hidden hand
			handSize++;
		}
		if (handSize <= 3) return false;
		std::vector<int> bounds(parent.exact.typeCount[player]);
		int totalHidden = 0;
		for (int i = 0; i < (int)bounds.size(); ++i) {
			bounds[i] = parent.exact.cardCount[player][i]; totalHidden += bounds[i];
		}
		if (handSize > totalHidden) return false;
		ExactWeight expected = ExactWeight::multiply(baseWeight, chooseCount(totalHidden, handSize));
		BoundedCompositionCursor cursor; cursor.reset(bounds, handSize);
		std::vector<int> handCounts;
		struct Outcome {
			std::vector<int> representativeHand;
			std::vector<int> discarded;
			ExactWeight weight;
		};
		std::vector<int> idOrder(bounds.size());
		std::iota(idOrder.begin(), idOrder.end(), 0);
		std::sort(idOrder.begin(), idOrder.end(), [&](int left, int right) {
			return parent.exact.cardId[player][left] < parent.exact.cardId[player][right];
		});
		struct CountKeyHash { size_t operator()(const std::array<unsigned char, DECK_SIZE>& key) const noexcept {
			size_t value = 1469598103934665603ULL;
			for (unsigned char count : key) { value ^= count; value *= 1099511628211ULL; }
			return value;
		} };
		std::unordered_map<std::array<unsigned char, DECK_SIZE>, size_t, CountKeyHash> byDiscard;
		std::vector<Outcome> outcomes;
		ExactWeight generated;
		unsigned long long raw = 0;
		while (cursor.next(handCounts)) {
			if (expired()) return false;
			ExactWeight allocation(1);
			for (int i = 0; i < (int)bounds.size(); ++i)
				allocation = ExactWeight::multiply(allocation, chooseCount(bounds[i], handCounts[i]));
			ExactWeight weight = ExactWeight::multiply(baseWeight, allocation);
			std::array<unsigned char, DECK_SIZE> discardKey{};
			for (int i = 0; i < (int)handCounts.size(); ++i) discardKey[i] = (unsigned char)handCounts[i];
			int keep = 3;
			for (int index : idOrder) {
				int count = std::min(keep, (int)discardKey[index]);
				discardKey[index] = (unsigned char)(discardKey[index] - count); keep -= count;
				if (keep == 0) break;
			}
			if (keep != 0) return false;
			auto found = byDiscard.find(discardKey);
			if (found == byDiscard.end()) {
				byDiscard.emplace(discardKey, outcomes.size());
				std::vector<int> discardCounts(handCounts.size());
				for (int i = 0; i < (int)discardCounts.size(); ++i) discardCounts[i] = discardKey[i];
				outcomes.push_back({ handCounts, std::move(discardCounts), weight });
			} else { outcomes[found->second].weight += weight; metrics.distributionMerges++; }
			generated += weight; raw++; metrics.enumeratedHiddenWorlds++;
		}
		if (generated != expected) {
			metrics.chanceMassMismatches++; metrics.probabilityExact = false; return false;
		}
		std::unordered_map<std::string, size_t, ExactStringHasher> bySuccessor;
		for (const Outcome& outcome : outcomes) {
			if (expired()) return false;
			auto child = std::make_unique<State>(parent);
			try {
				materializeUnknownHand(*child, player, outcome.representativeHand);
				if (!advance(*child, action)) return false;
				std::unordered_map<int, int> discarded;
				if (!selectProvisionalXerosicDiscard(*child, player, discarded)) return false;
				for (int i = 0; i < (int)outcome.discarded.size(); ++i) {
					int expectedCount = outcome.discarded[i];
					int actualCount = discarded[parent.exact.cardId[player][i]];
					if (actualCount != expectedCount) return false;
				}
				anonymizeXerosicHand(*child, player, bounds, discarded);
			} catch (...) { return false; }
			BeliefWorld candidate{ std::move(child), outcome.weight, baseKnowledge };
			std::string key = beliefWorldKey(candidate);
			auto found = bySuccessor.find(key);
			if (found == bySuccessor.end()) {
				bySuccessor.emplace(std::move(key), output.size()); output.push_back(std::move(candidate));
			} else { output[found->second].weight += outcome.weight; metrics.distributionMerges++; }
		}
		metrics.rawOutcomes += raw; metrics.groupedOutcomes += outcomes.size();
		metrics.provisionalOpponentPolicyNodes++;
		return !output.empty();
	}

	ExactScore solveProvisionalXerosicStreaming(const State& parent, const ExactHiddenState& request,
		const std::vector<int>& action) {
		const int player = request.pendingPlayer;
		if (request.pendingEffectCardId != 1197 || pendingArea(request) != AreaType::Hand
			|| !isMajkelFixedProfile(actorProfileCount) || !isMajkelFixedProfile(opponentProfileCount)) return unknown();
		int handSize = 0;
		for (CardRef ref : parent.players[player].hand) {
			if (!ref.isNull()) return unknown();
			handSize++;
		}
		std::vector<int> bounds(parent.exact.typeCount[player]);
		int totalHidden = 0;
		for (int i = 0; i < (int)bounds.size(); ++i) {
			bounds[i] = parent.exact.cardCount[player][i]; totalHidden += bounds[i];
		}
		if (handSize <= 3 || handSize > totalHidden) return unknown();
		struct Outcome { std::vector<int> hand, discarded; unsigned long long weight = 0; };
		std::vector<int> idOrder(bounds.size()); std::iota(idOrder.begin(), idOrder.end(), 0);
		std::sort(idOrder.begin(), idOrder.end(), [&](int left, int right) {
			return parent.exact.cardId[player][left] < parent.exact.cardId[player][right];
		});
		std::vector<Outcome> outcomes;
		BoundedCompositionCursor discardCursor; discardCursor.reset(bounds, handSize - 3);
		std::vector<int> discardCounts;
		unsigned long long generated = 0;
		unsigned long long raw = 0;
		while (discardCursor.next(discardCounts)) {
			if (expired()) return unknown();
			int firstDiscard = -1;
			for (int index : idOrder) if (discardCounts[index] > 0) { firstDiscard = index; break; }
			if (firstDiscard < 0) return unknown();
			std::vector<int> keepBounds(bounds.size());
			const int boundaryId = parent.exact.cardId[player][firstDiscard];
			for (int i = 0; i < (int)bounds.size(); ++i) {
				int remaining = bounds[i] - discardCounts[i];
				keepBounds[i] = parent.exact.cardId[player][i] <= boundaryId ? remaining : 0;
			}
			BoundedCompositionCursor keepCursor; keepCursor.reset(keepBounds, 3);
			std::vector<int> keptCounts, representative;
			unsigned long long outcomeWeight = 0;
			while (keepCursor.next(keptCounts)) {
				unsigned long long weight = 1;
				for (int i = 0; i < (int)bounds.size(); ++i) {
					ExactWeight factor = chooseCount(bounds[i], discardCounts[i] + keptCounts[i]);
					if (!factor.fitsUnsignedLongLong()
						|| factor.unsignedLongLong() > std::numeric_limits<unsigned long long>::max() / weight)
						return unknown();
					weight *= factor.unsignedLongLong();
				}
				if (outcomeWeight > std::numeric_limits<unsigned long long>::max() - weight) return unknown();
				outcomeWeight += weight; raw++; metrics.enumeratedHiddenWorlds++;
				if (representative.empty()) {
					representative.resize(bounds.size());
					for (int i = 0; i < (int)bounds.size(); ++i)
						representative[i] = discardCounts[i] + keptCounts[i];
				}
			}
			if (outcomeWeight == 0 || representative.empty()) continue;
			if (generated > std::numeric_limits<unsigned long long>::max() - outcomeWeight) return unknown();
			generated += outcomeWeight;
			outcomes.push_back({ std::move(representative), discardCounts, outcomeWeight });
		}
		ExactWeight totalWeight = chooseCount(totalHidden, handSize);
		if (!totalWeight.fitsUnsignedLongLong()) return unknown();
		unsigned long long total = totalWeight.unsignedLongLong();
		if (generated != total) {
			metrics.chanceMassMismatches++; metrics.probabilityExact = false; return unknown();
		}
		metrics.rawOutcomes += raw; metrics.groupedOutcomes += outcomes.size();
		metrics.provisionalOpponentPolicyNodes++;
		// MSVC's heap keeps the pages touched by the many tiny composition
		// temporaries in the working set even though only the compact outcomes
		// remain live.  The fixed-deck bootstrap is allowed up to the match-wide
		// 3 GiB limit; record the real peak, fail before crossing it, then return
		// unused pages before recursive search applies the normal 2.7 GiB guard.
		unsigned long long enumerationRss = ExactResidentBytes();
		metrics.peakRssBytes = std::max(metrics.peakRssBytes, enumerationRss);
		if (enumerationRss >= 3ULL * 1024ULL * 1024ULL * 1024ULL) {
			metrics.memoryLimitReached = true; return unknown();
		}
		metrics.memoryLimitReached = false; // the provisional enumerator is governed by the 3 GiB check above
#ifdef _WIN32
		SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
#endif
		resourceCheckCounter = 0; // allow the working-set trim to take effect before the next sample
		ExactFraction lower = ExactFraction::integer(0), upper = ExactFraction::integer(0);
		unsigned long long processed = 0;
		for (const Outcome& outcome : outcomes) {
			// A transient 2.7 GiB bootstrap mark must not prevent evaluation after
			// the explicit 3 GiB check and working-set trim above.
			metrics.memoryLimitReached = false;
			if (expired()) break;
			auto child = std::make_unique<State>(parent);
			try {
				materializeUnknownHand(*child, player, outcome.hand);
				if (!advance(*child, action)) return unknown();
				std::unordered_map<int, int> discarded;
				if (!selectProvisionalXerosicDiscard(*child, player, discarded)) return unknown();
				for (int i = 0; i < (int)outcome.discarded.size(); ++i)
					if (discarded[parent.exact.cardId[player][i]] != outcome.discarded[i]) return unknown();
				anonymizeXerosicHand(*child, player, bounds, discarded);
			} catch (...) { return unknown(); }
			ExactScore score = solveOwned(std::move(child));
			lower = ExactFraction::add(lower, score.lower.scaled(outcome.weight, total));
			upper = ExactFraction::add(upper, score.upper.scaled(outcome.weight, total));
			if (processed > std::numeric_limits<unsigned long long>::max() - outcome.weight) return unknown();
			processed += outcome.weight;
			if (!lower.valid || !upper.valid) { metrics.arithmeticOverflow = true; return unknown(); }
#ifdef _WIN32
			// State is intentionally streamed, but it is large enough that the CRT
			// heap otherwise retains every freed State page until the call ends.
			SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
#endif
			unsigned long long currentRss = ExactResidentBytes();
			metrics.peakRssBytes = std::max(metrics.peakRssBytes, currentRss);
			if (currentRss >= 3ULL * 1024ULL * 1024ULL * 1024ULL) {
				metrics.memoryLimitReached = true; break;
			}
		}
		unsigned long long remaining = processed >= total ? 0 : total - processed;
		if (remaining != 0) {
			lower = ExactFraction::add(lower, ExactFraction::integer(-100'000'000).scaled(remaining, total));
			upper = ExactFraction::add(upper, ExactFraction::integer(100'000'000).scaled(remaining, total));
			metrics.partialChanceNodes++;
		}
		if (metrics.peakRssBytes < 3ULL * 1024ULL * 1024ULL * 1024ULL)
			metrics.memoryLimitReached = false;
		return { lower, upper, {}, false };
	}

	bool expandRevealBelief(const State& parent, const ExactHiddenState& request,
		const std::vector<int>& action,
		const ExactWeight& baseWeight, const std::array<ExactKnowledgeState, 2>& baseKnowledge,
		std::vector<BeliefWorld>& output) {
		int player = request.pendingPlayer;
		if (player < 0 || player >= 2 || request.pending != ExactPendingType::RevealDeck) return false;
		if (!parent.exact.profileKnown[player]) return false;
		if (request.pendingEffectCardId == 1197 && pendingArea(request) == AreaType::Hand)
			return false; // handled by streaming or deferred as an unproven belief action
		int prizeSize = 0; for (CardRef ref : parent.players[player].prize) if (ref.isNull()) prizeSize++;
		int handSize = 0; for (CardRef ref : parent.players[player].hand) if (ref.isNull()) handSize++;
		int totalHidden = 0;
		std::vector<int> bounds(parent.exact.typeCount[player]);
		for (int i = 0; i < parent.exact.typeCount[player]; ++i) {
			bounds[i] = parent.exact.cardCount[player][i]; totalHidden += bounds[i];
		}
		if (prizeSize < 0 || handSize < 0 || prizeSize + handSize > totalHidden) return false;
		ExactWeight expected = ExactWeight::multiply(baseWeight,
			ExactWeight::multiply(chooseCount(totalHidden, prizeSize), chooseCount(totalHidden - prizeSize, handSize)));
		noteWeight(expected);
		std::string revealKey = keyFor(parent) + "\x1f" "BR3" "\x1f" + actionEquivalenceKey(parent, action)
			+ "\x1f" + baseWeight.text();
		appendSemantic(revealKey, player); appendSemantic(revealKey, request.pendingDetail);
		appendSemantic(revealKey, request.pendingEffectCardId); appendSemantic(revealKey, request.pendingEffectPlayer);
		for (int observer = 0; observer < 2; ++observer) appendKnowledgeKey(revealKey, baseKnowledge[observer]);
		auto [found, inserted] = partialBeliefReveals.try_emplace(revealKey);
		PartialBeliefRevealEntry& partial = found->second;
		if (inserted || !partial.initialized) {
			partial.bounds = bounds; partial.handBounds.resize(bounds.size());
			partial.prizeCounts.resize(bounds.size()); partial.handCounts.resize(bounds.size());
			partial.prizeCursor.reset(bounds, prizeSize); partial.expected = expected;
			partial.initialized = true;
			partial.accountedBytes = revealKey.size() + sizeof(PartialBeliefRevealEntry) + bounds.size() * sizeof(int) * 4;
			partialBytes += partial.accountedBytes;
		} else {
			metrics.partialRevealHits++;
			if (partial.bounds != bounds || partial.expected != expected) return false;
		}
		while (!partial.completed) {
			if (expired()) return false;
			if (!partial.handActive) {
				if (!partial.prizeCursor.next(partial.prizeCounts)) { partial.completed = true; break; }
				for (int i = 0; i < (int)bounds.size(); ++i)
					partial.handBounds[i] = bounds[i] - partial.prizeCounts[i];
				partial.handCursor.reset(partial.handBounds, handSize); partial.handActive = true;
			}
			if (!partial.handCursor.next(partial.handCounts)) { partial.handActive = false; continue; }
				ExactWeight allocation(1);
				for (int i = 0; i < (int)bounds.size(); ++i) {
					allocation = ExactWeight::multiply(allocation, chooseCount(bounds[i], partial.prizeCounts[i]));
					allocation = ExactWeight::multiply(allocation,
						chooseCount(bounds[i] - partial.prizeCounts[i], partial.handCounts[i]));
				}
				ExactWeight worldWeight = ExactWeight::multiply(baseWeight, allocation);
				noteWeight(worldWeight);
				auto child = std::make_unique<State>(parent);
				try {
					materializeUnknownZones(*child, player, partial.prizeCounts, partial.handCounts);
				} catch (...) { return false; }
				auto knowledge = baseKnowledge;
				int observer = parent.selectPlayer >= 0 ? parent.selectPlayer : actor;
				knowledge[observer].deckKnown[player] = true;
				std::vector<int> observedDeck;
				for (CardRef ref : child->players[player].deck) if (!ref.isNull())
					observedDeck.push_back(child->getCard(ref).cardId);
				std::sort(observedDeck.begin(), observedDeck.end());
				knowledge[observer].publicFacts.push_back('V');
				appendSemantic(knowledge[observer].publicFacts, player);
				knowledge[observer].knownDeckCounts[player].clear();
				for (int id : observedDeck) { appendSemantic(knowledge[observer].publicFacts, id);
					knowledge[observer].knownDeckCounts[player][id]++; }
				knowledge[observer].observationSequence++;
				try { if (!advance(*child, action)) return false; } catch (...) { return false; }
				partial.worlds.push_back({ std::move(child), worldWeight, std::move(knowledge) });
				partial.generated += worldWeight;
				partialBytes += sizeof(State) + sizeof(BeliefWorld) + 128;
				metrics.enumeratedHiddenWorlds++;
		}
		if (partial.generated != expected) {
			metrics.chanceMassMismatches++; metrics.probabilityExact = false; return false;
		}
		output.reserve(output.size() + partial.worlds.size());
		for (const BeliefWorld& world : partial.worlds)
			output.push_back({ std::make_unique<State>(*world.state), world.weight, world.knowledge });
		return true;
	}

	ExactScore revealAndReplay(const State& parent, const ExactHiddenState& request,
		const std::vector<int>& action) {
		if (request.pendingEffectCardId == 1197 && pendingArea(request) == AreaType::Hand)
			return solveProvisionalXerosicStreaming(parent, request, action);
		// A player who searches their own complete deck can distinguish every
		// (hand, prize) allocation represented below.  Different hand counts are
		// visible in their hand; with equal hand counts, different prize counts
		// imply a different observed deck multiset.  Consequently every allocation
		// is a singleton information set and may be evaluated as an exact streaming
		// chance outcome.  This avoids retaining tens of thousands of full State
		// copies without introducing strategy fusion.
		if (request.pendingPlayer == actor && parent.selectPlayer == actor
			&& pendingArea(request) == AreaType::Deck)
			return revealAndReplayOwnDeckStreaming(parent, request, action);
		std::vector<BeliefWorld> worlds;
		auto knowledge = initialKnowledge();
		if (!expandRevealBelief(parent, request, action, ExactWeight(1), knowledge, worlds) || worlds.empty()) return unknown();
		metrics.beliefWorldsBefore += worlds.size();
		return solveBelief(std::move(worlds));
	}

	bool isFixedTurnOneSearchRequest(const State& parent, const ExactHiddenState& request) const {
		if (parent.turn != 1 || !isMajkelFixedProfile(actorProfileCount)) return false;
		for (CardRef ref : parent.players[actor].hand)
			if (!ref.isNull() && parent.getCard(ref).cardId == 13) return false;
		return request.pendingEffectCardId == 1152 || request.pendingEffectCardId == 1086
			|| request.pendingEffectCardId == 19;
	}

	ExactScore revealAndReplayFixedTurnOneSearch(const State& parent, const ExactHiddenState& request,
		const std::vector<int>& action, int prizeSize, int totalHidden, const ExactWeight& totalWeight) {
		const int player = request.pendingPlayer;
		std::string revealKey = keyFor(parent) + "\x1fR5-FIXED-TURN1\x1f" + actionEquivalenceKey(parent, action);
		appendSemantic(revealKey, request.pendingDetail); appendSemantic(revealKey, request.pendingEffectCardId);
		appendSemantic(revealKey, request.pendingEffectPlayer);
		auto [found, inserted] = partialFixedTurnReveals.try_emplace(revealKey);
		PartialFixedTurnRevealEntry& partial = found->second;
		if (inserted) {
			static constexpr std::array<int, 6> relevantIds{ 66, 305, 343, 741, 742, 743 };
			std::array<int, relevantIds.size()> typeIndex{}; typeIndex.fill(-1);
			std::vector<int> relevantBounds(relevantIds.size(), 0);
			std::vector<bool> relevantType(parent.exact.typeCount[player], false);
			int totalRelevant = 0;
			for (int i = 0; i < parent.exact.typeCount[player]; ++i) {
				for (int r = 0; r < (int)relevantIds.size(); ++r) if (parent.exact.cardId[player][i] == relevantIds[r]) {
					typeIndex[r] = i; relevantBounds[r] = parent.exact.cardCount[player][i];
					relevantType[i] = true; totalRelevant += relevantBounds[r]; break;
				}
			}
			const int totalIrrelevant = totalHidden - totalRelevant;
			ExactWeight generated;
			for (int relevantPrizeTotal = 0; relevantPrizeTotal <= std::min(prizeSize, totalRelevant); ++relevantPrizeTotal) {
				int irrelevantPrizeTotal = prizeSize - relevantPrizeTotal;
				if (irrelevantPrizeTotal < 0 || irrelevantPrizeTotal > totalIrrelevant) continue;
				BoundedCompositionCursor cursor; cursor.reset(relevantBounds, relevantPrizeTotal);
				std::vector<int> relevantCounts;
				while (cursor.next(relevantCounts)) {
					FixedTurnRevealAllocation allocation;
					allocation.prizeCounts.assign(parent.exact.typeCount[player], 0);
					allocation.weight = chooseCount(totalIrrelevant, irrelevantPrizeTotal);
					for (int r = 0; r < (int)relevantIds.size(); ++r) {
						if (typeIndex[r] >= 0) allocation.prizeCounts[typeIndex[r]] = relevantCounts[r];
						allocation.weight = ExactWeight::multiply(allocation.weight,
							chooseCount(relevantBounds[r], relevantCounts[r]));
					}
					int left = irrelevantPrizeTotal;
					for (int i = 0; i < parent.exact.typeCount[player] && left > 0; ++i) if (!relevantType[i]) {
						int take = std::min(left, (int)parent.exact.cardCount[player][i]);
						allocation.prizeCounts[i] = take; left -= take;
					}
					if (left != 0 || allocation.weight.zero()) continue;
					generated += allocation.weight;
					partial.allocations.push_back(std::move(allocation));
				}
			}
			if (generated != totalWeight) {
				metrics.chanceMassMismatches++; metrics.probabilityExact = false;
				partialFixedTurnReveals.erase(found); return unknown();
			}
			partial.totalWeight = totalWeight;
			partial.accountedBytes = revealKey.size() + sizeof(PartialFixedTurnRevealEntry);
			for (const auto& allocation : partial.allocations)
				partial.accountedBytes += sizeof(FixedTurnRevealAllocation) + allocation.prizeCounts.size() * sizeof(int);
			partialBytes += partial.accountedBytes;
		} else {
			metrics.partialRevealHits++;
			if (partial.totalWeight != totalWeight) return unknown();
		}
		auto incomplete = [&](const ExactScore* current = nullptr, ExactWeight currentWeight = {}) {
			ExactFraction lower = partial.completedLower, upper = partial.completedUpper;
			ExactWeight covered = partial.processedWeight;
			if (current != nullptr) {
				lower = ExactFraction::add(lower, current->lower.scaled(currentWeight, totalWeight));
				upper = ExactFraction::add(upper, current->upper.scaled(currentWeight, totalWeight));
				covered += currentWeight;
			}
			ExactWeight remaining = covered >= totalWeight ? ExactWeight() : ExactWeight::subtract(totalWeight, covered);
			lower = ExactFraction::add(lower, ExactFraction::integer(-100'000'000).scaled(remaining, totalWeight));
			upper = ExactFraction::add(upper, ExactFraction::integer(100'000'000).scaled(remaining, totalWeight));
			return ExactScore{ lower, upper, {}, false };
		};
		std::vector<int> handCounts(parent.exact.typeCount[player], 0);
		while (partial.index < partial.allocations.size()) {
			if (expired()) { metrics.partialChanceNodes++; return incomplete(); }
			const FixedTurnRevealAllocation& allocation = partial.allocations[partial.index];
			auto world = std::make_unique<State>(parent);
			try {
				materializeUnknownZones(*world, player, allocation.prizeCounts, handCounts);
				if (!advance(*world, action)) return unknown();
			} catch (...) { return unknown(); }
			std::string quotientKey = observationKeyFor(*world, actor, nullptr, false);
			static constexpr std::array<int, 6> relevantIds{ 66, 305, 343, 741, 742, 743 };
			quotientKey += "\x1f" "TURN1-SEARCH";
			for (int id : relevantIds) {
				int count = 0;
				for (CardRef ref : world->players[actor].deck) if (!ref.isNull() && world->getCard(ref).cardId == id) count++;
				appendSemantic(quotientKey, count);
			}
			std::string sharedKey = "FIXED-TURN1-SEARCH\x1f" + quotientKey;
			ExactScore score;
			bool sharedHit = false;
			auto local = fixedFirstTurnRevealScores.find(quotientKey);
			bool cacheHit = local != fixedFirstTurnRevealScores.end();
			if (cacheHit) score = local->second;
			else if (usingSharedTable) cacheHit = sharedHit = transposition->find(sharedKey, score);
			if (cacheHit) {
				metrics.successorMerges++; metrics.merged++;
				if (sharedHit) metrics.rootSharedTTHits++;
			} else {
				struct StreamingGuard { bool& flag; bool previous;
					StreamingGuard(bool& value) : flag(value), previous(value) { flag = true; }
					~StreamingGuard() { flag = previous; }
				} streaming(singletonRevealStreaming);
				// Root workers normally yield every 20k nodes.  A concrete search
				// world cannot retain partial TT entries in streaming mode, so yielding
				// halfway through it would restart the same world forever.  Give one
				// world a bounded larger quantum; the wall-clock and RSS checks remain
				// active inside every expansion.
				struct QuantumGuard { unsigned long long& limit; unsigned long long previous;
					QuantumGuard(unsigned long long& value, unsigned long long expanded)
						: limit(value), previous(value) {
						if (value != std::numeric_limits<unsigned long long>::max())
							value = std::max(value, expanded + 500'000ULL);
					}
					~QuantumGuard() { limit = previous; }
				} quantum(nodeQuantumDeadline, metrics.expanded);
				score = solveOwned(std::move(world));
				if (score.certified) {
					fixedFirstTurnRevealScores.emplace(std::move(quotientKey), score);
					if (usingSharedTable) transposition->store(std::move(sharedKey), score);
				}
			}
			if (!score.certified) return incomplete(&score, allocation.weight);
			partial.completedLower = ExactFraction::add(partial.completedLower,
				score.lower.scaled(allocation.weight, totalWeight));
			partial.completedUpper = ExactFraction::add(partial.completedUpper,
				score.upper.scaled(allocation.weight, totalWeight));
			partial.processedWeight += allocation.weight; noteWeight(partial.processedWeight);
			partial.index++; metrics.enumeratedHiddenWorlds++;
		}
		if (partial.processedWeight != totalWeight) {
			metrics.chanceMassMismatches++; metrics.probabilityExact = false; return unknown();
		}
		ExactScore result{ partial.completedLower, partial.completedUpper, {},
			ExactCompare(partial.completedLower, partial.completedUpper) == 0 };
		partialBytes -= std::min(partialBytes, partial.accountedBytes);
		partialFixedTurnReveals.erase(revealKey);
		return result;
	}

	ExactScore revealAndReplayOwnDeckStreaming(const State& parent, const ExactHiddenState& request,
		const std::vector<int>& action) {
		int player = request.pendingPlayer;
		if (player != actor || parent.selectPlayer != actor
			|| pendingArea(request) != AreaType::Deck) return unknown();
		if (!parent.exact.profileKnown[player]) return unknown();
		int prizeSize = 0; for (CardRef ref : parent.players[player].prize) if (ref.isNull()) prizeSize++;
		int handSize = 0; for (CardRef ref : parent.players[player].hand) if (ref.isNull()) handSize++;
		int totalHidden = 0;
		for (int i = 0; i < parent.exact.typeCount[player]; ++i) totalHidden += parent.exact.cardCount[player][i];
		if (prizeSize < 0 || prizeSize > totalHidden) return unknown();
		ExactWeight totalWeight = ExactWeight::multiply(chooseCount(totalHidden, prizeSize),
			chooseCount(totalHidden - prizeSize, handSize));
		noteWeight(totalWeight);
		if (totalWeight.zero()) return unknown();
		if (handSize == 0 && isFixedTurnOneSearchRequest(parent, request))
			return revealAndReplayFixedTurnOneSearch(parent, request, action, prizeSize, totalHidden, totalWeight);
		std::string revealKey = keyFor(parent) + "\x1fR4\x1f" + actionEquivalenceKey(parent, action);
		appendSemantic(revealKey, request.pendingDetail);
		appendSemantic(revealKey, request.pendingEffectCardId);
		appendSemantic(revealKey, request.pendingEffectPlayer);
		PartialRevealEntry* partial = partialRevealFor(revealKey, parent.exact.typeCount[player]);
		if (partial == nullptr) return unknown();
		if (!partial->initialized) {
			std::vector<int> bounds(parent.exact.typeCount[player]);
			for (int i = 0; i < (int)bounds.size(); ++i) bounds[i] = parent.exact.cardCount[player][i];
			partial->prizeCursor.reset(bounds, prizeSize);
			partial->prizeCounts.assign(bounds.size(), 0);
			partial->handCounts.assign(bounds.size(), 0);
			partial->totalWeight = totalWeight; partial->initialized = true;
		} else if (partial->totalWeight != totalWeight) {
			return unknown();
		}
		auto incomplete = [&](const ExactScore* current = nullptr, ExactWeight currentWeight = {}) {
			ExactFraction lower = partial->completedLower, upper = partial->completedUpper;
			ExactWeight covered = partial->processedWeight;
			if (current != nullptr) {
				lower = ExactFraction::add(lower, current->lower.scaled(currentWeight, totalWeight));
				upper = ExactFraction::add(upper, current->upper.scaled(currentWeight, totalWeight));
				covered = ExactWeight::add(covered, currentWeight); noteWeight(covered);
			}
			ExactWeight remaining = covered >= totalWeight ? ExactWeight() : ExactWeight::subtract(totalWeight, covered);
			lower = ExactFraction::add(lower, ExactFraction::integer(-100'000'000).scaled(remaining, totalWeight));
			upper = ExactFraction::add(upper, ExactFraction::integer(100'000'000).scaled(remaining, totalWeight));
			if (!lower.valid || !upper.valid) { metrics.arithmeticOverflow = true; return unknown(); }
			return ExactScore{ lower, upper, {}, false };
		};
		while (true) {
			if (expired()) { metrics.partialChanceNodes++; return incomplete(); }
			if (!partial->handActive) {
				if (!partial->prizeCursor.next(partial->prizeCounts)) {
					if (partial->processedWeight != totalWeight) return unknown();
					ExactScore result{ partial->completedLower, partial->completedUpper, {},
						ExactCompare(partial->completedLower, partial->completedUpper) == 0 };
					partialBytes -= std::min(partialBytes, partial->accountedBytes);
					partialReveals.erase(revealKey);
					return result;
				}
				std::vector<int> handBounds(parent.exact.typeCount[player]);
				for (int i = 0; i < (int)handBounds.size(); ++i)
					handBounds[i] = parent.exact.cardCount[player][i] - partial->prizeCounts[i];
				partial->handCursor.reset(handBounds, handSize);
				partial->handActive = true;
			}
			if (!partial->pendingWorld) {
				if (!partial->handCursor.next(partial->handCounts)) {
					partial->handActive = false; continue;
				}
				ExactWeight weight(1);
				for (int i = 0; i < parent.exact.typeCount[player]; ++i) {
					int available = parent.exact.cardCount[player][i];
					weight = ExactWeight::multiply(weight, chooseCount(available, partial->prizeCounts[i]));
					weight = ExactWeight::multiply(weight,
						chooseCount(available - partial->prizeCounts[i], partial->handCounts[i]));
				}
				noteWeight(weight);
				partial->pendingWeight = weight; partial->pendingWorld = true;
			}
			auto world = std::make_unique<State>(parent);
			try {
				materializeUnknownZones(*world, player, partial->prizeCounts, partial->handCounts);
				if (!advance(*world, action)) return unknown();
			} catch (...) { return unknown(); }
			ExactScore score;
			const bool fixedTurnOneSearch = parent.turn == 1
				&& (request.pendingEffectCardId == 1152 || request.pendingEffectCardId == 1086
					|| request.pendingEffectCardId == 19)
				&& isMajkelFixedProfile(actorProfileCount)
				&& std::none_of(parent.players[actor].hand.begin(), parent.players[actor].hand.end(),
					[&](CardRef ref) { return !ref.isNull() && parent.getCard(ref).cardId == 13; });
			std::string quotientKey;
			std::string sharedQuotientKey;
			if (fixedTurnOneSearch) {
				// Poké Pad can access every non-Rule-Box Pokémon and Buddy-Buddy
				// Poffin can access the Basic subset.  The six counts below therefore
				// determine every later turn-one deck-query result in this fixed deck.
				// The remaining identities cannot affect a transition before the leaf,
				// and V3's hidden features are derived from profile/public zones.
				quotientKey = observationKeyFor(*world, actor, nullptr, false);
				static constexpr std::array<int, 6> relevantIds{ 66, 305, 343, 741, 742, 743 };
				std::array<int, relevantIds.size()> deckCounts{};
				for (CardRef ref : world->players[actor].deck) if (!ref.isNull()) {
					int id = world->getCard(ref).cardId;
					for (int i = 0; i < (int)relevantIds.size(); ++i)
						if (id == relevantIds[i]) { deckCounts[i]++; break; }
				}
				quotientKey += "\x1f" "TURN1-SEARCH";
				for (int count : deckCounts) appendSemantic(quotientKey, count);
				sharedQuotientKey = "FIXED-TURN1-SEARCH\x1f" + quotientKey;
				bool sharedHit = false;
				auto cached = fixedFirstTurnRevealScores.find(quotientKey);
				bool cacheHit = cached != fixedFirstTurnRevealScores.end();
				if (cacheHit) score = cached->second;
				else if (usingSharedTable) cacheHit = sharedHit = transposition->find(sharedQuotientKey, score);
				if (cacheHit) {
					metrics.successorMerges++; metrics.merged++;
					if (sharedHit) metrics.rootSharedTTHits++;
				}
			}
			if (!score.certified) {
				struct StreamingGuard {
					bool& flag; bool previous;
					StreamingGuard(bool& value) : flag(value), previous(value) { flag = true; }
					~StreamingGuard() { flag = previous; }
				} streaming(singletonRevealStreaming);
				score = solveOwned(std::move(world));
				if (fixedTurnOneSearch && score.certified) {
					fixedFirstTurnRevealScores.emplace(std::move(quotientKey), score);
					if (usingSharedTable) transposition->store(std::move(sharedQuotientKey), score);
				}
			}
			if (!score.certified) return incomplete(&score, partial->pendingWeight);
			partial->completedLower = ExactFraction::add(partial->completedLower,
				score.lower.scaled(partial->pendingWeight, totalWeight));
			partial->completedUpper = ExactFraction::add(partial->completedUpper,
				score.upper.scaled(partial->pendingWeight, totalWeight));
			if (!partial->completedLower.valid || !partial->completedUpper.valid) {
				metrics.arithmeticOverflow = true; return unknown();
			}
			partial->processedWeight += partial->pendingWeight; noteWeight(partial->processedWeight);
			metrics.enumeratedHiddenWorlds++;
			partial->pendingWeight = ExactWeight(); partial->pendingWorld = false;
		}
	}

	std::string beliefWorldKey(const BeliefWorld& world) const {
		std::string key = keyFor(*world.state);
		for (int observer = 0; observer < 2; ++observer) {
			appendKnowledgeKey(key, world.knowledge[observer]);
		}
		return key;
	}

	void normalizeBelief(std::vector<BeliefWorld>& worlds) {
		std::unordered_map<std::string, size_t, ExactStringHasher> byWorld;
		std::vector<BeliefWorld> normalized;
		normalized.reserve(worlds.size());
		for (BeliefWorld& world : worlds) {
			std::string key = beliefWorldKey(world);
			auto [found, inserted] = byWorld.emplace(std::move(key), normalized.size());
			if (inserted) normalized.push_back(std::move(world));
			else {
				normalized[found->second].weight += world.weight;
				metrics.beliefWorldsAfter++;
			}
		}
		worlds = std::move(normalized);
		ExactWeight common;
		for (const BeliefWorld& world : worlds)
			common = common.zero() ? world.weight : ExactWeight::gcd(common, world.weight);
		if (!common.zero() && common != ExactWeight(1)) {
			for (BeliefWorld& world : worlds) {
				auto divided = ExactWeight::divideRemainder(world.weight, common);
				if (!divided.second.zero()) throw std::runtime_error("belief GCD normalization failed");
				world.weight = std::move(divided.first);
			}
		}
	}

	ExactWeight beliefMass(const std::vector<BeliefWorld>& worlds) const {
		ExactWeight total; for (const BeliefWorld& world : worlds) total += world.weight; return total;
	}

	ExactScore aggregateBeliefScores(const std::vector<std::pair<ExactScore, ExactWeight>>& scores,
		const ExactWeight& total) {
		if (scores.empty() || total.zero()) return unknown();
		ExactFraction lower = ExactFraction::integer(0), upper = ExactFraction::integer(0);
		bool certified = true;
		ExactWeight processed;
		for (const auto& item : scores) {
			lower = ExactFraction::add(lower, item.first.lower.scaled(item.second, total));
			upper = ExactFraction::add(upper, item.first.upper.scaled(item.second, total));
			processed += item.second; certified = certified && item.first.certified;
		}
		if (processed != total) {
			metrics.chanceMassMismatches++; metrics.probabilityExact = false; return unknown();
		}
		return { lower, upper, {}, certified && ExactCompare(lower, upper) == 0 };
	}

	bool settleBeliefState(State& state) {
		try {
			while (!state.isFinish() && !IsExactTurnLeaf(state)
				&& state.exact.pending == ExactPendingType::None && state.selectType == SelectType::None) {
				state.step(); metrics.expanded++;
				if (expired()) return false;
			}
			return true;
		} catch (const std::exception& error) {
			metrics.exceptions++; metrics.lastException = error.what(); return false;
		} catch (...) {
			metrics.exceptions++; metrics.lastException = "belief automatic transition"; return false;
		}
	}

	std::string beliefControlKey(const BeliefWorld& world) const {
		const State& state = *world.state;
		std::string key;
		if (state.isFinish()) key = "F";
		else if (IsExactTurnLeaf(state)) key = "L";
		else if (state.exact.pending != ExactPendingType::None) {
			key = "P"; appendSemantic(key, (int)state.exact.pending);
			appendSemantic(key, state.exact.pendingPlayer); appendSemantic(key, state.exact.pendingCount);
			if (state.exact.pendingPlayer >= 0) {
				const PlayerState& ps = state.players[state.exact.pendingPlayer];
				appendSemantic(key, state.exact.pending == ExactPendingType::Draw ? ps.deck.size() : ps.prize.size());
			}
		} else if (state.selectType == SelectType::YesNo && state.selectContext == SelectContext::CoinHead) key = "C";
		else {
			key = "D"; appendSemantic(key, state.selectPlayer); appendSemantic(key, (int)state.selectType);
		}
		return key;
	}

	ExactScore solveBeliefInformationSet(std::vector<BeliefWorld> worlds) {
		if (worlds.empty()) return unknown();
		State& representative = *worlds.front().state;
		const int decisionPlayer = representative.selectPlayer;
		const std::string expectedObservation = observationKeyFor(representative, decisionPlayer,
			&worlds.front().knowledge[decisionPlayer]);
		for (const BeliefWorld& world : worlds) {
			if (world.state->selectPlayer != decisionPlayer
				|| observationKeyFor(*world.state, decisionPlayer, &world.knowledge[decisionPlayer]) != expectedObservation) {
				metrics.illegalInformationSetSplits++; metrics.informationSetSafe = false;
				metrics.hiddenInformationLeakDetected = true; return unknown();
			}
		}
		metrics.informationSets++;
		if (worlds.size() > 1) metrics.strategyFusionPrevented += worlds.size() - 1;
		struct CommonAction { std::vector<int> representative; std::vector<std::string> semantic; };
		std::vector<CommonAction> actions;
		if (!forEachLegalAction(representative, [&](const std::vector<int>& action) {
			actions.push_back({ action, semanticAction(representative, action) }); return true;
		})) return unknown();
		if (actions.empty()) return unknown();
		const bool maximize = decisionPlayer == actor;
		ExactScore best;
		bool first = true;
		for (const CommonAction& common : actions) {
			if (expired()) return unknown();
			std::vector<BeliefWorld> children;
			bool provisionalBeliefDeferred = false;
			for (const BeliefWorld& world : worlds) {
				std::vector<int> mapped;
				if (!remapAction(*world.state, common.semantic, mapped)) {
					metrics.illegalInformationSetSplits++; metrics.informationSetSafe = false;
					metrics.hiddenInformationLeakDetected = true; return unknown();
				}
				auto child = std::make_unique<State>(*world.state);
				if (!advance(*child, mapped)) return unknown();
				if (child->exact.pending == ExactPendingType::RevealDeck) {
					if (child->exact.pendingEffectCardId == 1197 && pendingArea(child->exact) == AreaType::Hand) {
						// The bootstrap policy is streamed only from a single concrete
						// root.  Expanding it inside another belief would multiply large
						// State sets, so retain an honest unproven interval for now.
						provisionalBeliefDeferred = true; metrics.provisionalOpponentPolicyNodes++; break;
					}
					if (!expandRevealBelief(*world.state, child->exact, mapped, world.weight, world.knowledge, children)) return unknown();
				} else children.push_back({ std::move(child), world.weight, world.knowledge });
			}
			ExactScore score = provisionalBeliefDeferred ? unknown() : solveBelief(std::move(children));
			if (first || (maximize ? ExactCompare(score.lower, best.lower) > 0
				: ExactCompare(score.upper, best.upper) < 0)) {
				best = score; best.action = common.representative; first = false;
			}
		}
		return best;
	}

	ExactScore solveBelief(std::vector<BeliefWorld> worlds) {
		struct DepthGuard { int& value; DepthGuard(int& v) : value(v) { ++value; } ~DepthGuard() { --value; } } guard(recursionDepth);
		if (worlds.empty() || recursionDepth > 384 || expired()) return unknown();
		metrics.beliefNodes++; metrics.expanded++;
		for (BeliefWorld& world : worlds) if (!settleBeliefState(*world.state)) return unknown();
		normalizeBelief(worlds);
		ExactWeight total = beliefMass(worlds); noteWeight(total, false);
		std::vector<std::string> beliefKeyParts;
		beliefKeyParts.reserve(worlds.size());
		for (const BeliefWorld& world : worlds)
			beliefKeyParts.push_back(beliefWorldKey(world) + "@" + world.weight.text());
		std::sort(beliefKeyParts.begin(), beliefKeyParts.end());
		PlayerInformationStateV3 informationState;
		informationState.observer = actor; informationState.environmentPriorId = environmentPriorId;
		informationState.evaluatorSchema = evaluator ? evaluator->schemaVersion() : 0;
		informationState.evaluatorModel = evaluator ? evaluator->modelHash() : 0;
		informationState.normalizedWorlds = std::move(beliefKeyParts);
		std::string beliefKey = informationState.canonicalKey();
		auto cachedBelief = beliefTransposition.find(beliefKey);
		if (cachedBelief != beliefTransposition.end()) { metrics.merged++; return cachedBelief->second; }
		auto finish = [&](ExactScore result) {
			if (result.certified && beliefTransposition.size() < 250'000
				&& beliefTranspositionBytes + beliefKey.size() + sizeof(ExactScore) + 64 < 256ULL * 1024ULL * 1024ULL) {
				auto [_, inserted] = beliefTransposition.emplace(beliefKey, result);
				if (inserted) beliefTranspositionBytes += beliefKey.size() + sizeof(ExactScore) + 64;
			}
			return result;
		};

		std::unordered_map<std::string, std::vector<BeliefWorld>, ExactStringHasher> controlGroups;
		for (BeliefWorld& world : worlds) controlGroups[beliefControlKey(world)].push_back(std::move(world));
		if (controlGroups.size() > 1) {
			std::vector<std::pair<ExactScore, ExactWeight>> scores;
			for (auto& item : controlGroups) {
				ExactWeight mass = beliefMass(item.second);
				scores.push_back({ solveBelief(std::move(item.second)), mass });
			}
			return finish(aggregateBeliefScores(scores, total));
		}
		worlds = std::move(controlGroups.begin()->second);
		State& representative = *worlds.front().state;

		if (representative.isFinish() || IsExactTurnLeaf(representative)) {
			std::unordered_map<std::string, std::vector<BeliefWorld>, ExactStringHasher> observations;
			for (BeliefWorld& world : worlds) {
				observations[observationKeyFor(*world.state, actor, &world.knowledge[actor])].push_back(std::move(world));
			}
			std::vector<std::pair<ExactScore, ExactWeight>> scores;
			for (auto& item : observations) {
				ExactWeight mass = beliefMass(item.second);
				bool policyCertified = true;
				for (const BeliefWorld& world : item.second)
					policyCertified = policyCertified && !world.state->exact.provisionalOpponentPolicy;
				std::vector<std::string> beliefTokens;
				for (const BeliefWorld& world : item.second)
					beliefTokens.push_back(beliefWorldKey(world) + "@" + world.weight.text());
				std::sort(beliefTokens.begin(), beliefTokens.end());
				std::string cacheKey = item.first + "#" + std::to_string(evaluator ? evaluator->modelHash() : 0);
				for (const std::string& token : beliefTokens) { appendSemantic(cacheKey, token.size()); cacheKey += token; }
				auto cached = evaluationCache.find(cacheKey);
				long long value;
				if (cached != evaluationCache.end()) value = cached->second;
				else { value = evaluateBeliefInformationState(item.second, mass); evaluationCache.emplace(std::move(cacheKey), value); }
				scores.push_back({ { ExactFraction::integer(value), ExactFraction::integer(value), {}, policyCertified }, mass });
				metrics.leaves++;
			}
			return finish(aggregateBeliefScores(scores, total));
		}

		if (representative.exact.pending == ExactPendingType::Opaque
			|| representative.exact.pending == ExactPendingType::RevealDeck) return unknown();

		if (representative.exact.pending == ExactPendingType::Draw
			|| representative.exact.pending == ExactPendingType::TakePrize) {
			std::vector<BeliefWorld> children;
			ExactWeight expected, generated;
			for (BeliefWorld& world : worlds) {
				auto types = chanceCardTypes(*world.state);
				if (types.empty()) return unknown();
				ExactWeight localTotal; for (const auto& type : types) localTotal += type.second;
				expected += ExactWeight::multiply(world.weight, localTotal);
				for (const auto& type : types) {
					auto child = std::make_unique<State>(*world.state);
					try {
					if (world.state->exact.pending == ExactPendingType::Draw) resolveDraw(*child, type.first);
					else resolvePrize(*child, type.first);
					} catch (...) { return unknown(); }
					auto knowledge = world.knowledge;
					int pendingPlayer = world.state->exact.pendingPlayer;
					appendKnowledgeFact(knowledge[pendingPlayer],
						world.state->exact.pending == ExactPendingType::Draw ? 'D' : 'P', type.first);
					if (world.state->exact.pending == ExactPendingType::Draw) {
						if (!knowledge[pendingPlayer].knownTop[pendingPlayer].empty())
							knowledge[pendingPlayer].knownTop[pendingPlayer].erase(knowledge[pendingPlayer].knownTop[pendingPlayer].begin());
						auto ownKnown = knowledge[pendingPlayer].knownDeckCounts[pendingPlayer].find(type.first);
						if (ownKnown != knowledge[pendingPlayer].knownDeckCounts[pendingPlayer].end() && --ownKnown->second <= 0)
							knowledge[pendingPlayer].knownDeckCounts[pendingPlayer].erase(ownKnown);
						for (int observer = 0; observer < 2; ++observer) if (observer != pendingPlayer) {
							knowledge[observer].deckKnown[pendingPlayer] = false;
							knowledge[observer].knownDeckCounts[pendingPlayer].clear();
							knowledge[observer].knownTop[pendingPlayer].clear();
							knowledge[observer].knownBottom[pendingPlayer].clear();
						}
					} else {
						auto known = knowledge[pendingPlayer].knownPrizeCounts[pendingPlayer].find(type.first);
						if (known != knowledge[pendingPlayer].knownPrizeCounts[pendingPlayer].end() && --known->second <= 0)
							knowledge[pendingPlayer].knownPrizeCounts[pendingPlayer].erase(known);
					}
					ExactWeight weight = ExactWeight::multiply(world.weight, type.second);
					generated += weight;
					children.push_back({ std::move(child), weight, std::move(knowledge) });
				}
			}
			if (generated != expected) {
				metrics.chanceMassMismatches++; metrics.probabilityExact = false; return unknown();
			}
			return finish(solveBelief(std::move(children)));
		}

		if (representative.selectType == SelectType::YesNo && representative.selectContext == SelectContext::CoinHead) {
			std::vector<BeliefWorld> children;
			for (BeliefWorld& world : worlds) for (int option = 0; option < 2; ++option) {
				auto child = std::make_unique<State>(*world.state);
				if (!advance(*child, { option })) return unknown();
				auto knowledge = world.knowledge;
				appendKnowledgeFact(knowledge[0], 'C', option); appendKnowledgeFact(knowledge[1], 'C', option);
				children.push_back({ std::move(child), world.weight, std::move(knowledge) });
			}
			return finish(solveBelief(std::move(children)));
		}

		std::unordered_map<std::string, std::vector<BeliefWorld>, ExactStringHasher> informationSets;
		for (BeliefWorld& world : worlds) {
			int observer = world.state->selectPlayer;
			informationSets[observationKeyFor(*world.state, observer, &world.knowledge[observer])].push_back(std::move(world));
		}
		std::vector<std::pair<ExactScore, ExactWeight>> scores;
		for (auto& item : informationSets) {
			ExactWeight mass = beliefMass(item.second);
			scores.push_back({ solveBeliefInformationSet(std::move(item.second)), mass });
		}
		return finish(aggregateBeliefScores(scores, total));
	}

	void resolveDraw(State& state, int cardId) {
		int player = state.exact.pendingPlayer;
		PlayerState& ps = state.players[player];
		int index = -1;
		if (state.exact.deckUnknown[player]) {
			for (int i = 0; i < ps.deck.size(); ++i) {
				if (!ps.deck[i].isNull() && state.getCard(ps.deck[i]).cardId == cardId) { index = i; break; }
			}
			if (index < 0) {
				for (int i = 0; i < ps.deck.size(); ++i) if (ps.deck[i].isNull()) { index = i; break; }
				if (index < 0) throw std::runtime_error("hidden deck slot missing");
				materialize(state, player, AreaType::Deck, index, cardId);
			}
			decrementPool(state, player, cardId);
		} else {
			for (int i = 0; i < ps.deck.size(); ++i) if (state.getCard(ps.deck[i]).cardId == cardId) { index = i; break; }
			if (index < 0) throw std::runtime_error("draw card missing");
		}
		CardRef ref = MoveCard(state, player, AreaType::Deck, index, AreaType::Hand);
		LogDraw(state, player, ref);
		if (--state.exact.pendingCount == 0) state.exact.clearPending();
	}

	void resolvePrize(State& state, int cardId) {
		int player = state.exact.pendingPlayer;
		PlayerState& ps = state.players[player];
		int index = -1;
		for (int i = 0; i < ps.prize.size(); ++i) if (ps.prize[i].isNull()) { index = i; break; }
		if (index < 0) {
			for (int i = 0; i < ps.prize.size(); ++i) if (state.getCard(ps.prize[i]).cardId == cardId) { index = i; break; }
		} else {
			materialize(state, player, AreaType::Prize, index, cardId); decrementPool(state, player, cardId);
		}
		CardRef ref = ps.prize[index];
		state.targetList.push_back(state.makeAreaRef(ref));
		if (--state.exact.pendingCount == 0) {
			state.exact.clearPending();
			SelectedPrizeTarget(state);
		}
	}

	std::vector<std::pair<int, ExactWeight>> chanceCardTypes(const State& state) const {
		std::vector<std::pair<int, ExactWeight>> result;
		int player = state.exact.pendingPlayer;
		if (player != actor && state.exact.deckUnknown[player] && !state.exact.profileKnown[player]) return result;
		if (state.exact.deckUnknown[player]) {
			for (int i = 0; i < state.exact.typeCount[player]; ++i) if (state.exact.cardCount[player][i] > 0)
				result.push_back({ state.exact.cardId[player][i], ExactWeight(state.exact.cardCount[player][i]) });
		} else {
			std::unordered_map<int, unsigned long long> counts;
			const auto& list = state.exact.pending == ExactPendingType::Draw ? state.players[player].deck : state.players[player].prize;
			for (CardRef ref : list) if (!ref.isNull()) counts[state.getCard(ref).cardId]++;
			for (auto [id, count] : counts) result.push_back({ id, ExactWeight(count) });
		}
		std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
			return left.first < right.first;
		});
		return result;
	}

	ExactScore multiDrawChance(const State& state, const std::string& nodeKey) {
		auto types = chanceCardTypes(state);
		const int drawCount = state.exact.pendingCount;
		if (types.empty() || drawCount <= 1) return unknown();
		std::vector<int> bounds; bounds.reserve(types.size());
		int available = 0;
		for (const auto& item : types) {
			if (!item.second.fitsUnsignedLongLong()
				|| item.second.unsignedLongLong() > (unsigned long long)std::numeric_limits<int>::max()) return unknown();
			bounds.push_back((int)item.second.unsignedLongLong()); available += bounds.back();
		}
		if (drawCount > available) return unknown();
		std::string resumeKey = nodeKey + "\x1fMULTI-DRAW";
		auto [found, inserted] = partialMultiDraws.try_emplace(resumeKey);
		PartialMultiDrawEntry& partial = found->second;
		if (inserted || !partial.initialized) {
			partial.bounds = bounds; partial.counts.assign(bounds.size(), 0);
			partial.cursor.reset(bounds, drawCount);
			partial.totalWeight = chooseCount(available, drawCount);
			partial.initialized = true;
			partial.accountedBytes = resumeKey.size() + sizeof(PartialMultiDrawEntry)
				+ bounds.size() * sizeof(int) * 8;
			partialBytes += partial.accountedBytes;
		} else if (partial.bounds != bounds || partial.totalWeight != chooseCount(available, drawCount)) {
			return unknown();
		}
		const ExactWeight& total = partial.totalWeight;
		noteWeight(total);
		auto incomplete = [&](const ExactScore* current = nullptr) {
			ExactFraction lower = partial.completedLower, upper = partial.completedUpper;
			ExactWeight covered = partial.processedWeight;
			if (current != nullptr) {
				lower = ExactFraction::add(lower, current->lower.scaled(partial.pendingWeight, total));
				upper = ExactFraction::add(upper, current->upper.scaled(partial.pendingWeight, total));
				covered += partial.pendingWeight;
			}
			ExactWeight remaining = covered >= total ? ExactWeight() : ExactWeight::subtract(total, covered);
			lower = ExactFraction::add(lower, ExactFraction::integer(-100'000'000).scaled(remaining, total));
			upper = ExactFraction::add(upper, ExactFraction::integer(100'000'000).scaled(remaining, total));
			return ExactScore{ lower, upper, {}, false };
		};
		// A combination can be much larger than the normal 20k-node root
		// scheduling quantum.  Preserve the wall-clock/RSS deadline while allowing
		// one exact multiset outcome to finish and become reusable in the TT.
		struct QuantumGuard { unsigned long long& limit; unsigned long long previous;
			QuantumGuard(unsigned long long& value, unsigned long long expanded)
				: limit(value), previous(value) {
				if (value != std::numeric_limits<unsigned long long>::max())
					value = std::max(value, expanded + 500'000ULL);
			}
			~QuantumGuard() { limit = previous; }
		} quantum(nodeQuantumDeadline, metrics.expanded);
		while (true) {
			if (!partial.pending) {
				if (!partial.cursor.next(partial.counts)) break;
				partial.pendingWeight = ExactWeight(1);
				for (int i = 0; i < (int)partial.counts.size(); ++i)
					partial.pendingWeight = ExactWeight::multiply(partial.pendingWeight,
						chooseCount(partial.bounds[i], partial.counts[i]));
				partial.pending = true;
			}
			if (expired()) {
				metrics.partialChanceNodes++;
				return incomplete();
			}
			auto child = std::make_unique<State>(state);
			try {
				for (int i = 0; i < (int)partial.counts.size(); ++i)
					for (int n = 0; n < partial.counts[i]; ++n) resolveDraw(*child, types[i].first);
			} catch (...) { return unknown(); }
			ExactScore score = solveOwned(std::move(child));
			if (!score.certified) { metrics.partialChanceNodes++; return incomplete(&score); }
			partial.completedLower = ExactFraction::add(partial.completedLower,
				score.lower.scaled(partial.pendingWeight, total));
			partial.completedUpper = ExactFraction::add(partial.completedUpper,
				score.upper.scaled(partial.pendingWeight, total));
			if (!partial.completedLower.valid || !partial.completedUpper.valid) {
				metrics.arithmeticOverflow = true; return unknown();
			}
			partial.processedWeight += partial.pendingWeight; noteWeight(partial.processedWeight);
			metrics.enumeratedHiddenWorlds++;
			partial.pendingWeight = ExactWeight(); partial.pending = false;
		}
		if (partial.processedWeight != total) {
			metrics.chanceMassMismatches++; metrics.probabilityExact = false; return unknown();
		}
		ExactScore result{ partial.completedLower, partial.completedUpper, {},
			ExactCompare(partial.completedLower, partial.completedUpper) == 0 };
		partialBytes -= std::min(partialBytes, partial.accountedBytes);
		partialMultiDraws.erase(resumeKey);
		return result;
	}

	ExactScore chance(const State& state, const std::string& nodeKey) {
		if (state.exact.pending == ExactPendingType::Draw && state.exact.pendingCount > 1
			&& (state.exact.deckUnknown[state.exact.pendingPlayer]
				|| state.exact.deckExchangeable[state.exact.pendingPlayer]))
			return multiDrawChance(state, nodeKey);
		auto types = chanceCardTypes(state);
		if (types.empty()) return unknown();
		PartialChanceEntry* partial = singletonRevealStreaming ? nullptr : partialChanceFor(nodeKey);
		ExactWeight total; for (const auto& item : types) total += item.second;
		noteWeight(total);
		ExactFraction lower = ExactFraction::integer(0), upper = ExactFraction::integer(0);
		bool certified = true;
		ExactWeight processed;
		for (const auto& item : types) {
			int id = item.first; const ExactWeight& weight = item.second;
			if (expired()) {
				metrics.partialChanceNodes++;
				ExactWeight remaining = ExactWeight::subtract(total, processed);
				lower = ExactFraction::add(lower, ExactFraction::integer(-100'000'000).scaled(remaining, total));
				upper = ExactFraction::add(upper, ExactFraction::integer(100'000'000).scaled(remaining, total));
				return { lower, upper, {}, false };
			}
			ExactScore score;
			auto saved = partial == nullptr ? nullptr : [&]() -> ExactScore* {
				auto found = partial->completedOutcomes.find(id);
				return found == partial->completedOutcomes.end() ? nullptr : &found->second;
			}();
			if (saved != nullptr) {
				score = *saved;
				if (weight.fitsUnsignedLongLong()
					&& weight.unsignedLongLong() <= std::numeric_limits<unsigned long long>::max() - metrics.resumedChanceMass)
					metrics.resumedChanceMass += weight.unsignedLongLong();
				else metrics.resumedChanceMass = std::numeric_limits<unsigned long long>::max();
			} else {
				auto child = std::make_unique<State>(state);
				try {
					if (state.exact.pending == ExactPendingType::Draw) resolveDraw(*child, id); else resolvePrize(*child, id);
				} catch (...) { return unknown(); }
				score = solveOwned(std::move(child));
				if (partial != nullptr && score.certified) partial->completedOutcomes.emplace(id, score);
			}
			auto l = score.lower.scaled(weight, total), u = score.upper.scaled(weight, total);
			lower = ExactFraction::add(lower, l); upper = ExactFraction::add(upper, u);
			processed += weight;
			if (!lower.valid || !upper.valid) { metrics.arithmeticOverflow = true; return unknown(); }
			certified = certified && score.certified;
		}
		if (processed != total) { metrics.chanceMassMismatches++; metrics.probabilityExact = false; return unknown(); }
		return { lower, upper, {}, certified && ExactCompare(lower, upper) == 0 };
	}

	ExactScore coinChance(const State& state, const std::string& nodeKey) {
		(void)nodeKey;
		struct CoinOutcome { std::unique_ptr<State> state; ExactWeight weight; };
		std::vector<CoinOutcome> outcomes;
		std::unordered_map<std::string, size_t, ExactStringHasher> bySuccessor;
		for (int option = 0; option < 2; ++option) {
			if (expired()) { metrics.partialChanceNodes++; return unknown(); }
			auto child = std::make_unique<State>(state);
			if (!advance(*child, { option })) return unknown();
			std::string key = keyFor(*child);
			auto [found, inserted] = bySuccessor.emplace(std::move(key), outcomes.size());
			if (inserted) outcomes.push_back({ std::move(child), ExactWeight(1) });
			else { outcomes[found->second].weight += ExactWeight(1); metrics.distributionMerges++; }
		}
		ExactFraction lower = ExactFraction::integer(0), upper = ExactFraction::integer(0);
		bool certified = true;
		for (CoinOutcome& outcome : outcomes) {
			if (expired()) { metrics.partialChanceNodes++; return unknown(); }
			ExactScore score = solveOwned(std::move(outcome.state));
			lower = ExactFraction::add(lower, score.lower.scaled(outcome.weight, ExactWeight(2)));
			upper = ExactFraction::add(upper, score.upper.scaled(outcome.weight, ExactWeight(2)));
			if (!lower.valid || !upper.valid) { metrics.arithmeticOverflow = true; return unknown(); }
			certified = certified && score.certified;
		}
		return { lower, upper, {}, certified && ExactCompare(lower, upper) == 0 };
	}

	ExactScore decision(const State& state, bool maximize, const std::string& nodeKey) {
		unsigned long long expandedBefore = metrics.expanded;
		ExactScore result;
		bool first = true;
		ExactFraction aggregate = maximize ? ExactFraction::integer(-100'000'000) : ExactFraction::integer(100'000'000);
		bool allCertified = true;
		std::unordered_set<std::string> equivalentActions;
		std::unordered_map<std::string, std::pair<ExactScore, unsigned long long>, ExactStringHasher> successorScores;
		PartialDecisionEntry* partial = singletonRevealStreaming ? nullptr : partialDecisionFor(nodeKey);
		size_t actionOrdinal = 0;
		bool completed = forEachLegalAction(state, [&](const std::vector<int>& action) {
			metrics.rawOutcomes++;
			std::string actionKey = actionEquivalenceKey(state, action);
			if (!equivalentActions.insert(actionKey).second) { metrics.groupedOutcomes++; return true; }
			const size_t thisOrdinal = actionOrdinal++;
			ExactScore* savedAction = nullptr;
			if (partial != nullptr) {
				auto found = partial->actionBounds.find(actionKey);
				if (found != partial->actionBounds.end()) savedAction = &found->second;
			}
			if (savedAction != nullptr && (savedAction->certified || thisOrdinal < partial->resumeOrdinal)) {
				ExactScore score = *savedAction;
				if (!score.certified) metrics.resumedActionCount++;
				if (maximize) { if (ExactCompare(score.upper, aggregate) > 0) aggregate = score.upper; }
				else { if (ExactCompare(score.lower, aggregate) < 0) aggregate = score.lower; }
				allCertified = allCertified && score.certified;
				if (first || (maximize ? ExactCompare(score.lower, result.lower) > 0 : ExactCompare(score.upper, result.upper) < 0)) {
					result = score; result.action = action; first = false;
				}
				if (recursionDepth == 1)
					rootActionValues.push_back({ action, score.lower, score.upper, score.certified });
				return true;
			}
			auto child = std::make_unique<State>(state);
			if (!advance(*child, action)) return true;
			ExactScore score;
			if (!singletonRevealStreaming && canonicalMainEnabled && child->selectType == SelectType::Main
				&& child->exact.pending == ExactPendingType::None) {
				std::string successorKey = keyFor(*child);
				auto successor = successorScores.find(successorKey);
				if (successor != successorScores.end()) {
					score = successor->second.first;
					successor->second.second++;
					metrics.successorMerges++; metrics.groupedOutcomes++;
					metrics.largestEquivalenceClass = std::max(metrics.largestEquivalenceClass, successor->second.second);
				} else {
					score = solveOwned(std::move(child));
					successorScores.emplace(std::move(successorKey), std::make_pair(score, 1ULL));
				}
			} else {
				score = child->exact.pending == ExactPendingType::RevealDeck
					? revealAndReplay(state, child->exact, action) : solveOwned(std::move(child));
			}
			if (partial != nullptr) {
				auto found = partial->actionBounds.find(actionKey);
				if (found == partial->actionBounds.end()) {
					size_t bytes = actionKey.size() + sizeof(ExactScore) + 32;
					partialBytes += bytes; partial->accountedBytes += bytes;
					partial->actionBounds.emplace(actionKey, score);
				} else {
					if (ExactCompare(score.lower, found->second.lower) > 0) found->second.lower = score.lower;
					if (ExactCompare(score.upper, found->second.upper) < 0) found->second.upper = score.upper;
					found->second.certified = ExactCompare(found->second.lower, found->second.upper) == 0;
					score = found->second;
					metrics.resumedActionCount++;
				}
				partial->resumeOrdinal = thisOrdinal + 1;
			}
			if (recursionDepth == 1)
				rootActionValues.push_back({ action, score.lower, score.upper, score.certified });
			if (maximize) {
				if (ExactCompare(score.upper, aggregate) > 0) aggregate = score.upper;
			} else {
				if (ExactCompare(score.lower, aggregate) < 0) aggregate = score.lower;
			}
			allCertified = allCertified && score.certified;
			if (first || (maximize ? ExactCompare(score.lower, result.lower) > 0 : ExactCompare(score.upper, result.upper) < 0)) {
				result = score; result.action = action; first = false;
			}
			return !expired();
		});
		if (completed && partial != nullptr) partial->resumeOrdinal = 0;
		if (first) return unknown();
		if (!completed) {
			if (maximize) result.upper = ExactFraction::integer(100'000'000);
			else result.lower = ExactFraction::integer(-100'000'000);
			result.certified = false;
			metrics.partialDecisionNodes++;
			if (!singletonRevealStreaming) rememberPolicy(state, result, metrics.expanded - expandedBefore);
			return result;
		}
		if (maximize) result.upper = aggregate; else result.lower = aggregate;
		result.certified = allCertified && ExactCompare(result.lower, result.upper) == 0;
		if (!singletonRevealStreaming) rememberPolicy(state, result, metrics.expanded - expandedBefore);
		return result;
	}

	ExactScore solve(const State& input) { return solveOwned(std::make_unique<State>(input)); }

	ExactScore solveOwned(std::unique_ptr<State> owned) {
		struct DepthGuard { int& depth; DepthGuard(int& value) : depth(value) { depth++; } ~DepthGuard() { depth--; } } guard(recursionDepth);
		metrics.maxDepth = std::max(metrics.maxDepth, recursionDepth);
		if (recursionDepth > 384) {
			metrics.depthLimitNodes++;
			metrics.lastDepthSelectType = (int)owned->selectType;
			metrics.lastDepthTurnActionCount = owned->turnActionCount;
			return unknown();
		}
		State& state = *owned;
		metrics.expanded++;
		if (expired()) return unknown();
		// Automatic engine continuations are not search-tree depth.  Running
		// each one through solve() recursively can exhaust the C++ stack long
		// before the wall-clock budget, so settle them iteratively.
		try {
			while (!state.isFinish() && !IsExactTurnLeaf(state)
				&& state.exact.pending == ExactPendingType::None && state.selectType == SelectType::None) {
				state.step();
				metrics.expanded++;
				if (expired()) return unknown();
			}
		} catch (const std::exception& error) {
			metrics.exceptions++; metrics.lastException = error.what(); return unknown();
		} catch (...) {
			metrics.exceptions++; metrics.lastException = "automatic transition"; return unknown();
		}
		// Terminal turn leaves are cheap to evaluate and overwhelmingly unique.
		// Storing their full State keys consumed the TT before any reusable Main
		// node could be completed.
		if (state.isFinish() || IsExactTurnLeaf(state)) {
			metrics.leaves++;
			auto value = ExactFraction::integer(evaluate(state));
			return { value, value, {}, !state.exact.provisionalOpponentPolicy };
		}
		// A search has made this concrete world a singleton information set, but
		// turn-one Main states still contain many commuting action orders.  The
		// compact fixed-deck key preserves every card count that can affect another
		// turn-one deck query while omitting the irrelevant prize identities already
		// quotiented by revealAndReplayFixedTurnOneSearch.
		std::string fixedMainKey = fixedTurnOneMainKey(state);
		if (!fixedMainKey.empty()) {
			ExactScore fixedCached;
			auto local = fixedFirstTurnMainScores.find(fixedMainKey);
			bool fixedHit = local != fixedFirstTurnMainScores.end();
			bool sharedHit = false;
			if (fixedHit) fixedCached = local->second;
			else if (usingSharedTable) fixedHit = sharedHit = transposition->find(fixedMainKey, fixedCached);
			if (fixedHit) {
				metrics.merged++; metrics.canonicalStateMerges++;
				if (sharedHit) metrics.rootSharedTTHits++;
				return fixedCached;
			}
		}
		std::string key;
		if (!singletonRevealStreaming) key = keyFor(state);
		const bool shareable = usingSharedTable && !singletonRevealStreaming;
		ExactScore cached;
		bool cacheHit = false;
		if (shareable) cacheHit = transposition->find(key, cached);
		else if (!singletonRevealStreaming) {
			auto found = localTransposition.find(key);
			if (found != localTransposition.end()) { cached = found->second; cacheHit = true; }
		}
		if (cacheHit) {
			metrics.merged++;
			if (shareable) metrics.canonicalStateMerges++;
			if (shareable && usingSharedTable) metrics.rootSharedTTHits++;
			return cached;
		}
		ExactScore result;
		if (state.exact.pending == ExactPendingType::Opaque || state.exact.pending == ExactPendingType::RevealDeck) {
			metrics.opaque++; metrics.lastPendingDetail = state.exact.pendingDetail;
			metrics.lastPendingPlayer = state.exact.pendingPlayer;
			metrics.lastPendingEffectCardId = state.exact.pendingEffectCardId;
			metrics.lastPendingEffectPlayer = state.exact.pendingEffectPlayer;
			metrics.lastPendingNullCount = state.exact.pendingNullCount;
			if (state.exact.pendingPlayer >= 0) metrics.lastPendingDeckUnknown = state.exact.deckUnknown[state.exact.pendingPlayer];
			switch (state.exact.blockReason) {
			case ExactBlockReason::UnknownOpponentList: metrics.unknownOpponentList++; break;
			case ExactBlockReason::InterruptedTransition: metrics.interruptedTransition++; break;
			default: metrics.unsupportedConcreteReference++; break;
			}
			result = unknown();
		} else if (state.exact.pending == ExactPendingType::Draw || state.exact.pending == ExactPendingType::TakePrize) {
			result = chance(state, key);
		} else if (state.selectType == SelectType::YesNo && state.selectContext == SelectContext::CoinHead) {
			result = coinChance(state, key);
		} else {
			result = decision(state, state.selectPlayer == actor, key);
		}
		if (result.certified && !singletonRevealStreaming) {
			auto partialDecision = partialDecisions.find(key);
			if (partialDecision != partialDecisions.end()) {
				partialBytes -= std::min(partialBytes, partialDecision->second.accountedBytes);
				partialDecisions.erase(partialDecision);
			}
			auto partialChance = partialChances.find(key);
			if (partialChance != partialChances.end()) {
				partialBytes -= std::min(partialBytes, partialChance->second.accountedBytes);
				partialChances.erase(partialChance);
			}
			if (shareable) transposition->store(std::move(key), result);
			else {
				size_t bytes = key.size() + sizeof(ExactScore) + 96;
				if (localTransposition.size() < MaxLocalTranspositionEntries
					&& localTranspositionBytes + bytes <= MaxLocalTranspositionBytes) {
					localTranspositionBytes += bytes;
					localTransposition.emplace(std::move(key), result);
				}
			}
		}
		if (result.certified && !fixedMainKey.empty()) {
			fixedFirstTurnMainScores.emplace(fixedMainKey, result);
			if (usingSharedTable) transposition->store(std::move(fixedMainKey), result);
		}
		metrics.partialTableBytes = partialBytes;
		metrics.sessionBytes = transposition->bytes() + localTranspositionBytes + policyBytes + partialBytes
			+ beliefTranspositionBytes + evaluationCache.size() * 128ULL;
		return result;
	}
};
