#pragma once

#include "ExactSearchHooks.h"

#include <chrono>
#include <numeric>
#include <unordered_map>

struct ExactFraction {
	long long numerator = 0;
	unsigned long long denominator = 1;
	bool valid = true;

	static ExactFraction integer(long long value) { return { value, 1, true }; }

	void normalize() {
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
		ExactFraction result = *this;
		auto g1 = std::gcd(weight, total); weight /= g1; total /= g1;
		unsigned long long magnitude = result.numerator < 0 ? (unsigned long long)(-(result.numerator + 1)) + 1 : (unsigned long long)result.numerator;
		auto g2 = std::gcd(magnitude, total); result.numerator /= (long long)g2; total /= g2;
		auto g3 = std::gcd(result.denominator, weight); result.denominator /= g3; weight /= g3;
		long long n; unsigned long long d;
		if (!checkedMul(result.numerator, weight, n) || !checkedMulU(result.denominator, total, d)) return { 0, 1, false };
		result.numerator = n; result.denominator = d; result.normalize(); return result;
	}

	static ExactFraction add(const ExactFraction& a, const ExactFraction& b) {
		if (!a.valid || !b.valid) return { 0, 1, false };
		auto g = std::gcd(a.denominator, b.denominator);
		unsigned long long am = b.denominator / g, bm = a.denominator / g;
		long long an, bn;
		unsigned long long d;
		if (!checkedMul(a.numerator, am, an) || !checkedMul(b.numerator, bm, bn)) return { 0, 1, false };
		if ((bn > 0 && an > std::numeric_limits<long long>::max() - bn)
			|| (bn < 0 && an < std::numeric_limits<long long>::min() - bn)) return { 0, 1, false };
		if (!checkedMulU(a.denominator, am, d)) return { 0, 1, false };
		ExactFraction result{ an + bn, d, true }; result.normalize(); return result;
	}
};

inline int ExactCompare(const ExactFraction& a, const ExactFraction& b) {
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

struct ExactScore {
	ExactFraction lower = ExactFraction::integer(-100'000'000);
	ExactFraction upper = ExactFraction::integer(100'000'000);
	std::vector<int> action;
	bool certified = false;
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
};

struct ExactDecision {
	ExactScore score;
	ExactMetrics metrics;
};

class ExactPlanner {
public:
	ExactPlanner(const int* deck, const int* handValues, int deckCount, int budgetMilliseconds)
		: deadline(std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(1, budgetMilliseconds))) {
		for (int i = 0; i < deckCount; ++i) {
			profileCount[deck[i]]++;
			handValue[deck[i]] = handValues == nullptr ? 100 : handValues[i];
		}
	}

	ExactDecision decide(State root) {
		actor = root.selectPlayer;
		initializeHidden(root);
		root.game->config.manualCoin = true;
		root.exact.enabled = true;
		root.exact.actor = (signed char)actor;
		ExactDecision result;
		result.score = solve(root);
		result.metrics = metrics;
		return result;
	}

	ExactDecision evaluateRootAction(State root, int optionIndex) {
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
			result.score = child.exact.pending == ExactPendingType::RevealDeck
				? revealAndReplay(root, { optionIndex }) : solve(child);
			result.score.action = { optionIndex };
		}
		result.metrics = metrics;
		return result;
	}

