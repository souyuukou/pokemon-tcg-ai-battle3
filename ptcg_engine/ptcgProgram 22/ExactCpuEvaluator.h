#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>

// A deliberately tiny, deterministic evaluator for the exact-search hot path.
// Training uses floating point, but the exported model and inference are fully
// integer so Windows and Linux evaluate every leaf identically.
class ExactCpuEvaluator {
public:
	static constexpr int InputCount = 48;
	static constexpr int HiddenCount = 8;
	static constexpr int WeightScale = 4096;
	static constexpr int OutputScale = WeightScale * WeightScale;
	static constexpr int ScoreScale = 10'000'000;

	bool load(const std::string& path, std::string& error) {
		std::ifstream stream(path, std::ios::binary);
		if (!stream) { error = "cannot open evaluator model"; return false; }
		Header header{};
		stream.read(reinterpret_cast<char*>(&header), sizeof(header));
		const Header expected{};
		if (!stream || std::string(header.magic, header.magic + 8) != std::string(expected.magic, expected.magic + 8)
			|| header.version != 1 || header.inputs != InputCount || header.hidden != HiddenCount
			|| header.weightScale != WeightScale || header.scoreScale != ScoreScale) {
			error = "invalid evaluator model header"; return false;
		}
		stream.read(reinterpret_cast<char*>(inputWeight.data()), sizeof(inputWeight));
		stream.read(reinterpret_cast<char*>(hiddenBias.data()), sizeof(hiddenBias));
		stream.read(reinterpret_cast<char*>(outputWeight.data()), sizeof(outputWeight));
		stream.read(reinterpret_cast<char*>(&outputBias), sizeof(outputBias));
		if (!stream || stream.peek() != std::char_traits<char>::eof()) {
			error = "invalid evaluator model length"; return false;
		}
		loaded = true; modelPath = path; return true;
	}

	bool isLoaded() const { return loaded; }
	const std::string& path() const { return modelPath; }

	long long evaluate(const State& state, int actor) const {
		std::array<int, InputCount> x{};
		extract(state, actor, x);
		long long output = outputBias;
		for (int h = 0; h < HiddenCount; ++h) {
			long long sum = hiddenBias[h];
			for (int i = 0; i < InputCount; ++i)
				sum += (long long)inputWeight[h * InputCount + i] * x[i];
			// Clipped ReLU keeps the product bounded and makes inference cheap.
			sum = std::clamp(sum, 0LL, 127LL * WeightScale);
			output += (long long)outputWeight[h] * sum;
		}
		long long score = output >= 0
			? (output * ScoreScale + OutputScale / 2) / OutputScale
			: -(((-output) * ScoreScale + OutputScale / 2) / OutputScale);
		// The network itself is an integer-valued evaluator. A coarse output unit
		// greatly increases rational cancellation in exact chance aggregation.
		constexpr long long quantum = 1'000;
		score = score >= 0 ? ((score + quantum / 2) / quantum) * quantum
			: -(((-score + quantum / 2) / quantum) * quantum);
		return std::clamp(score, -99'000'000LL, 99'000'000LL);
	}

	static ExactCpuEvaluator& global() { static ExactCpuEvaluator value; return value; }
	static std::mutex& globalMutex() { static std::mutex value; return value; }

private:
	struct Header {
		char magic[8] = { 'P','T','C','G','E','V','1','\0' };
		std::uint32_t version = 1;
		std::uint32_t inputs = InputCount;
		std::uint32_t hidden = HiddenCount;
		std::uint32_t weightScale = WeightScale;
		std::uint32_t scoreScale = ScoreScale;
	};

	std::array<std::int16_t, InputCount * HiddenCount> inputWeight{};
	std::array<std::int32_t, HiddenCount> hiddenBias{};
	std::array<std::int16_t, HiddenCount> outputWeight{};
	std::int64_t outputBias = 0;
	bool loaded = false;
	std::string modelPath;

