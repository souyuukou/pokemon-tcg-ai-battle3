#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Collision-free sparse NNUE used by exact turn leaves.  The model owns an
// explicit sorted card-id vocabulary; only ids absent from the model use slot
// zero.  Inference is allocation-free and integer-only.
class ExactSparseEvaluatorV2 {
public:
	static constexpr int DenseCount = 40;
	static constexpr int HiddenCount = 32;
	static constexpr int RelationCount = 30;
	static constexpr int WeightScale = 4096;
	static constexpr int BeliefScale = 256;
	static constexpr int ScoreScale = 100'000'000;
	static constexpr int NonTerminalLimit = 90'000'000;
	static constexpr int SchemaVersion = 2;

	bool load(const std::string& path, std::string& error) {
		std::ifstream stream(path, std::ios::binary);
		if (!stream) { error = "cannot open evaluator model"; return false; }
		std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(stream)), {});
		if (raw.size() < sizeof(Header)) { error = "invalid evaluator model length"; return false; }
		Header header{}; std::memcpy(&header, raw.data(), sizeof(header));
		if (std::memcmp(header.magic, "PTCGEV2", 7) != 0 || header.version != 2
			|| header.denseCount != DenseCount || header.hiddenCount != HiddenCount
			|| header.relationCount != RelationCount || header.cardCount == 0
			|| header.weightScale != WeightScale || header.beliefScale != BeliefScale
			|| header.scoreScale != ScoreScale || header.schemaVersion != SchemaVersion) {
			error = "invalid evaluator V2 header"; return false;
		}
		size_t expected = sizeof(Header)
			+ (size_t)header.cardCount * sizeof(std::int32_t)
			+ (size_t)DenseCount * HiddenCount * sizeof(std::int16_t)
			+ (size_t)RelationCount * header.cardCount * HiddenCount * sizeof(std::int16_t)
			+ (size_t)HiddenCount * sizeof(std::int32_t)
			+ (size_t)HiddenCount * sizeof(std::int16_t) + sizeof(std::int64_t);
		if (raw.size() != expected) { error = "invalid evaluator V2 payload length"; return false; }
		size_t offset = sizeof(Header);
		auto read = [&](auto& destination, size_t count) {
			using Value = typename std::decay_t<decltype(destination)>::value_type;
			destination.resize(count); std::memcpy(destination.data(), raw.data() + offset, count * sizeof(Value));
			offset += count * sizeof(Value);
		};
		read(cardIds, header.cardCount);
		if (cardIds.front() != 0 || !std::is_sorted(cardIds.begin(), cardIds.end())
			|| std::adjacent_find(cardIds.begin(), cardIds.end()) != cardIds.end()) {
			error = "evaluator V2 card ids must be unique, sorted, and start with UNK"; return false;
		}
		if (checksumCards(cardIds) != header.cardChecksum) { error = "evaluator V2 card checksum mismatch"; return false; }
		read(denseWeight, (size_t)DenseCount * HiddenCount);
		read(sparseWeight, (size_t)RelationCount * header.cardCount * HiddenCount);
		read(hiddenBias, HiddenCount); read(outputWeight, HiddenCount);
		std::memcpy(&outputBias, raw.data() + offset, sizeof(outputBias));
		int maxId = cardIds.back(); cardIndex.assign((size_t)std::max(0, maxId) + 1, 0);
		for (int i = 1; i < (int)cardIds.size(); ++i) if (cardIds[i] >= 0) cardIndex[cardIds[i]] = i;
		modelHashValue = fnv1a(raw.data(), raw.size()); loaded = true; modelPath = path; return true;
	}

	bool isLoaded() const { return loaded; }
	std::uint64_t modelHash() const { return modelHashValue; }
	const std::string& path() const { return modelPath; }
	struct SparseInput { std::int32_t cardId; std::int16_t relation; std::int16_t q8; };
	struct FeatureRecord {
		std::array<std::int16_t, DenseCount> dense{};
		std::vector<SparseInput> sparse;
	};
	struct BeliefInput {
		const std::unordered_map<int, int>* ownDeckQ8 = nullptr;
		const std::unordered_map<int, int>* ownPrizeQ8 = nullptr;
	};
	static FeatureRecord extractFeatures(const State& state, int actor,
		const std::unordered_map<int, int>* actorProfile = nullptr, const BeliefInput* belief = nullptr) {
		std::lock_guard<std::mutex> lock(featureMutex());
		FeatureRecord record; extractDense(state, actor, record.dense);
		visitSparseFeatures(state, actor, actorProfile, belief, [&](int id, int relation, int q8) {
			record.sparse.push_back({ id, (std::int16_t)relation, (std::int16_t)std::clamp(q8, -32768, 32767) });
		});
		std::sort(record.sparse.begin(), record.sparse.end(), [](const SparseInput& a, const SparseInput& b) {
			return a.relation != b.relation ? a.relation < b.relation : a.cardId < b.cardId;
		});
		return record;
	}

	long long evaluate(const State& state, int actor,
		const std::unordered_map<int, int>* actorProfile = nullptr, const BeliefInput* belief = nullptr) const {
		std::lock_guard<std::mutex> lock(featureMutex());
		std::array<std::int16_t, DenseCount> dense{}; extractDense(state, actor, dense);
		std::array<std::int32_t, HiddenCount> accumulator{};
		for (int h = 0; h < HiddenCount; ++h) accumulator[h] = hiddenBias[h];
		for (int feature = 0; feature < DenseCount; ++feature) for (int h = 0; h < HiddenCount; ++h)
			saturatingAdd(accumulator[h], (long long)denseWeight[(size_t)h * DenseCount + feature] * dense[feature]);
		visitSparseFeatures(state, actor, actorProfile, belief, [&](int id, int relation, int q8) {
			addSparse(id, relation, q8, accumulator);
		});
		return finish(accumulator);
	}

	long long evaluate(const FeatureRecord& features) const {
		std::array<std::int32_t, HiddenCount> accumulator{};
		for (int h = 0; h < HiddenCount; ++h) accumulator[h] = hiddenBias[h];
		for (int feature = 0; feature < DenseCount; ++feature) for (int h = 0; h < HiddenCount; ++h)
			saturatingAdd(accumulator[h], (long long)denseWeight[(size_t)h * DenseCount + feature] * features.dense[feature]);
		for (const SparseInput& input : features.sparse)
			addSparse(input.cardId, input.relation, input.q8, accumulator);
		return finish(accumulator);
	}