private:
	std::unordered_map<int, int> profileCount;
	std::unordered_map<int, int> handValue;
	std::chrono::steady_clock::time_point deadline;
	ExactMetrics metrics;
	int actor = 0;
	std::unordered_map<std::string, ExactScore> transposition;
	static constexpr size_t MaxTranspositionEntries = 250'000;
	static constexpr size_t MaxTranspositionBytes = 550ULL * 1024ULL * 1024ULL;
	size_t transpositionBytes = 0;

	bool expired() {
		if (std::chrono::steady_clock::now() < deadline) return false;
		metrics.timedOut = true; return true;
	}

	std::string keyFor(State state) const {
		state.logs.clear();
		state.logIndex = {};
		state.selected.clear();
		BinaryWriter writer;
		state.serialize(writer);
		return std::string((const char*)writer.buf.data(), writer.buf.size());
	}

	void initializeHidden(State& state) {
		state.exact = {};
		state.exact.enabled = true;
		state.exact.actor = (signed char)actor;
		auto remaining = profileCount;
		for (const Card& card : state.allCard) {
			auto it = remaining.find(card.cardId);
			if (card.cardId != 0 && card.playerIndex == actor && it != remaining.end() && it->second > 0) it->second--;
		}
		for (const auto& [id, count] : remaining) {
			if (count <= 0) continue;
			int index = state.exact.typeCount++;
			state.exact.cardId[index] = id;
			state.exact.cardCount[index] = (unsigned char)count;
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

	std::vector<std::vector<int>> legalActions(const State& state) const {
		std::vector<std::vector<int>> output;
		std::vector<int> current;
		std::function<void(int, int)> choose = [&](int start, int left) {
			if (left == 0) { output.push_back(current); return; }
			for (int i = start; i <= (int)state.options.size() - left; ++i) {
				current.push_back(i); choose(i + 1, left - 1); current.pop_back();
			}
		};
		for (int count = state.selectMin; count <= state.selectMax; ++count) choose(0, count);
		if (state.selectType == SelectType::Main) {
			std::stable_sort(output.begin(), output.end(), [&](const auto& left, const auto& right) {
				auto isEnd = [&](const auto& action) {
					return action.size() == 1 && state.options[action[0]].type == SelectOptionType::End;
				};
				return isEnd(left) && !isEnd(right);
			});
		}
		return output;
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
			return true;
		} catch (...) {
			metrics.exceptions++;
			metrics.lastException = "unknown";
			state.exact.pending = ExactPendingType::Opaque;
			return true;
		}
	}

	CardRef materialize(State& state, int player, AreaType area, int areaIndex, int cardId) {
		int freeIndex = -1;
		for (int i = 3; i < (int)state.allCard.size(); ++i) if (state.allCard[i].cardId == 0) { freeIndex = i; break; }
		if (freeIndex < 0) throw std::runtime_error("no exact card slot");
		CardRef ref(freeIndex);
		Card& card = state.allCard[freeIndex]; card = {}; card.init(cardId, state.moveCounter++, player); card.area = area;
		state.getCardRef(area, areaIndex, player) = ref;
		return ref;
	}

	void decrementPool(State& state, int cardId) {
		for (int i = 0; i < state.exact.typeCount; ++i) if (state.exact.cardId[i] == cardId) {
			if (state.exact.cardCount[i] == 0) throw std::runtime_error("empty hidden card type");
			state.exact.cardCount[i]--; return;
		}
		throw std::runtime_error("unknown hidden card type");
	}

	static unsigned long long chooseCount(int n, int k) {
		if (k < 0 || k > n) return 0;
		k = std::min(k, n - k);
		unsigned long long value = 1;
		for (int i = 1; i <= k; ++i) value = value * (unsigned long long)(n - k + i) / (unsigned long long)i;
		return value;
	}

	void materializeUnknownZones(State& state, const std::vector<int>& prizeCounts) {
		int player = state.exact.actor;
		PlayerState& ps = state.players[player];
		std::vector<int> prizeIds, deckIds;
		for (int i = 0; i < state.exact.typeCount; ++i) {
			int p = prizeCounts[i];
			for (int n = 0; n < p; ++n) prizeIds.push_back(state.exact.cardId[i]);
			for (int n = p; n < state.exact.cardCount[i]; ++n) deckIds.push_back(state.exact.cardId[i]);
		}
		int prizeAt = 0, deckAt = 0;
		for (int i = 0; i < ps.prize.size(); ++i) if (ps.prize[i].isNull()) {
			if (prizeAt >= (int)prizeIds.size()) throw std::runtime_error("prize materialization mismatch");
			materialize(state, player, AreaType::Prize, i, prizeIds[prizeAt++]);
		}
		for (int i = 0; i < ps.deck.size(); ++i) if (ps.deck[i].isNull()) {
			if (deckAt >= (int)deckIds.size()) throw std::runtime_error("deck materialization mismatch");
			materialize(state, player, AreaType::Deck, i, deckIds[deckAt++]);
		}
		if (prizeAt != (int)prizeIds.size() || deckAt != (int)deckIds.size()) throw std::runtime_error("hidden zone size mismatch");
		state.exact.deckUnknown[player] = false;
		state.exact.deckExchangeable[player] = true;
		state.exact.prizeExchangeable[player] = true;
		state.exact.clearPending();
	}

	ExactScore revealAndReplay(const State& parent, const std::vector<int>& action) {
		int prizeSize = parent.players[actor].prize.size();
		int totalHidden = 0;
		for (int i = 0; i < parent.exact.typeCount; ++i) totalHidden += parent.exact.cardCount[i];
		if (prizeSize < 0 || prizeSize > totalHidden) return unknown();
		unsigned long long totalWeight = chooseCount(totalHidden, prizeSize);
		if (totalWeight == 0) return unknown();
		ExactFraction lower = ExactFraction::integer(0), upper = ExactFraction::integer(0);
		bool certified = true, any = false;
		std::vector<int> prizeCounts(parent.exact.typeCount, 0);
		std::function<bool(int, int, unsigned long long)> enumerate = [&](int type, int left, unsigned long long weight) {
			if (expired()) return false;
			if (type == parent.exact.typeCount) {
				if (left != 0) return true;
				State world = parent;
				try {
					materializeUnknownZones(world, prizeCounts);
					if (!advance(world, action)) return true;
				} catch (...) { return false; }
				ExactScore score = solve(world);
				auto l = score.lower.scaled(weight, totalWeight), u = score.upper.scaled(weight, totalWeight);
				lower = ExactFraction::add(lower, l); upper = ExactFraction::add(upper, u);
				if (!lower.valid || !upper.valid) { metrics.arithmeticOverflow = true; return false; }
				certified = certified && score.certified; any = true; return true;
			}
			int available = parent.exact.cardCount[type];
			for (int take = 0; take <= std::min(available, left); ++take) {
				prizeCounts[type] = take;
				if (!enumerate(type + 1, left - take, weight * chooseCount(available, take))) return false;
			}
			prizeCounts[type] = 0; return true;
		};
		if (!enumerate(0, prizeSize, 1) || !any) return unknown();
		return { lower, upper, {}, certified && ExactCompare(lower, upper) == 0 };
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
			decrementPool(state, cardId);
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
			materialize(state, player, AreaType::Prize, index, cardId); decrementPool(state, cardId);
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
			for (int i = 0; i < state.exact.typeCount; ++i) if (state.exact.cardCount[i] > 0)
				result.push_back({ state.exact.cardId[i], state.exact.cardCount[i] });
		} else {
			std::unordered_map<int, unsigned long long> counts;
			const auto& list = state.exact.pending == ExactPendingType::Draw ? state.players[player].deck : state.players[player].prize;
			for (CardRef ref : list) if (!ref.isNull()) counts[state.getCard(ref).cardId]++;
			for (auto [id, count] : counts) result.push_back({ id, count });
		}
		return result;
	}

	ExactScore chance(State state) {
		auto types = chanceCardTypes(state);
		if (types.empty()) return unknown();
		unsigned long long total = 0; for (auto [_, w] : types) total += w;
		ExactFraction lower = ExactFraction::integer(0), upper = ExactFraction::integer(0);
		bool certified = true;
		for (auto [id, weight] : types) {
			if (expired()) return unknown();
			State child = state;
			try {
				if (state.exact.pending == ExactPendingType::Draw) resolveDraw(child, id); else resolvePrize(child, id);
			} catch (...) { return unknown(); }
			ExactScore score = solve(child);
			auto l = score.lower.scaled(weight, total), u = score.upper.scaled(weight, total);
			lower = ExactFraction::add(lower, l); upper = ExactFraction::add(upper, u);
			if (!lower.valid || !upper.valid) { metrics.arithmeticOverflow = true; return unknown(); }
			certified = certified && score.certified;
		}
		return { lower, upper, {}, certified && ExactCompare(lower, upper) == 0 };
	}

	ExactScore coinChance(const State& state) {
		ExactFraction lower = ExactFraction::integer(0), upper = ExactFraction::integer(0);
		bool certified = true;
		for (int option = 0; option < 2; ++option) {
			State child = state;
			if (!advance(child, { option })) return unknown();
			ExactScore score = solve(child);
			lower = ExactFraction::add(lower, score.lower.scaled(1, 2));
			upper = ExactFraction::add(upper, score.upper.scaled(1, 2));
			if (!lower.valid || !upper.valid) { metrics.arithmeticOverflow = true; return unknown(); }
			certified = certified && score.certified;
		}
		return { lower, upper, {}, certified && ExactCompare(lower, upper) == 0 };
	}

	ExactScore decision(const State& state, bool maximize) {
		ExactScore result;
		bool first = true;
		ExactFraction aggregate = maximize ? ExactFraction::integer(-100'000'000) : ExactFraction::integer(100'000'000);
		bool allCertified = true;
		for (const auto& action : legalActions(state)) {
			if (expired()) {
				if (first) return unknown();
				if (maximize) result.upper = ExactFraction::integer(100'000'000);
				else result.lower = ExactFraction::integer(-100'000'000);
				result.certified = false;
				return result;
			}
			State child = state;
			if (!advance(child, action)) continue;
			ExactScore score = child.exact.pending == ExactPendingType::RevealDeck
				? revealAndReplay(state, action) : solve(child);
			if (maximize) {
				if (ExactCompare(score.upper, aggregate) > 0) aggregate = score.upper;
			} else {
				if (ExactCompare(score.lower, aggregate) < 0) aggregate = score.lower;
			}
			allCertified = allCertified && score.certified;
			if (first || (maximize ? ExactCompare(score.lower, result.lower) > 0 : ExactCompare(score.upper, result.upper) < 0)) {
				result = score; result.action = action; first = false;
			}
		}
		if (first) return unknown();
		if (maximize) result.upper = aggregate; else result.lower = aggregate;
		result.certified = allCertified && ExactCompare(result.lower, result.upper) == 0;
		return result;
	}

	ExactScore solve(State state) {
		metrics.expanded++;
		if (expired()) return unknown();
		std::string key = keyFor(state);
		auto cached = transposition.find(key);
		if (cached != transposition.end()) { metrics.merged++; return cached->second; }
		ExactScore result;
		if (state.isFinish() || IsExactTurnLeaf(state)) {
			metrics.leaves++;
			auto value = ExactFraction::integer(evaluate(state));
			result = { value, value, {}, true };
		} else if (state.exact.pending == ExactPendingType::Opaque || state.exact.pending == ExactPendingType::RevealDeck) {
			metrics.opaque++; metrics.lastPendingDetail = state.exact.pendingDetail; result = unknown();
		} else if (state.exact.pending == ExactPendingType::Draw || state.exact.pending == ExactPendingType::TakePrize) {
			result = chance(state);
		} else if (state.selectType == SelectType::YesNo && state.selectContext == SelectContext::CoinHead) {
			result = coinChance(state);
		} else if (state.selectType == SelectType::None) {
			try { state.step(); result = solve(state); } catch (...) { result = unknown(); }
		} else {
			result = decision(state, state.selectPlayer == actor);
		}
		size_t entryBytes = key.size() + sizeof(ExactScore) + 96;
		if (result.certified && transposition.size() < MaxTranspositionEntries
			&& transpositionBytes + entryBytes <= MaxTranspositionBytes) {
			transpositionBytes += entryBytes;
			transposition.emplace(std::move(key), result);
		}
		return result;
	}
};