	static int clip(int value) { return std::clamp(value, -127, 127); }
	static unsigned bucket(int cardId, int zone) {
		std::uint32_t x = (std::uint32_t)cardId * 0x9e3779b1U + (std::uint32_t)zone * 0x85ebca6bU;
		x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15;
		return x % 24U;
	}

	static void addCards(const State& state, const auto& list, int sign, int zone,
		std::array<int, InputCount>& x) {
		for (CardRef ref : list) if (!ref.isNull()) {
			int id = state.getCard(ref).cardId;
			if (id > 0) x[24 + bucket(id, zone)] = clip(x[24 + bucket(id, zone)] + sign * 8);
		}
	}

	static void extract(const State& state, int actor, std::array<int, InputCount>& x) {
		int enemy = 1 - actor;
		const PlayerState& me = state.players[actor];
		const PlayerState& opp = state.players[enemy];
		auto delta = [](int a, int b, int scale = 1) { return clip((a - b) * scale); };
		auto cardCount = [](const auto& a, const auto& b) { return (int)a.size() + (int)b.size(); };
		auto damage = [&](const PlayerState& ps) {
			int value = 0;
			for (CardRef ref : ps.active) if (!ref.isNull()) value += state.getCard(ref).damage;
			for (CardRef ref : ps.bench) if (!ref.isNull()) value += state.getCard(ref).damage;
			return value;
		};
		x[0] = delta((int)opp.prize.size(), (int)me.prize.size(), 16);
		x[1] = delta((int)me.hand.size(), (int)opp.hand.size(), 4);
		x[2] = delta((int)me.deck.size(), (int)opp.deck.size(), 2);
		x[3] = delta((int)me.active.size(), (int)opp.active.size(), 24);
		x[4] = delta((int)me.bench.size(), (int)opp.bench.size(), 12);
		x[5] = delta((int)me.energy.size(), (int)opp.energy.size(), 6);
		x[6] = clip((damage(opp) - damage(me)) / 10);
		x[7] = delta((int)opp.trash.size(), (int)me.trash.size(), 2);
		x[8] = state.supporterPlayed ? -16 : 16;
		x[9] = state.energyPlayed ? -12 : 12;
		x[10] = state.retreated ? -8 : 8;
		x[11] = clip(state.turn);
		x[12] = clip(state.turnActionCount / 4);
		x[13] = delta(state.benchCapacity(actor), state.benchCapacity(enemy), 8);
		x[14] = delta(cardCount(me.active, me.bench), cardCount(opp.active, opp.bench), 8);
		x[15] = clip(std::max(0, 5 - (int)me.deck.size()) * -16);
		x[16] = clip(std::max(0, 5 - (int)opp.deck.size()) * 16);
		x[17] = me.isPoisoned() ? -12 : 0; x[17] += opp.isPoisoned() ? 12 : 0;
		x[18] = me.isBurned() ? -10 : 0; x[18] += opp.isBurned() ? 10 : 0;
		x[19] = me.badStatus == BadStatusType::Paralyzed ? -16 : 0; x[19] += opp.badStatus == BadStatusType::Paralyzed ? 16 : 0;
		x[20] = me.badStatus == BadStatusType::Asleep ? -10 : 0; x[20] += opp.badStatus == BadStatusType::Asleep ? 10 : 0;
		x[21] = me.badStatus == BadStatusType::Confused ? -8 : 0; x[21] += opp.badStatus == BadStatusType::Confused ? 8 : 0;
		x[22] = 0; // reserved for a future replay-visible turn-history feature
		x[23] = 1;
		addCards(state, me.hand, 1, 1, x); addCards(state, opp.hand, -1, 1, x);
		addCards(state, me.active, 1, 2, x); addCards(state, opp.active, -1, 2, x);
		addCards(state, me.bench, 1, 3, x); addCards(state, opp.bench, -1, 3, x);
		addCards(state, me.trash, 1, 4, x); addCards(state, opp.trash, -1, 4, x);
	}
};