private:
	static std::mutex& featureMutex() { static std::mutex value; return value; }
	long long finish(const std::array<std::int32_t, HiddenCount>& accumulator) const {
		long long output = outputBias;
		for (int h = 0; h < HiddenCount; ++h) {
			long long activation = std::clamp<long long>(accumulator[h], 0, 127LL * WeightScale);
			output += (long long)outputWeight[h] * activation;
		}
		constexpr long long divisor = (long long)WeightScale * WeightScale;
		auto scaled = [&](long long magnitude) {
			long long quotient = magnitude / divisor, remainder = magnitude % divisor;
			return quotient * ScoreScale + (remainder * ScoreScale + divisor / 2) / divisor;
		};
		long long score = output >= 0 ? scaled(output) : -scaled(-output);
		return std::clamp(score, (long long)-NonTerminalLimit, (long long)NonTerminalLimit);
	}
#pragma pack(push, 1)
	struct Header {
		char magic[8];
		std::uint32_t version, denseCount, hiddenCount, relationCount, cardCount;
		std::uint32_t weightScale, beliefScale, scoreScale, schemaVersion;
		std::uint64_t cardChecksum;
		std::uint8_t datasetHash[32];
	};
#pragma pack(pop)
	enum Relation : int {
		OwnHand, OwnActive, OppActive, OwnBench, OppBench, OwnDiscard, OppDiscard,
		OwnStadium, OppStadium, OwnActiveEnergy, OppActiveEnergy, OwnBenchEnergy, OppBenchEnergy,
		OwnActiveTool, OppActiveTool, OwnBenchTool, OppBenchTool,
		OwnActiveEvolution, OppActiveEvolution, OwnBenchEvolution, OppBenchEvolution,
		OwnHiddenPool, OwnDeckBelief, OwnPrizeBelief, OwnAttack, OppAttack,
		OwnPokemonHp, OppPokemonHp, OwnPersistentState, OppPersistentState
	};
	std::vector<std::int32_t> cardIds;
	std::vector<int> cardIndex;
	std::vector<std::int16_t> denseWeight, sparseWeight, outputWeight;
	std::vector<std::int32_t> hiddenBias;
	std::int64_t outputBias = 0;
	std::uint64_t modelHashValue = 0;
	bool loaded = false;
	std::string modelPath;

	static std::uint64_t fnv1a(const void* data, size_t count) {
		const auto* bytes = static_cast<const std::uint8_t*>(data); std::uint64_t value = 1469598103934665603ULL;
		for (size_t i = 0; i < count; ++i) { value ^= bytes[i]; value *= 1099511628211ULL; } return value;
	}
	static std::uint64_t checksumCards(const std::vector<std::int32_t>& ids) {
		return fnv1a(ids.data(), ids.size() * sizeof(ids[0]));
	}
	static int clip(int value) { return std::clamp(value, -127, 127); }
	static void saturatingAdd(std::int32_t& target, long long value) {
		long long sum = (long long)target + value;
		target = (std::int32_t)std::clamp(sum, (long long)std::numeric_limits<std::int32_t>::min(),
			(long long)std::numeric_limits<std::int32_t>::max());
	}
	int indexFor(int id) const { return id >= 0 && id < (int)cardIndex.size() ? cardIndex[id] : 0; }
	void addSparse(int cardId, int relation, int q8, std::array<std::int32_t, HiddenCount>& accumulator) const {
		int index = indexFor(cardId); size_t base = ((size_t)relation * cardIds.size() + index) * HiddenCount;
		for (int h = 0; h < HiddenCount; ++h) saturatingAdd(accumulator[h], (long long)sparseWeight[base + h] * q8);
	}
	static int totalDamage(const State& state, const PlayerState& player) {
		int value = 0; for (CardRef ref : player.active) if (!ref.isNull()) value += state.getCard(ref).damage;
		for (CardRef ref : player.bench) if (!ref.isNull()) value += state.getCard(ref).damage; return value;
	}
	static int activeHp(const State& state, const PlayerState& player) {
		return player.active.empty() || player.active[0].isNull() ? 0 : state.getHp(state.getCard(player.active[0]));
	}
	static int popcount64(unsigned long long value) { return (int)std::popcount(value); }
	template<class Callback>
	static void visitSparseFeatures(const State& state, int actor,
		const std::unordered_map<int, int>* actorProfile, const BeliefInput* belief, Callback&& callback) {
		auto addRef = [&](CardRef ref, int relation, int q8 = BeliefScale) {
			if (!ref.isNull()) callback(state.getCard(ref).cardId, relation, q8);
		};
		const PlayerState& me = state.players[actor]; const PlayerState& opp = state.players[1 - actor];
		for (CardRef ref : me.hand) addRef(ref, OwnHand);
		for (CardRef ref : me.active) addRef(ref, OwnActive);
		for (CardRef ref : opp.active) addRef(ref, OppActive);
		for (CardRef ref : me.bench) addRef(ref, OwnBench);
		for (CardRef ref : opp.bench) addRef(ref, OppBench);
		for (CardRef ref : me.trash) addRef(ref, OwnDiscard);
		for (CardRef ref : opp.trash) addRef(ref, OppDiscard);
		for (CardRef ref : state.stadium) if (!ref.isNull())
			addRef(ref, state.getCard(ref).playerIndex == actor ? OwnStadium : OppStadium);
		auto attachedRelation = [&](CardRef ref, const PlayerState& owner, int activeRelation, int benchRelation) {
			const Card& child = state.getCard(ref);
			bool active = !owner.active.empty() && !owner.active[0].isNull()
				&& child.attachMoveCounter == state.getCard(owner.active[0]).moveCounter;
			addRef(ref, active ? activeRelation : benchRelation);
		};
		for (CardRef ref : me.energy) attachedRelation(ref, me, OwnActiveEnergy, OwnBenchEnergy);
		for (CardRef ref : opp.energy) attachedRelation(ref, opp, OppActiveEnergy, OppBenchEnergy);
		for (CardRef ref : me.tool) attachedRelation(ref, me, OwnActiveTool, OwnBenchTool);
		for (CardRef ref : opp.tool) attachedRelation(ref, opp, OppActiveTool, OppBenchTool);
		for (CardRef ref : me.preEvolution) attachedRelation(ref, me, OwnActiveEvolution, OwnBenchEvolution);
		for (CardRef ref : opp.preEvolution) attachedRelation(ref, opp, OppActiveEvolution, OppBenchEvolution);
		auto pokemonFeatures = [&](const PlayerState& owner, bool own) {
			auto visit = [&](CardRef ref) {
				if (ref.isNull()) return;
				const Card& card = state.getCard(ref); int maximum = std::max(1, state.getMaxHp(card));
				callback(card.cardId, own ? OwnPokemonHp : OppPokemonHp,
					std::clamp(state.getHp(card) * BeliefScale / maximum, 0, BeliefScale));
				int persistent = popcount64(card.thisTurn.value[0]) + popcount64(card.nextTurn.value[0])
					+ popcount64(card.continualState[0]);
				callback(card.cardId, own ? OwnPersistentState : OppPersistentState,
					(1 + std::min(15, persistent)) * BeliefScale);
				auto& energies = state.game->energyList; state.getEnergies(card.playerIndex, ref, energies);
				SetAttackEnergy(state, card, energies, true);
				for (const AttackEnergy& attack : state.game->attackEnergyList)
					callback(1'000'000 + attack.attack->attackId, own ? OwnAttack : OppAttack,
						(5 - std::clamp(attack.insufficientEnergy, 0, 4)) * BeliefScale);
			};
			for (CardRef ref : owner.active) visit(ref); for (CardRef ref : owner.bench) visit(ref);
		};
		pokemonFeatures(me, true); pokemonFeatures(opp, false);
		if (actorProfile != nullptr) {
			int hiddenTotal = (int)me.deck.size() + (int)me.prize.size();
			for (const auto& item : *actorProfile) {
				int remaining = item.second;
				for (const Card& card : state.allCard) if (card.cardId == item.first && card.playerIndex == actor
					&& card.area != AreaType::Deck && card.area != AreaType::Prize) remaining--;
				if (remaining <= 0) continue;
				callback(item.first, OwnHiddenPool, remaining * BeliefScale);
				if (belief == nullptr && hiddenTotal > 0) {
					auto expected = [&](int zoneSize) { return (remaining * zoneSize * BeliefScale + hiddenTotal / 2) / hiddenTotal; };
					callback(item.first, OwnDeckBelief, expected((int)me.deck.size()));
					callback(item.first, OwnPrizeBelief, expected((int)me.prize.size()));
				}
			}
		}
		if (belief != nullptr) {
			if (belief->ownDeckQ8 != nullptr) for (const auto& item : *belief->ownDeckQ8)
				if (item.second != 0) callback(item.first, OwnDeckBelief, item.second);
			if (belief->ownPrizeQ8 != nullptr) for (const auto& item : *belief->ownPrizeQ8)
				if (item.second != 0) callback(item.first, OwnPrizeBelief, item.second);
		}
	}
	static void extractDense(const State& state, int actor, std::array<std::int16_t, DenseCount>& x) {
		const PlayerState& me = state.players[actor]; const PlayerState& opp = state.players[1 - actor];
		auto set = [&](int at, int value) { x[at] = (std::int16_t)clip(value); };
		set(0, ((int)opp.prize.size() - (int)me.prize.size()) * 16); set(1, me.prize.size() * 12); set(2, opp.prize.size() * 12);
		set(3, ((int)me.hand.size() - (int)opp.hand.size()) * 4); set(4, me.hand.size() * 4); set(5, opp.hand.size() * 4);
		set(6, ((int)me.deck.size() - (int)opp.deck.size()) * 2); set(7, me.deck.size() * 2); set(8, opp.deck.size() * 2);
		set(9, ((int)me.active.size() + (int)me.bench.size() - (int)opp.active.size() - (int)opp.bench.size()) * 12);
		set(10, (activeHp(state, me) - activeHp(state, opp)) / 10);
		set(11, (totalDamage(state, opp) - totalDamage(state, me)) / 10);
		set(12, ((int)me.energy.size() - (int)opp.energy.size()) * 6); set(13, ((int)me.tool.size() - (int)opp.tool.size()) * 8);
		set(14, (state.benchCapacity(actor) - state.benchCapacity(1 - actor)) * 12); set(15, state.turn);
		set(16, state.firstPlayer == actor ? 16 : -16); set(17, state.activePlayerIndex() == actor ? 16 : -16);
		set(18, -std::max(0, 6 - (int)me.deck.size()) * 12); set(19, std::max(0, 6 - (int)opp.deck.size()) * 12);
		set(20, me.isPoisoned() ? -16 : 0); set(21, opp.isPoisoned() ? 16 : 0);
		set(22, me.isBurned() ? -12 : 0); set(23, opp.isBurned() ? 12 : 0);
		set(24, me.badStatus == BadStatusType::None ? 0 : -16); set(25, opp.badStatus == BadStatusType::None ? 0 : 16);
		set(26, state.supporterPlayed ? -12 : 12); set(27, state.energyPlayed ? -10 : 10); set(28, state.retreated ? -8 : 8);
		set(29, popcount64(me.thisTurn.value) * -8); set(30, popcount64(opp.thisTurn.value) * 8);
		set(31, popcount64(me.nextTurn.value) * -8); set(32, popcount64(opp.nextTurn.value) * 8);
		set(33, popcount64(me.continualState) * -4); set(34, popcount64(opp.continualState) * 4);
		set(35, me.active.empty() || me.active[0].isNull() ? 0 : -state.getCard(me.active[0]).getMaster().retreatCost * 8);
		set(36, opp.active.empty() || opp.active[0].isNull() ? 0 : state.getCard(opp.active[0]).getMaster().retreatCost * 8);
		set(37, me.bench.empty() ? -16 : 16); set(38, opp.bench.empty() ? 16 : -16); set(39, 32);
	}
};
