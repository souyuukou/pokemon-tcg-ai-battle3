#pragma once

#include "ExactSearchHooks.h"
#include "ExactCanonicalState.h"
#include "ExactCpuEvaluator.h"
#include "ExactBigRational.h"
#include "ExactCardPartition.h"

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
	bool structurallyBlocked = false;
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
	unsigned long long dynamicPartitionBuilds = 0;
	unsigned long long dynamicPartitionFallbacks = 0;
	unsigned long long dynamicPartitionCacheHits = 0;
	unsigned long long dynamicPartitionMaxClasses = 0;
	unsigned long long dynamicPartitionMaxVisibleIdentities = 0;
	unsigned long long continuationDraws = 0;
	unsigned long long continuationDrawClasses = 0;
	unsigned long long continuationClassOutcomes = 0;
	unsigned long long continuationConditionalSplits = 0;
	unsigned long long continuationDrawOutcomes = 0;
	unsigned long long continuationAtomsMerged = 0;
	int dynamicPartitionFallbackCardId = 0;
	int dynamicPartitionFallbackEffectType = 0;
	int dynamicPartitionFallbackTargetType = 0;
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

	// A second root worker may traverse the same expensive action from the
	// opposite side. Completed descendants are shared through the immutable TT,
	// so the two cores do not spend the deadline on the same prefix.
	void setReverseActionOrder(bool reverse) { reverseActionOrder = reverse; }
	void setConcreteWorldCaching(bool enabled) { concreteWorldCaching = enabled; }

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
	struct PartitionRevealAllocation {
		std::vector<int> prizeCounts;
		ExactWeight weight;
	};
	struct PartialPartitionRevealEntry {
		std::vector<PartitionRevealAllocation> allocations;
		size_t index = 0;
		ExactFraction completedLower = ExactFraction::integer(0);
		ExactFraction completedUpper = ExactFraction::integer(0);
		ExactWeight totalWeight, processedWeight;
		size_t accountedBytes = 0;
	};
	struct MultiDrawOutcome {
		std::vector<int> atomCounts;
		ExactWeight weight;
		std::string continuationKey;
	};
	struct PartialMultiDrawEntry {
		std::vector<int> bounds;
		std::string continuationSchema;
		std::vector<MultiDrawOutcome> outcomes;
		size_t outcomeIndex = 0;
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
	// nested partitioned reveal and inserts another resumable entry.
	std::map<std::string, PartialPartitionRevealEntry> partialPartitionReveals;
	std::map<std::string, PartialMultiDrawEntry> partialMultiDraws;
	std::unordered_map<std::string, ExactScore, ExactStringHasher> beliefTransposition;
	mutable std::unordered_map<std::string, long long, ExactStringHasher> evaluationCache;
	// Dynamic turn search quotient.  The key retains the complete public
	// state, semantic search options, and every deck count that can affect a later
	// query before the turn leaf, while omitting identities proven irrelevant.
	std::unordered_map<std::string, ExactScore, ExactStringHasher> partitionTurnRevealScores;
	std::unordered_map<std::string, ExactScore, ExactStringHasher> partitionTurnMainScores;
	std::unordered_map<std::string, ExactCardPartition, ExactStringHasher> partitionAnalysisCache;
	std::unordered_map<int, std::vector<long long>> continuationIdentityCache;
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
	bool reverseActionOrder = false;
	bool concreteWorldCaching = false;
	// Full own-deck reveals produce singleton information sets. Turn sessions may
	// opt into concreteWorldCaching for large later-turn DAGs; one-shot calls keep
	// the lower-overhead streaming path.
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

	static bool targetIncludesDeck(const Target& target) {
		for (AreaType area : target.areas) if (area == AreaType::Deck) return true;
		return false;
	}

	static bool exposesArbitraryDeckIdentity(EffectType type) {
		switch (type) {
		case EffectType::Draw:
		case EffectType::DrawTargetCount:
		case EffectType::DrawPrizeCount:
		case EffectType::DrawUntil:
		case EffectType::DrawUntilPsychic:
		case EffectType::DrawMirror:
		case EffectType::LookDeck:
		case EffectType::LookDeckReverse:
		case EffectType::LookDeckBottom:
		case EffectType::DeckToTrash:
		case EffectType::DeckToTrashCoinUntilTail:
		case EffectType::DeckBottomToTrash:
		case EffectType::SwitchDeck:
			return true;
		default:
			return false;
		}
	}

	static bool isDrawEffect(EffectType type) {
		switch (type) {
		case EffectType::Draw: case EffectType::DrawTargetCount:
		case EffectType::DrawPrizeCount: case EffectType::DrawUntil:
		case EffectType::DrawUntilPsychic: case EffectType::DrawMirror: return true;
		default: return false;
		}
	}

	std::vector<long long> continuationIdentityKey(int cardId) {
		auto cached = continuationIdentityCache.find(cardId);
		if (cached != continuationIdentityCache.end()) return cached->second;
		std::vector<long long> key;
		if (evaluator && evaluator->isLoaded()) {
			auto model = evaluator->cardContinuationSignature(cardId);
			key.reserve(model.size() + 96);
			for (std::int16_t value : model) key.push_back(value);
		} else key.push_back(cardId);
		auto found = CardTable.find(cardId);
		if (found == CardTable.end()) { key.push_back(cardId); continuationIdentityCache[cardId] = key; return key; }
		const CardMaster& card = found->second;
		key.push_back((int)card.cardType); key.push_back((int)card.pokemonType);
		key.push_back((int)card.evolutionType); key.push_back((int)card.energyType);
		key.push_back(card.energyCount); key.push_back(card.hp);
		// Pokemon and Energy can carry identity-specific evolution, attack, Ability,
		// or attachment semantics which are not completely represented by the V3
		// leaf accumulator. Keep them singleton unless a future compiler proves a
		// complete continuation signature for those rule objects.
		if (card.cardType == CardType::Pokemon || IsEnergy(card.cardType))
			key.push_back(cardId);
		auto appendText = [&](const std::u8string& text) {
			key.push_back(-1); key.push_back((long long)text.size());
			for (char8_t value : text) key.push_back((unsigned char)value);
		};
		// V3 combo features derive evolutionary relations from these strings.
		if (card.cardType == CardType::Pokemon) {
			appendText(card.name); appendText(card.nameEn);
			appendText(card.evolvesFrom); appendText(card.evolvesFrom2);
		}
		continuationIdentityCache[cardId] = key; return key;
	}

	ExactCardPartition turnDependencyPartition(const State& state,
		const ExactHiddenState* pending = nullptr, bool drawContinuationOnly = false) {
		std::vector<ExactCardAtom> population;
		for (int i = 0; i < state.exact.typeCount[actor]; ++i)
			if (state.exact.cardCount[actor][i] > 0)
				population.push_back({ state.exact.cardId[actor][i], state.exact.cardCount[actor][i] });
		ExactCardPartition partition(population);
		std::set<int> reachable;
		auto addRef = [&](AreaType area, int index) {
			try {
				CardRef ref = state.getCardRef(area, index, actor);
				if (!ref.isNull()) reachable.insert(state.getCard(ref).cardId);
			} catch (...) {}
		};
		// Seed from every owned card that can already act or can become playable
		// later in this turn.  Current legality alone is insufficient: benching a
		// Pokemon or changing a restriction may enable a card after this node.
		for (const SelectOption& option : state.options) {
			switch (option.type) {
			case SelectOptionType::Play: addRef(AreaType::Hand, option.param0); break;
			case SelectOptionType::Attach:
			case SelectOptionType::Evolve: addRef((AreaType)option.param0, option.param1); break;
			case SelectOptionType::Ability: addRef((AreaType)option.param0, option.param1); break;
			case SelectOptionType::Skill: {
				auto skill = SkillTable.find(option.param0);
				if (skill != SkillTable.end()) reachable.insert(skill->second.cardId);
				break;
			}
			default: break;
			}
		}
		for (CardRef ref : state.players[actor].hand) if (!ref.isNull()) {
			int cardId = state.getCard(ref).cardId;
			auto master = CardTable.find(cardId);
			if (master == CardTable.end()) { reachable.insert(cardId); continue; }
			// Once the once-per-turn attachment has been consumed, an Energy
			// subsequently exposed by a search/draw cannot become an operator this
			// turn. Treating Enriching Energy as reachable here forced every deck
			// identity to split solely because its attach effect draws four cards.
			if (state.energyPlayed && IsEnergy(master->second.cardType)) continue;
			if (state.stadiumPlayed && master->second.cardType == CardType::Stadium) continue;
			// A Supporter absent from the legal option list cannot become legal
			// later in the same turn (first-turn prohibition or already used).
			// Evolution cards are likewise unreachable during either player's
			// first turn unless the engine already exposed a legal effect option.
			bool firstTurnEvolution = state.turn <= 2
				&& (master->second.evolutionType == EvolutionType::Stage1
					|| master->second.evolutionType == EvolutionType::Stage2);
			// Other cards may become legal after a bench/target/stadium change.
			if ((!firstTurnEvolution && master->second.cardType != CardType::Supporter)
				|| reachable.contains(cardId))
				reachable.insert(cardId);
		}
		// A draw reveals the concrete card to its owner. Any card which could become
		// a legal operator later in this turn must therefore be an identity-visible
		// continuation class before the draw is aggregated. This closes the strategy
		// fusion hole where two currently-in-deck Items shared model weights but
		// offered different actions after being drawn.
		if (drawContinuationOnly) {
			const PlayerState& player = state.players[actor];
			for (const ExactCardAtom& atom : population) {
				auto found = CardTable.find(atom.cardId);
				if (found == CardTable.end()) { reachable.insert(atom.cardId); continue; }
				const CardMaster& card = found->second;
				bool exhausted = IsEnergy(card.cardType) && state.energyPlayed;
				exhausted = exhausted || (card.cardType == CardType::Supporter
					&& (state.supporterPlayed || player.thisTurn.cannotPlaySupporter
						|| (state.turn <= 1 && !card.canPlayFirstTurn)));
				exhausted = exhausted || (card.cardType == CardType::Stadium
					&& (state.stadiumPlayed || player.cannotPlayStadium
						|| player.thisTurn.cannotPlayStadium));
				if (!exhausted) reachable.insert(atom.cardId);
			}
		}
		std::string dependencyKey = "TURN-DEPENDENCY-V1|";
		appendSemantic(dependencyKey, drawContinuationOnly ? 1 : 0);
		appendSemantic(dependencyKey, state.turn <= 2 ? 1 : 0);
		for (const ExactCardAtom& atom : population) {
			appendSemantic(dependencyKey, atom.cardId); appendSemantic(dependencyKey, atom.count);
		}
		for (int id : reachable) appendSemantic(dependencyKey, id);
		if (pending != nullptr) {
			appendSemantic(dependencyKey, pending->pendingPlayer);
			appendSemantic(dependencyKey, pending->pendingSkillId);
			appendSemantic(dependencyKey, pending->pendingEffectIndex);
			appendSemantic(dependencyKey, pending->pendingDetail);
		}
		auto cachedPartition = partitionAnalysisCache.find(dependencyKey);
		if (cachedPartition != partitionAnalysisCache.end()) {
			metrics.dynamicPartitionCacheHits++;
			return cachedPartition->second;
		}

		bool exposeAll = false;
		int dependencyCardId = 0;
		int dependencyEffectType = 0;
		auto applyTarget = [&](const Target& target) {
			if (!targetIncludesDeck(target)) return;
			std::set<int> matching;
			for (const ExactCardAtom& atom : population) {
				auto master = CardTable.find(atom.cardId);
				if (master == CardTable.end()) { exposeAll = true; return; }
				ExactStaticTargetResult result = ExactStaticTargetMatches(master->second, target);
				if (!result.supported) {
					exposeAll = true;
					metrics.dynamicPartitionFallbackCardId = dependencyCardId;
					metrics.dynamicPartitionFallbackEffectType = dependencyEffectType;
					metrics.dynamicPartitionFallbackTargetType = (int)target.conditions.front().targetType;
					return;
				}
				if (result.matches) matching.insert(atom.cardId);
			}
			partition.refineVisible([&](int cardId) { return matching.contains(cardId); });
			for (int cardId : matching) {
				auto master = CardTable.find(cardId);
				if (master == CardTable.end()) { exposeAll = true; return; }
				// Neither player can evolve during their first turn.  A searched
				// evolution card is observable (and remains a singleton class), but
				// its evolve-time Skill is not a reachable operator this turn.
				if (state.turn <= 2 && (master->second.evolutionType == EvolutionType::Stage1
					|| master->second.evolutionType == EvolutionType::Stage2)) continue;
				reachable.insert(cardId);
			}
		};

		if (pending != nullptr && pending->pendingSkillId > 0 && pending->pendingEffectIndex >= 0) {
			auto skill = SkillTable.find(pending->pendingSkillId);
			if (skill == SkillTable.end() || pending->pendingEffectIndex >= (int)skill->second.effects.size()) exposeAll = true;
			else {
				dependencyCardId = skill->second.cardId;
				dependencyEffectType = (int)skill->second.effects[pending->pendingEffectIndex].effectType;
				applyTarget(skill->second.effects[pending->pendingEffectIndex].target);
			}
		}

		std::set<int> scanned;
		while (!exposeAll) {
			auto next = std::find_if(reachable.begin(), reachable.end(), [&](int id) { return !scanned.contains(id); });
			if (next == reachable.end()) break;
			int id = *next; scanned.insert(id);
			dependencyCardId = id;
			auto master = CardTable.find(id);
			if (master == CardTable.end()) { exposeAll = true; break; }
			for (const Skill* skill : master->second.getSkills()) if (skill != nullptr) {
				for (const Effect& effect : skill->effects) {
					dependencyEffectType = (int)effect.effectType;
					bool arbitraryIdentity = exposesArbitraryDeckIdentity(effect.effectType);
					if (arbitraryIdentity && isDrawEffect(effect.effectType) && drawContinuationOnly
						&& evaluator && evaluator->isLoaded()) arbitraryIdentity = false;
					if (arbitraryIdentity) {
						exposeAll = true;
						metrics.dynamicPartitionFallbackCardId = id;
						metrics.dynamicPartitionFallbackEffectType = (int)effect.effectType;
						break;
					}
					// An unfiltered deck-size condition is identity-free.  A filtered
					// condition observes its predicate just as a selection does, even
					// when no card is moved.
					if (targetIncludesDeck(effect.target)
						&& (!effect.isCondition || !effect.target.conditions.empty()))
						applyTarget(effect.target);
					if (exposeAll) break;
				}
				if (exposeAll) break;
			}
		}
		if (exposeAll) partition.exposeAllIdentities();
		else if (drawContinuationOnly) {
			partition.refineVisible([&](int cardId) { return reachable.contains(cardId); });
			partition.refineEquivalent([&](int cardId) {
				// With no model loaded, retain card identity. Structural zero-evaluator
				// tests are not allowed to weaken the certified model equivalence rule.
				return continuationIdentityKey(cardId);
			});
		}
		metrics.dynamicPartitionBuilds++;
		metrics.dynamicPartitionMaxClasses = std::max<unsigned long long>(
			metrics.dynamicPartitionMaxClasses, partition.classes().size());
		metrics.dynamicPartitionMaxVisibleIdentities = std::max<unsigned long long>(
			metrics.dynamicPartitionMaxVisibleIdentities, partition.visibleCardIds().size());
		if (!partition.hasCompressedClass()) metrics.dynamicPartitionFallbacks++;
		if (partitionAnalysisCache.size() < 16'384)
			partitionAnalysisCache.emplace(std::move(dependencyKey), partition);
		return partition;
	}

	std::string partitionTurnMainKey(const State& state) {
		if (!singletonRevealStreaming || state.selectPlayer != actor
			|| state.selectType != SelectType::Main || state.exact.deckUnknown[actor]) return {};
		ExactCardPartition partition = turnDependencyPartition(state);
		if (!partition.hasCompressedClass()) return {};
		std::string key = "DYNAMIC-TURN-MAIN\x1f" + partition.schemaKey();
		key += observationKeyFor(state, actor, nullptr, false);
		for (int id : partition.visibleCardIds()) {
			int count = 0;
			for (CardRef ref : state.players[actor].deck)
				if (!ref.isNull() && state.getCard(ref).cardId == id) count++;
			appendSemantic(key, id); appendSemantic(key, count);
		}
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
		// lower bound. An assisting reverse worker deliberately reaches it last.
		int endIndex = -1;
		if (state.selectType == SelectType::Main && state.selectMin <= 1 && state.selectMax >= 1) {
			for (int i = 0; i < (int)state.options.size(); ++i) {
				if (state.options[i].type == SelectOptionType::End) { endIndex = i; break; }
			}
			if (!reverseActionOrder && endIndex >= 0
				&& !callback(std::vector<int>{ endIndex })) return false;
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
		if (reverseActionOrder) std::reverse(groups.begin(), groups.end());
		std::vector<int> current;
		std::function<bool(int, int)> choose = [&](int group, int left) {
			if (expired()) return false;
			if (left == 0) {
				std::vector<int> action = current; std::sort(action.begin(), action.end());
				if (!reverseActionOrder && action.size() == 1 && action[0] == endIndex) return true;
				return callback(action);
			}
			if (group >= (int)groups.size()) return true;
			int remainingCapacity = 0;
			for (int i = group; i < (int)groups.size(); ++i) remainingCapacity += (int)groups[i].index.size();
			if (remainingCapacity < left) return true;
			int maximum = std::min(left, (int)groups[group].index.size());
			int take = reverseActionOrder ? maximum : 0;
			for (; reverseActionOrder ? take >= 0 : take <= maximum;
				take += reverseActionOrder ? -1 : 1) {
				for (int i = 0; i < take; ++i) current.push_back(groups[group].index[i]);
				if (!choose(group + 1, left - take)) return false;
				for (int i = 0; i < take; ++i) current.pop_back();
			}
			return true;
		};
		int count = reverseActionOrder ? state.selectMax : state.selectMin;
		for (; reverseActionOrder ? count >= state.selectMin : count <= state.selectMax;
			count += reverseActionOrder ? -1 : 1) {
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
			// Copies of the same card in hand are exchangeable for play, attach,
			// and evolve. Physical hand indices caused factorial duplicate action
			// prefixes before canonical successors had a chance to merge them.
			if (option.type == SelectOptionType::Play) {
				CardRef ref = state.getCardRef(AreaType::Hand, option.param0, state.selectPlayer);
				if (!ref.isNull()) token = "PLAY:" + std::to_string(state.getCard(ref).cardId);
			} else if (option.type == SelectOptionType::Attach || option.type == SelectOptionType::Evolve) {
				CardRef ref = state.getCardRef((AreaType)option.param0, option.param1, state.selectPlayer);
				if (!ref.isNull()) token = (option.type == SelectOptionType::Attach ? "ATTACH:" : "EVOLVE:")
					+ std::to_string(state.getCard(ref).cardId) + ":" + std::to_string(option.param2)
					+ ":" + std::to_string(option.param3);
			}
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

	void materializeUnknownHandFromPool(State& state, int player,
		const std::vector<int>& handCounts) {
		materializeUnknownHand(state, player, handCounts);
		for (int i = 0; i < (int)handCounts.size(); ++i)
			for (int n = 0; n < handCounts[i]; ++n)
				decrementPool(state, player, state.exact.cardId[player][i]);
	}

	bool expandHiddenHandBelief(const State& parent, const ExactHiddenState& request,
		const std::vector<int>& action, const ExactWeight& baseWeight,
		const std::array<ExactKnowledgeState, 2>& baseKnowledge,
		std::vector<BeliefWorld>& output) {
		const int player = request.pendingPlayer;
		if (player < 0 || player >= 2 || request.pending != ExactPendingType::RevealDeck
			|| pendingArea(request) != AreaType::Hand || !parent.exact.profileKnown[player]) return false;
		int handSize = 0;
		for (CardRef ref : parent.players[player].hand) if (ref.isNull()) handSize++;
		std::vector<int> bounds(parent.exact.typeCount[player]);
		int totalHidden = 0;
		for (int i = 0; i < (int)bounds.size(); ++i) {
			bounds[i] = parent.exact.cardCount[player][i]; totalHidden += bounds[i];
		}
		if (handSize < 0 || handSize > totalHidden) return false;
		ExactWeight expected = ExactWeight::multiply(baseWeight, chooseCount(totalHidden, handSize));
		ExactWeight generated;
		BoundedCompositionCursor cursor; cursor.reset(bounds, handSize);
		std::vector<int> handCounts;
		unsigned long long raw = 0;
		while (cursor.next(handCounts)) {
			if (expired()) return false;
			ExactWeight allocation(1);
			for (int i = 0; i < (int)bounds.size(); ++i)
				allocation = ExactWeight::multiply(allocation, chooseCount(bounds[i], handCounts[i]));
			if (allocation.zero()) continue;
			ExactWeight weight = ExactWeight::multiply(baseWeight, allocation);
			auto child = std::make_unique<State>(parent);
			auto knowledge = baseKnowledge;
			try {
				materializeUnknownHandFromPool(*child, player, handCounts);
				for (int i = 0; i < (int)handCounts.size(); ++i)
					for (int n = 0; n < handCounts[i]; ++n)
						appendKnowledgeFact(knowledge[player], 'H', parent.exact.cardId[player][i]);
				if (!advance(*child, action)) return false;
			} catch (...) { return false; }
			output.push_back({ std::move(child), weight, std::move(knowledge) });
			generated += weight; raw++; metrics.enumeratedHiddenWorlds++;
		}
		if (generated != expected) {
			metrics.chanceMassMismatches++; metrics.probabilityExact = false; return false;
		}
		metrics.rawOutcomes += raw; metrics.groupedOutcomes += output.size();
		return !output.empty();
	}

	bool isForcedHiddenHandDiscard(const ExactHiddenState& request, int& keepCount) const {
		if (pendingArea(request) != AreaType::Hand || request.pendingSkillId <= 0
			|| request.pendingEffectIndex < 0) return false;
		auto skill = SkillTable.find(request.pendingSkillId);
		if (skill == SkillTable.end() || request.pendingEffectIndex >= (int)skill->second.effects.size()) return false;
		const Effect& effect = skill->second.effects[request.pendingEffectIndex];
		if (effect.effectType != EffectType::ToTrash || effect.effectSelectType != EffectSelectType::CardUntil
			|| !effect.enemySelect) return false;
		keepCount = effect.selectCount;
		return keepCount >= 0 && canForgetOpponentHandAfterForcedDiscard(request);
	}

	static bool targetReadsEnemyHand(const Target& target) {
		bool hand = false;
		for (AreaType area : target.areas) if (area == AreaType::Hand) { hand = true; break; }
		if (!hand) return false;
		return target.targetPlayer == TargetPlayer::Enemy || target.targetPlayer == TargetPlayer::Both;
	}

	bool canForgetOpponentHandAfterForcedDiscard(const ExactHiddenState& request) const {
		auto effectCard = CardTable.find(request.pendingEffectCardId);
		// The proof below relies on the once-per-turn Supporter rule: after this
		// effect resolves, every other Supporter is unreachable until the leaf.
		if (effectCard == CardTable.end() || effectCard->second.cardType != CardType::Supporter)
			return false;
		for (const auto& profile : actorProfileCount) {
			auto card = CardTable.find(profile.first);
			if (card == CardTable.end()) return false;
			if (card->second.cardType == CardType::Supporter) continue;
			auto reads = [](const Effect& effect) { return targetReadsEnemyHand(effect.target); };
			for (const Skill* skill : card->second.getSkills()) {
				if (skill == nullptr) continue;
				for (const Effect& effect : skill->effects) if (reads(effect)) return false;
			}
			for (const Attack* attack : card->second.attacks) {
				if (attack == nullptr) continue;
				for (const Effect& effect : attack->preEffects) if (reads(effect)) return false;
				for (const Effect& effect : attack->postEffects) if (reads(effect)) return false;
			}
		}
		return true;
	}

	bool selectHiddenHandDiscard(State& state, int player, const std::vector<int>& discardCounts) {
		std::unordered_map<int, int> remaining;
		for (int i = 0; i < (int)discardCounts.size(); ++i)
			if (discardCounts[i] > 0) remaining[state.exact.cardId[player][i]] = discardCounts[i];
		std::vector<int> selected;
		for (int optionIndex = 0; optionIndex < (int)state.options.size(); ++optionIndex) {
			const SelectOption& option = state.options[optionIndex];
			if (option.type != SelectOptionType::Card) continue;
			CardPosition position = option.getCardPosition();
			if (position.playerIndex != player || position.area != AreaType::Hand) continue;
			CardRef ref = state.getCardRef(position);
			if (ref.isNull()) return false;
			int id = state.getCard(ref).cardId;
			auto found = remaining.find(id);
			if (found != remaining.end() && found->second > 0) {
				selected.push_back(optionIndex); found->second--;
			}
		}
		for (const auto& item : remaining) if (item.second != 0) return false;
		if ((int)selected.size() != state.selectMin || state.selectMin != state.selectMax) return false;
		std::sort(selected.begin(), selected.end());
		return advance(state, selected);
	}

	void anonymizeHiddenHandAfterDiscard(State& state, int player,
		const std::vector<int>& originalBounds, const std::vector<int>& discardCounts) {
		PlayerState& ps = state.players[player];
		for (int i = 0; i < ps.hand.size(); ++i) {
			CardRef ref = ps.hand[i];
			if (ref.isNull()) continue;
			state.allCard[ref.cardIndex] = {}; ps.hand[i] = CardRef(0);
		}
		for (int i = 0; i < state.exact.typeCount[player]; ++i) {
			int count = originalBounds[i] - discardCounts[i];
			if (count < 0 || count > 255) throw std::runtime_error("hidden hand residual mismatch");
			state.exact.cardCount[player][i] = (unsigned char)count;
		}
		state.exact.deckUnknown[player] = true;
		state.exact.prizeExchangeable[player] = true;
		state.exact.provisionalOpponentPolicy = false;
		state.exact.clearPending();
	}

	ExactScore solveForcedHiddenHandDiscard(const State& parent, const ExactHiddenState& request,
		const std::vector<int>& action, int keepCount) {
		const int player = request.pendingPlayer;
		if (player < 0 || player >= 2 || player == actor || !parent.exact.profileKnown[player]) return unknown();
		int handSize = 0;
		for (CardRef ref : parent.players[player].hand) {
			if (!ref.isNull()) return unknown();
			handSize++;
		}
		const int discardSize = handSize - keepCount;
		if (discardSize <= 0) return unknown();
		std::vector<int> bounds(parent.exact.typeCount[player]);
		int totalHidden = 0;
		for (int i = 0; i < (int)bounds.size(); ++i) {
			bounds[i] = parent.exact.cardCount[player][i]; totalHidden += bounds[i];
		}
		if (handSize > totalHidden) return unknown();
		struct CountKeyHash { size_t operator()(const std::array<unsigned char, DECK_SIZE>& key) const noexcept {
			size_t value = 1469598103934665603ULL;
			for (unsigned char count : key) { value ^= count; value *= 1099511628211ULL; }
			return value;
		} };
		std::unordered_map<std::array<unsigned char, DECK_SIZE>, ExactScore, CountKeyHash> discardScores;
		ExactWeight totalWeight = chooseCount(totalHidden, handSize), processedWeight;
		ExactFraction lower = ExactFraction::integer(0), upper = ExactFraction::integer(0);
		unsigned long long handWorlds = 0;
		// A root worker normally yields after 20k nodes.  Yielding before one
		// information set has considered every legal discard would restart that
		// minimisation and can never make progress.  Complete a bounded chunk while
		// retaining the wall-clock and RSS checks performed by expired().
		struct QuantumGuard { unsigned long long& limit; unsigned long long previous;
			QuantumGuard(unsigned long long& value, unsigned long long expanded)
				: limit(value), previous(value) {
				if (value != std::numeric_limits<unsigned long long>::max())
					value = std::max(value, expanded + 2'000'000ULL);
			}
			~QuantumGuard() { limit = previous; }
		} quantum(nodeQuantumDeadline, metrics.expanded);
		BoundedCompositionCursor handCursor; handCursor.reset(bounds, handSize);
		std::vector<int> handCounts;
		while (handCursor.next(handCounts)) {
			if (expired()) break;
			ExactWeight handWeight(1);
			for (int i = 0; i < (int)bounds.size(); ++i)
				handWeight = ExactWeight::multiply(handWeight, chooseCount(bounds[i], handCounts[i]));
			BoundedCompositionCursor discardCursor; discardCursor.reset(handCounts, discardSize);
			std::vector<int> discardCounts;
			ExactFraction handLower = ExactFraction::integer(100'000'000);
			ExactFraction handUpper = ExactFraction::integer(100'000'000);
			bool foundAction = false;
			while (discardCursor.next(discardCounts)) {
				if (expired()) break;
				std::array<unsigned char, DECK_SIZE> key{};
				for (int i = 0; i < (int)discardCounts.size(); ++i) key[i] = (unsigned char)discardCounts[i];
				auto cached = discardScores.find(key);
				ExactScore score;
				if (cached != discardScores.end()) { score = cached->second; metrics.successorMerges++; }
				else {
					std::vector<int> representative = discardCounts;
					int left = keepCount;
					for (int i = 0; i < (int)bounds.size() && left > 0; ++i) {
						int take = std::min(left, bounds[i] - discardCounts[i]);
						representative[i] += take; left -= take;
					}
					if (left != 0) return unknown();
					auto child = std::make_unique<State>(parent);
					try {
						materializeUnknownHand(*child, player, representative);
						if (!advance(*child, action)) return unknown();
						if (!selectHiddenHandDiscard(*child, player, discardCounts)) return unknown();
						anonymizeHiddenHandAfterDiscard(*child, player, bounds, discardCounts);
					} catch (...) { return unknown(); }
					score = solveOwned(std::move(child));
					discardScores.emplace(key, score);
					metrics.enumeratedHiddenWorlds++;
				}
				if (!foundAction || ExactCompare(score.lower, handLower) < 0) handLower = score.lower;
				if (!foundAction || ExactCompare(score.upper, handUpper) < 0) handUpper = score.upper;
				foundAction = true;
			}
			if (!foundAction || expired()) break;
			lower = ExactFraction::add(lower, handLower.scaled(handWeight, totalWeight));
			upper = ExactFraction::add(upper, handUpper.scaled(handWeight, totalWeight));
			processedWeight += handWeight; metrics.informationSets++; handWorlds++;
		}
		ExactWeight remaining = processedWeight >= totalWeight ? ExactWeight()
			: ExactWeight::subtract(totalWeight, processedWeight);
		if (!remaining.zero()) {
			lower = ExactFraction::add(lower, ExactFraction::integer(-100'000'000).scaled(remaining, totalWeight));
			upper = ExactFraction::add(upper, ExactFraction::integer(100'000'000).scaled(remaining, totalWeight));
			metrics.partialChanceNodes++;
		}
		metrics.rawOutcomes += handWorlds;
		metrics.groupedOutcomes += discardScores.size();
		bool certified = remaining.zero() && ExactCompare(lower, upper) == 0;
		return { lower, upper, {}, certified };
	}

	bool expandRevealBelief(const State& parent, const ExactHiddenState& request,
		const std::vector<int>& action,
		const ExactWeight& baseWeight, const std::array<ExactKnowledgeState, 2>& baseKnowledge,
		std::vector<BeliefWorld>& output) {
		int player = request.pendingPlayer;
		if (player < 0 || player >= 2 || request.pending != ExactPendingType::RevealDeck) return false;
		if (!parent.exact.profileKnown[player]) return false;
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
		if (pendingArea(request) == AreaType::Hand) {
			int keepCount = 0;
			if (isForcedHiddenHandDiscard(request, keepCount))
				return solveForcedHiddenHandDiscard(parent, request, action, keepCount);
			std::vector<BeliefWorld> worlds;
			auto knowledge = initialKnowledge();
			if (!expandHiddenHandBelief(parent, request, action, ExactWeight(1), knowledge, worlds)
				|| worlds.empty()) return unknown();
			metrics.beliefWorldsBefore += worlds.size();
			return solveBelief(std::move(worlds));
		}
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

	bool dynamicTurnSearchPartition(const State& parent, const ExactHiddenState& request,
		ExactCardPartition& partition) {
		if (request.pending != ExactPendingType::RevealDeck
			|| request.pendingPlayer != actor || pendingArea(request) != AreaType::Deck) return false;
		partition = turnDependencyPartition(parent, &request);
		return partition.hasCompressedClass();
	}

	ExactScore revealAndReplayPartitionedTurnSearch(const State& parent, const ExactHiddenState& request,
		const std::vector<int>& action, int prizeSize, int totalHidden, const ExactWeight& totalWeight,
		const ExactCardPartition& partition) {
		const int player = request.pendingPlayer;
		std::string revealKey = keyFor(parent) + "\x1fR7-DYNAMIC-TURN\x1f"
			+ partition.schemaKey() + actionEquivalenceKey(parent, action);
		appendSemantic(revealKey, request.pendingDetail); appendSemantic(revealKey, request.pendingEffectCardId);
		appendSemantic(revealKey, request.pendingEffectPlayer);
		auto [found, inserted] = partialPartitionReveals.try_emplace(revealKey);
		PartialPartitionRevealEntry& partial = found->second;
		if (inserted) {
			std::vector<int> relevantIds = partition.visibleCardIds();
			std::vector<int> typeIndex(relevantIds.size(), -1);
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
					PartitionRevealAllocation allocation;
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
				partialPartitionReveals.erase(found); return unknown();
			}
			if (reverseActionOrder)
				std::reverse(partial.allocations.begin(), partial.allocations.end());
			partial.totalWeight = totalWeight;
			partial.accountedBytes = revealKey.size() + sizeof(PartialPartitionRevealEntry);
			for (const auto& allocation : partial.allocations)
				partial.accountedBytes += sizeof(PartitionRevealAllocation) + allocation.prizeCounts.size() * sizeof(int);
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
			const PartitionRevealAllocation& allocation = partial.allocations[partial.index];
			auto world = std::make_unique<State>(parent);
			try {
				materializeUnknownZones(*world, player, allocation.prizeCounts, handCounts);
				if (!advance(*world, action)) return unknown();
			} catch (...) { return unknown(); }
			std::string quotientKey = observationKeyFor(*world, actor, nullptr, false);
			std::vector<int> relevantIds = partition.visibleCardIds();
			quotientKey += "\x1f" "DYNAMIC-TURN-SEARCH" + partition.schemaKey();
			for (int id : relevantIds) {
				int count = 0;
				for (CardRef ref : world->players[actor].deck) if (!ref.isNull() && world->getCard(ref).cardId == id) count++;
				appendSemantic(quotientKey, count);
			}
			std::string sharedKey = "DYNAMIC-TURN-SEARCH\x1f" + quotientKey;
			ExactScore score;
			bool sharedHit = false;
			auto local = partitionTurnRevealScores.find(quotientKey);
			bool cacheHit = local != partitionTurnRevealScores.end();
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
				// Root workers normally yield every 20k nodes. Give one concrete world
				// a bounded larger quantum to amortize reconstruction; canonical TT and
				// partial cursors still make the boundary exactly resumable.
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
					partitionTurnRevealScores.emplace(std::move(quotientKey), score);
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
		partialPartitionReveals.erase(revealKey);
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
		ExactCardPartition turnPartition;
		if (handSize == 0 && dynamicTurnSearchPartition(parent, request, turnPartition))
			return revealAndReplayPartitionedTurnSearch(parent, request, action, prizeSize, totalHidden, totalWeight,
				turnPartition);
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
			ExactCardPartition dynamicPartition;
			const bool partitionedTurnSearch = handSize == 0
				&& dynamicTurnSearchPartition(parent, request, dynamicPartition);
			std::string quotientKey;
			std::string sharedQuotientKey;
			if (partitionedTurnSearch) {
				quotientKey = observationKeyFor(*world, actor, nullptr, false);
				std::vector<int> relevantIds = dynamicPartition.visibleCardIds();
				std::vector<int> deckCounts(relevantIds.size(), 0);
				for (CardRef ref : world->players[actor].deck) if (!ref.isNull()) {
					int id = world->getCard(ref).cardId;
					for (int i = 0; i < (int)relevantIds.size(); ++i)
						if (id == relevantIds[i]) { deckCounts[i]++; break; }
				}
				quotientKey += "\x1f" "DYNAMIC-TURN-SEARCH" + dynamicPartition.schemaKey();
				for (int count : deckCounts) appendSemantic(quotientKey, count);
				sharedQuotientKey = "DYNAMIC-TURN-SEARCH\x1f" + quotientKey;
				bool sharedHit = false;
				auto cached = partitionTurnRevealScores.find(quotientKey);
				bool cacheHit = cached != partitionTurnRevealScores.end();
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
				if (partitionedTurnSearch && score.certified) {
					partitionTurnRevealScores.emplace(std::move(quotientKey), score);
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
			for (const BeliefWorld& world : worlds) {
				std::vector<int> mapped;
				if (!remapAction(*world.state, common.semantic, mapped)) {
					metrics.illegalInformationSetSplits++; metrics.informationSetSafe = false;
					metrics.hiddenInformationLeakDetected = true; return unknown();
				}
				auto child = std::make_unique<State>(*world.state);
				if (!advance(*child, mapped)) return unknown();
				if (child->exact.pending == ExactPendingType::RevealDeck) {
					bool expanded = pendingArea(child->exact) == AreaType::Hand
						? expandHiddenHandBelief(*world.state, child->exact, mapped, world.weight, world.knowledge, children)
						: expandRevealBelief(*world.state, child->exact, mapped, world.weight, world.knowledge, children);
					if (!expanded) return unknown();
				} else children.push_back({ std::move(child), world.weight, world.knowledge });
			}
			ExactScore score = solveBelief(std::move(children));
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

	struct DrawContinuationClass {
		std::vector<std::pair<int, int>> atoms;
		int count = 0;
	};

	std::vector<DrawContinuationClass> drawContinuationClasses(const State& state,
		const std::vector<std::pair<int, ExactWeight>>& types, std::string& schema) {
		std::map<int, int> available;
		for (const auto& item : types) {
			if (!item.second.fitsUnsignedLongLong()
				|| item.second.unsignedLongLong() > (unsigned long long)DECK_SIZE) return {};
			available[item.first] = (int)item.second.unsignedLongLong();
		}
		ExactCardPartition partition = turnDependencyPartition(state, nullptr, true);
		std::set<int> assigned;
		std::vector<DrawContinuationClass> result;
		for (const ExactCardClass& source : partition.classes()) {
			DrawContinuationClass target;
			for (const ExactCardAtom& atom : source.atoms) {
				auto found = available.find(atom.cardId);
				if (found == available.end() || found->second <= 0) continue;
				target.atoms.push_back({ atom.cardId, found->second });
				target.count += found->second; assigned.insert(atom.cardId);
			}
			if (target.count > 0) result.push_back(std::move(target));
		}
		// A stale or partially materialized profile must never silently drop an
		// identity. Conservatively retain any unmatched card as a singleton class.
		for (const auto& item : available) if (!assigned.contains(item.first))
			result.push_back({ { { item.first, item.second } }, item.second });
		std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
			return left.atoms.front().first < right.atoms.front().first;
		});
		schema = "CONTINUATION-DRAW-V1|";
		for (const DrawContinuationClass& group : result) {
			appendSemantic(schema, group.count); appendSemantic(schema, (long long)group.atoms.size());
			for (const auto& atom : group.atoms) {
				appendSemantic(schema, atom.first); appendSemantic(schema, atom.second);
			}
		}
		return result;
	}

	ExactScore multiDrawChance(const State& state, const std::string& nodeKey) {
		auto types = chanceCardTypes(state);
		const int drawCount = state.exact.pendingCount;
		if (types.empty() || drawCount <= 1) return unknown();
		std::string continuationSchema;
		std::vector<DrawContinuationClass> classes;
		if (state.exact.pendingPlayer == actor)
			classes = drawContinuationClasses(state, types, continuationSchema);
		else {
			continuationSchema = "CONTINUATION-DRAW-SINGLETON|";
			for (const auto& item : types) {
				if (!item.second.fitsUnsignedLongLong()) return unknown();
				int count = (int)item.second.unsignedLongLong();
				classes.push_back({ { { item.first, count } }, count });
				appendSemantic(continuationSchema, item.first); appendSemantic(continuationSchema, count);
			}
		}
		if (classes.empty()) return unknown();
		std::vector<int> bounds; bounds.reserve(types.size());
		int available = 0;
		for (const auto& type : types) {
			if (!type.second.fitsUnsignedLongLong()
				|| type.second.unsignedLongLong() > (unsigned long long)DECK_SIZE) return unknown();
			bounds.push_back((int)type.second.unsignedLongLong()); available += bounds.back();
		}
		if (drawCount > available) return unknown();
		std::string resumeKey = nodeKey + "\x1fMULTI-DRAW";
		auto [found, inserted] = partialMultiDraws.try_emplace(resumeKey);
		PartialMultiDrawEntry& partial = found->second;
		if (inserted || !partial.initialized) {
			partial.bounds = bounds;
			partial.continuationSchema = continuationSchema;
			partial.totalWeight = chooseCount(available, drawCount);
			std::map<int, int> typeIndex;
			for (int i = 0; i < (int)types.size(); ++i) typeIndex[types[i].first] = i;
			std::vector<int> classBounds; classBounds.reserve(classes.size());
			for (const DrawContinuationClass& group : classes) classBounds.push_back(group.count);
			BoundedCompositionCursor classCursor; classCursor.reset(classBounds, drawCount);
			std::vector<int> classCounts;
			std::unordered_map<std::string, size_t, ExactStringHasher> byContinuation;
			ExactWeight generated;
			unsigned long long rawDrawOutcomes = 0;
			// The outer enumeration axis is the continuation class, not card ID.
			// Only after observing a class-count vector do we conditionally split a
			// class into atoms. This preserves exact hypergeometric mass while
			// avoiding identity enumeration for classes which need no refinement.
			struct ConditionalAllocation {
				std::vector<std::pair<int, int>> counts;
				ExactWeight weight;
				std::string symmetricKey;
			};
			while (classCursor.next(classCounts)) {
				metrics.continuationClassOutcomes++;
				std::vector<std::vector<ConditionalAllocation>> allocations(classes.size());
				ExactWeight outerWeight(1);
				bool valid = true;
				for (int groupIndex = 0; groupIndex < (int)classes.size(); ++groupIndex) {
					const DrawContinuationClass& group = classes[groupIndex];
					const int take = classCounts[groupIndex];
					outerWeight = ExactWeight::multiply(outerWeight, chooseCount(group.count, take));
					std::vector<int> atomBounds; atomBounds.reserve(group.atoms.size());
					for (const auto& atom : group.atoms) atomBounds.push_back(atom.second);
					BoundedCompositionCursor atomCursor; atomCursor.reset(atomBounds, take);
					std::vector<int> localCounts;
					ExactWeight conditionalMass;
					while (atomCursor.next(localCounts)) {
						ConditionalAllocation allocation;
						allocation.weight = ExactWeight(1);
						std::vector<std::pair<int, int>> symmetric;
						for (int atomIndex = 0; atomIndex < (int)group.atoms.size(); ++atomIndex) {
							const auto& atom = group.atoms[atomIndex];
							allocation.weight = ExactWeight::multiply(allocation.weight,
								chooseCount(atom.second, localCounts[atomIndex]));
							if (localCounts[atomIndex] > 0)
								allocation.counts.push_back({ typeIndex.at(atom.first), localCounts[atomIndex] });
							symmetric.push_back({ atom.second, localCounts[atomIndex] });
						}
						std::sort(symmetric.begin(), symmetric.end());
						appendSemantic(allocation.symmetricKey, (long long)symmetric.size());
						for (const auto& pair : symmetric) {
							appendSemantic(allocation.symmetricKey, pair.first);
							appendSemantic(allocation.symmetricKey, pair.second);
						}
						conditionalMass += allocation.weight;
						allocations[groupIndex].push_back(std::move(allocation));
					}
					if (conditionalMass != chooseCount(group.count, take)) { valid = false; break; }
					if (allocations[groupIndex].size() > 1)
						metrics.continuationConditionalSplits += allocations[groupIndex].size() - 1;
				}
				if (!valid) {
					metrics.chanceMassMismatches++; metrics.probabilityExact = false;
					partialMultiDraws.erase(found); return unknown();
				}
				ExactWeight generatedForClassVector;
				std::vector<int> atomCounts(bounds.size(), 0);
				std::function<void(int, const ExactWeight&, std::string)> combine;
				combine = [&](int groupIndex, const ExactWeight& weight, std::string key) {
					if (groupIndex == (int)allocations.size()) {
						auto [position, created] = byContinuation.emplace(key, partial.outcomes.size());
						if (created) partial.outcomes.push_back({ atomCounts, weight, std::move(key) });
						else partial.outcomes[position->second].weight += weight;
						generated += weight; generatedForClassVector += weight;
						metrics.rawOutcomes++; rawDrawOutcomes++;
						return;
					}
					for (const ConditionalAllocation& allocation : allocations[groupIndex]) {
						for (const auto& item : allocation.counts) atomCounts[item.first] = item.second;
						combine(groupIndex + 1, ExactWeight::multiply(weight, allocation.weight),
							key + allocation.symmetricKey);
						for (const auto& item : allocation.counts) atomCounts[item.first] = 0;
					}
				};
				combine(0, ExactWeight(1), {});
				if (generatedForClassVector != outerWeight) {
					metrics.chanceMassMismatches++; metrics.probabilityExact = false;
					partialMultiDraws.erase(found); return unknown();
				}
			}
			if (generated != partial.totalWeight) {
				metrics.chanceMassMismatches++; metrics.probabilityExact = false;
				partialMultiDraws.erase(found); return unknown();
			}
			std::sort(partial.outcomes.begin(), partial.outcomes.end(), [](const auto& left, const auto& right) {
				return left.continuationKey < right.continuationKey;
			});
			metrics.groupedOutcomes += rawDrawOutcomes >= partial.outcomes.size()
				? rawDrawOutcomes - partial.outcomes.size() : 0;
			partial.initialized = true;
			partial.accountedBytes = resumeKey.size() + sizeof(PartialMultiDrawEntry)
				+ bounds.size() * sizeof(int) * 8;
			for (const auto& outcome : partial.outcomes)
				partial.accountedBytes += sizeof(MultiDrawOutcome)
					+ outcome.atomCounts.size() * sizeof(int) + outcome.continuationKey.size();
			partialBytes += partial.accountedBytes;
			metrics.continuationDraws++;
			metrics.continuationDrawClasses += classes.size();
			for (const auto& group : classes) if (group.atoms.size() > 1)
				metrics.continuationAtomsMerged += group.atoms.size() - 1;
		} else if (partial.bounds != bounds || partial.continuationSchema != continuationSchema
			|| partial.totalWeight != chooseCount(available, drawCount)) {
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
		while (partial.outcomeIndex < partial.outcomes.size()) {
			const MultiDrawOutcome& outcome = partial.outcomes[partial.outcomeIndex];
			partial.pendingWeight = outcome.weight; partial.pending = true;
			if (expired()) {
				metrics.partialChanceNodes++;
				return incomplete();
			}
			auto child = std::make_unique<State>(state);
			try {
				for (int i = 0; i < (int)outcome.atomCounts.size(); ++i)
					for (int n = 0; n < outcome.atomCounts[i]; ++n) resolveDraw(*child, types[i].first);
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
			metrics.enumeratedHiddenWorlds++; metrics.continuationDrawOutcomes++;
			partial.outcomeIndex++;
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
		PartialChanceEntry* partial = (!nodeKey.empty()
			&& (!singletonRevealStreaming || (canonicalMainEnabled && concreteWorldCaching)))
			? partialChanceFor(nodeKey) : nullptr;
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
		PartialDecisionEntry* partial = (!nodeKey.empty()
			&& (!singletonRevealStreaming || (canonicalMainEnabled && concreteWorldCaching)))
			? partialDecisionFor(nodeKey) : nullptr;
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
			if (canonicalMainEnabled && (!singletonRevealStreaming || concreteWorldCaching)
				&& child->selectType == SelectType::Main
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
		// Main states still contain many commuting action orders.  The
		// dynamic partition key preserves every card count that can affect another
		// turn-local deck query while omitting the irrelevant prize identities already
		// quotiented by revealAndReplayPartitionedTurnSearch.
		std::string partitionMainKey = partitionTurnMainKey(state);
		if (!partitionMainKey.empty()) {
			ExactScore partitionCached;
			auto local = partitionTurnMainScores.find(partitionMainKey);
			bool partitionHit = local != partitionTurnMainScores.end();
			bool sharedHit = false;
			if (partitionHit) partitionCached = local->second;
			else if (usingSharedTable) partitionHit = sharedHit = transposition->find(partitionMainKey, partitionCached);
			if (partitionHit) {
				metrics.merged++; metrics.canonicalStateMerges++;
				if (sharedHit) metrics.rootSharedTTHits++;
				return partitionCached;
			}
		}
		// A concrete reveal world can still contain a large turn DAG (notably when
		// Enriching Energy enables a four-card draw). The former streaming fast path
		// disabled both TT and partial cursors, so a 500k-node quantum restarted that
		// world from its first action forever. Retain exact canonical state and
		// resumable entries whenever root canonicalization is enabled.
		const bool cacheConcreteWorld = singletonRevealStreaming && canonicalMainEnabled && concreteWorldCaching
			&& (state.selectType == SelectType::Main
				|| state.exact.pending == ExactPendingType::Draw
				|| state.exact.pending == ExactPendingType::TakePrize);
		std::string key;
		if (!partitionMainKey.empty()) key = partitionMainKey;
		else if (!singletonRevealStreaming || cacheConcreteWorld) key = keyFor(state);
		const bool shareable = usingSharedTable && (!singletonRevealStreaming || cacheConcreteWorld);
		ExactScore cached;
		bool cacheHit = false;
		if (shareable) cacheHit = transposition->find(key, cached);
		else if (!singletonRevealStreaming || cacheConcreteWorld) {
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
			case ExactBlockReason::UnknownOpponentList:
				metrics.unknownOpponentList++; metrics.structurallyBlocked = true; break;
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
		if (result.certified && (!singletonRevealStreaming || cacheConcreteWorld)) {
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
		if (result.certified && !partitionMainKey.empty()) {
			partitionTurnMainScores.emplace(partitionMainKey, result);
			if (usingSharedTable) transposition->store(std::move(partitionMainKey), result);
		}
		metrics.partialTableBytes = partialBytes;
		metrics.sessionBytes = transposition->bytes() + localTranspositionBytes + policyBytes + partialBytes
			+ beliefTranspositionBytes + evaluationCache.size() * 128ULL;
		return result;
	}
};
