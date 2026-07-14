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
};

struct ExactDecision {
	ExactScore score;
	ExactMetrics metrics;
	std::vector<ExactRootActionValue> rootActions;
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
		result.metrics = metrics;
		result.rootActions = rootActionValues;
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
				? revealAndReplay(root, { optionIndex }) : solveOwned(std::make_unique<State>(std::move(child)));
			result.score.action = { optionIndex };
		}
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
		unsigned long long totalWeight = 0, processedWeight = 0, pendingWeight = 0;
		bool initialized = false, handActive = false, pendingWorld = false;
		size_t accountedBytes = 0;
	};
	std::unordered_map<int, int> actorProfileCount;
	std::unordered_map<int, int> opponentProfileCount;
	std::unordered_map<int, int> handValue;
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

	std::string observationKeyFor(const State& state) const {
		const int observer = state.selectPlayer;
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
			if (state.selectDeck && player == observer)
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
		metrics.policyNodes++; metrics.sessionBytes = transposition->bytes() + localTranspositionBytes + policyBytes + partialBytes;
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

	long long evaluate(const State& state) const {
		if (state.isFinish()) {
			int winner = state.winPlayer();
			return winner == actor ? 100'000'000 : (winner == 2 ? 0 : -100'000'000);
		}
		if (evaluator && evaluator->isLoaded()) return evaluator->evaluate(state, actor);
		int enemy = 1 - actor;
		const PlayerState& me = state.players[actor];
		const PlayerState& opp = state.players[enemy];
		long long value = 1'000'000LL * (opp.prize.size() - me.prize.size());
		value += 2'000LL * ((me.active.size() + me.bench.size()) - (opp.active.size() + opp.bench.size()));
		value += 500LL * (me.energy.size() - opp.energy.size());
		for (CardRef ref : me.hand) {
			if (ref.isNull()) continue;
			int id = state.getCard(ref).cardId;
			auto it = handValue.find(id); value += (it == handValue.end() ? 100 : it->second);
		}
		value -= 80LL * opp.hand.size();
		int shortage = std::max(0, 4 - me.deck.size());
		value -= 2'000LL * shortage * shortage;
		auto damage = [&](const PlayerState& ps) {
			long long total = 0;
			for (CardRef ref : ps.active) if (!ref.isNull()) total += state.getCard(ref).damage;
			for (CardRef ref : ps.bench) if (!ref.isNull()) total += state.getCard(ref).damage;
			return total;
		};
		value += 100LL * (damage(opp) - damage(me));
		return value;
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

	static unsigned long long chooseCount(int n, int k) {
		if (n < 0 || n > DECK_SIZE || k < 0 || k > n) return 0;
		static const auto table = [] {
			std::array<std::array<unsigned long long, DECK_SIZE + 1>, DECK_SIZE + 1> value{};
			for (int row = 0; row <= DECK_SIZE; ++row) {
				value[row][0] = value[row][row] = 1;
				for (int column = 1; column < row; ++column)
					value[row][column] = value[row - 1][column - 1] + value[row - 1][column];
			}
			return value;
		}();
		return table[n][k];
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

	ExactScore revealAndReplay(const State& parent, const std::vector<int>& action) {
		int player = parent.exact.pendingPlayer >= 0 ? parent.exact.pendingPlayer : actor;
		if (!parent.exact.profileKnown[player]) return unknown();
		int prizeSize = 0; for (CardRef ref : parent.players[player].prize) if (ref.isNull()) prizeSize++;
		int handSize = 0; for (CardRef ref : parent.players[player].hand) if (ref.isNull()) handSize++;
		int totalHidden = 0;
		for (int i = 0; i < parent.exact.typeCount[player]; ++i) totalHidden += parent.exact.cardCount[player][i];
		if (prizeSize < 0 || prizeSize > totalHidden) return unknown();
		unsigned long long totalWeight = chooseCount(totalHidden, prizeSize) * chooseCount(totalHidden - prizeSize, handSize);
		if (totalWeight == 0) return unknown();
		std::string revealKey = keyFor(parent) + "\x1fR\x1f" + actionEquivalenceKey(parent, action);
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
		auto incomplete = [&](const ExactScore* current = nullptr, unsigned long long currentWeight = 0) {
			ExactFraction lower = partial->completedLower, upper = partial->completedUpper;
			unsigned long long covered = partial->processedWeight;
			if (current != nullptr) {
				lower = ExactFraction::add(lower, current->lower.scaled(currentWeight, totalWeight));
				upper = ExactFraction::add(upper, current->upper.scaled(currentWeight, totalWeight));
				covered += currentWeight;
			}
			unsigned long long remaining = covered >= totalWeight ? 0 : totalWeight - covered;
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
				unsigned long long weight = 1;
				for (int i = 0; i < parent.exact.typeCount[player]; ++i) {
					int available = parent.exact.cardCount[player][i];
					weight *= chooseCount(available, partial->prizeCounts[i]);
					weight *= chooseCount(available - partial->prizeCounts[i], partial->handCounts[i]);
				}
				partial->pendingWeight = weight; partial->pendingWorld = true;
			}
			auto world = std::make_unique<State>(parent);
			try {
				materializeUnknownZones(*world, player, partial->prizeCounts, partial->handCounts);
				if (!advance(*world, action)) return unknown();
			} catch (...) { return unknown(); }
			ExactScore score = solveOwned(std::move(world));
			if (!score.certified) return incomplete(&score, partial->pendingWeight);
			partial->completedLower = ExactFraction::add(partial->completedLower,
				score.lower.scaled(partial->pendingWeight, totalWeight));
			partial->completedUpper = ExactFraction::add(partial->completedUpper,
				score.upper.scaled(partial->pendingWeight, totalWeight));
			if (!partial->completedLower.valid || !partial->completedUpper.valid) {
				metrics.arithmeticOverflow = true; return unknown();
			}
			partial->processedWeight += partial->pendingWeight;
			metrics.enumeratedHiddenWorlds++;
			partial->pendingWeight = 0; partial->pendingWorld = false;
		}
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

	std::vector<std::pair<int, unsigned long long>> chanceCardTypes(const State& state) const {
		std::vector<std::pair<int, unsigned long long>> result;
		int player = state.exact.pendingPlayer;
		if (player != actor && state.exact.deckUnknown[player]) return result;
		if (state.exact.deckUnknown[player]) {
			for (int i = 0; i < state.exact.typeCount[player]; ++i) if (state.exact.cardCount[player][i] > 0)
				result.push_back({ state.exact.cardId[player][i], state.exact.cardCount[player][i] });
		} else {
			std::unordered_map<int, unsigned long long> counts;
			const auto& list = state.exact.pending == ExactPendingType::Draw ? state.players[player].deck : state.players[player].prize;
			for (CardRef ref : list) if (!ref.isNull()) counts[state.getCard(ref).cardId]++;
			for (auto [id, count] : counts) result.push_back({ id, count });
		}
		return result;
	}

	ExactScore chance(const State& state, const std::string& nodeKey) {
		auto types = chanceCardTypes(state);
		if (types.empty()) return unknown();
		PartialChanceEntry* partial = partialChanceFor(nodeKey);
		unsigned long long total = 0; for (auto [_, w] : types) total += w;
		ExactFraction lower = ExactFraction::integer(0), upper = ExactFraction::integer(0);
		bool certified = true;
		unsigned long long processed = 0;
		for (auto [id, weight] : types) {
			if (expired()) {
				metrics.partialChanceNodes++;
				unsigned long long remaining = total - processed;
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
				score = *saved; metrics.resumedChanceMass += weight;
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
		return { lower, upper, {}, certified && ExactCompare(lower, upper) == 0 };
	}

	ExactScore coinChance(const State& state, const std::string& nodeKey) {
		(void)nodeKey;
		struct CoinOutcome { std::unique_ptr<State> state; unsigned long long weight; };
		std::vector<CoinOutcome> outcomes;
		std::unordered_map<std::string, size_t, ExactStringHasher> bySuccessor;
		for (int option = 0; option < 2; ++option) {
			if (expired()) { metrics.partialChanceNodes++; return unknown(); }
			auto child = std::make_unique<State>(state);
			if (!advance(*child, { option })) return unknown();
			std::string key = keyFor(*child);
			auto [found, inserted] = bySuccessor.emplace(std::move(key), outcomes.size());
			if (inserted) outcomes.push_back({ std::move(child), 1 });
			else { outcomes[found->second].weight++; metrics.distributionMerges++; }
		}
		ExactFraction lower = ExactFraction::integer(0), upper = ExactFraction::integer(0);
		bool certified = true;
		for (CoinOutcome& outcome : outcomes) {
			if (expired()) { metrics.partialChanceNodes++; return unknown(); }
			ExactScore score = solveOwned(std::move(outcome.state));
			lower = ExactFraction::add(lower, score.lower.scaled(outcome.weight, 2));
			upper = ExactFraction::add(upper, score.upper.scaled(outcome.weight, 2));
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
		PartialDecisionEntry* partial = partialDecisionFor(nodeKey);
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
			if (canonicalMainEnabled && child->selectType == SelectType::Main
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
					? revealAndReplay(state, action) : solveOwned(std::move(child));
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
			rememberPolicy(state, result, metrics.expanded - expandedBefore);
			return result;
		}
		if (maximize) result.upper = aggregate; else result.lower = aggregate;
		result.certified = allCertified && ExactCompare(result.lower, result.upper) == 0;
		rememberPolicy(state, result, metrics.expanded - expandedBefore);
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
			return { value, value, {}, true };
		}
		std::string key = keyFor(state);
		const bool shareable = usingSharedTable;
		ExactScore cached;
		bool cacheHit = false;
		if (shareable) cacheHit = transposition->find(key, cached);
		else {
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
		if (result.certified) {
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
		metrics.partialTableBytes = partialBytes;
		metrics.sessionBytes = transposition->bytes() + localTranspositionBytes + policyBytes + partialBytes;
		return result;
	}
};
