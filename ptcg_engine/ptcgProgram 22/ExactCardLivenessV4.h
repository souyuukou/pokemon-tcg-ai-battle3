// SPDX-FileCopyrightText: © Pokémon/Nintendo/Creatures/GAME FREAK TM, ®, and character names are trademarks of Nintendo.
// SPDX-License-Identifier: LicenseRef-PTCG-ABC-Competition-Use-Only
#pragma once

#include "Card.h"
#include "Skill.h"
#include "State.h"
#include "Types.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Conservative turn-remainder liveness for V4 Passive Residual.
// Passive is allowed only when every PassiveProofV4 flag is proven true.
// "Currently unplayable" is NEVER sufficient by itself.
namespace ExactCardLivenessV4 {

static constexpr int LivenessSchemaVersion = 2;

enum class CardLiveness : unsigned char { Active = 0, Passive = 1, Unknown = 2 };

enum class CardObservationKind : unsigned char {
	None = 0,
	CountOnly = 1,
	StaticPredicate = 2,
	CardIdentity = 3,
	CardOrder = 4,
	Unknown = 5
};

enum Reason : std::uint64_t {
	CurrentlyPlayable = 1ull << 0,
	MayBecomePlayable = 1ull << 1,
	ReferencedByReachableTarget = 1ull << 2,
	MayMoveZone = 1ull << 3,
	MayChangeLegality = 1ull << 4,
	MayTriggerEffect = 1ull << 5,
	MayBeDiscarded = 1ull << 6,
	MayBeReturnedToDeck = 1ull << 7,
	MayBeAttached = 1ull << 8,
	MayBeEvolved = 1ull << 9,
	MayBeUsedAsCost = 1ull << 10,
	UnknownEffect = 1ull << 11,
	UnsupportedTarget = 1ull << 12,
	InReachableSet = 1ull << 13,
	PassiveProven = 1ull << 14,
	EnergyOnceConsumed = 1ull << 15,
	SupporterLocked = 1ull << 16,
	StadiumLocked = 1ull << 17,
	EvolutionBlocked = 1ull << 18,
	HandTargetObserved = 1ull << 19,
	DeckIdentityObserved = 1ull << 20,
	ProofIncomplete = 1ull << 21,
	ClosureRequired = 1ull << 22,
};

struct PassiveProofV4 {
	bool handActionInvariant = false;
	bool handTargetInvariant = false;
	bool zoneMovementInvariant = false;
	bool deckRemovalInvariant = false;
	bool semanticInvariant = false;
	bool actionIndependentResidual = false;

	std::uint64_t reachableOperatorHash = 0;
	std::uint64_t partitionSchemaHash = 0;
	std::uint64_t proofHash = 0;

	bool allProven() const {
		return handActionInvariant && handTargetInvariant && zoneMovementInvariant
			&& deckRemovalInvariant && semanticInvariant && actionIndependentResidual;
	}
};

struct OperatorFootprint {
	int operatorCardId = 0;
	EffectType effectType = EffectType::NoEffect;
	CardObservationKind observation = CardObservationKind::Unknown;
	bool mayTargetHand = false;
	bool mayTargetDeck = false;
	bool mayDiscardHand = false;
	bool mayReturnHandToDeck = false;
	bool mayCountHandByType = false;
	bool maySearchDeckByIdentity = false;
	bool mayMoveCardZones = false;
};

struct OperatorClosure {
	std::unordered_set<int> reachableCards;
	std::unordered_set<int> reachableEffectTypes; // cast EffectType
	std::vector<OperatorFootprint> footprints;
	std::uint64_t reachableOperatorHash = 0;
	std::uint64_t partitionSchemaHash = 0;
	bool complete = false;
};

struct CardLivenessResult {
	CardLiveness liveness = CardLiveness::Unknown;
	std::uint64_t reasonMask = 0;
	PassiveProofV4 proof{};
};

inline CardObservationKind ObservationKindForEffect(EffectType type) {
	switch (type) {
	case EffectType::NoEffect:
	case EffectType::AttackDamage:
	case EffectType::AttackDamageChange:
	case EffectType::Coin:
	case EffectType::NotMove:
		return CardObservationKind::None;
	case EffectType::Draw:
	case EffectType::DrawTargetCount:
	case EffectType::DrawPrizeCount:
	case EffectType::DrawUntil:
	case EffectType::DrawUntilPsychic:
	case EffectType::DrawMirror:
		return CardObservationKind::CountOnly;
	case EffectType::LookDeck:
	case EffectType::LookDeckReverse:
	case EffectType::LookDeckBottom:
	case EffectType::LookAndReturn:
	case EffectType::SwitchDeck:
	case EffectType::DeckToTrash:
	case EffectType::DeckBottomToTrash:
	case EffectType::ToDeck:
	case EffectType::ToDeckAndShuffle:
	case EffectType::ToHand:
	case EffectType::ToTrash:
	case EffectType::SelectCard:
	case EffectType::ForEach:
	case EffectType::SelectEvolvesFrom:
	case EffectType::SelectEvolvesTo:
	case EffectType::SelectAttachFrom:
	case EffectType::SelectAttachTo:
		return CardObservationKind::CardIdentity;
	case EffectType::ToDeckBottomClose:
	case EffectType::ToDeckBottomReverse:
		return CardObservationKind::CardOrder;
	default:
		// Unclassified EffectType must fail closed — never Passive.
		return CardObservationKind::Unknown;
	}
}

inline bool EffectKindBlocksPassive(CardObservationKind kind) {
	return kind == CardObservationKind::CardIdentity
		|| kind == CardObservationKind::CardOrder
		|| kind == CardObservationKind::Unknown;
}

inline bool TargetIncludesHand(const Target& target) {
	for (AreaType area : target.areas)
		if (area == AreaType::Hand) return true;
	return false;
}

inline bool TargetIncludesDeck(const Target& target) {
	for (AreaType area : target.areas)
		if (area == AreaType::Deck) return true;
	return false;
}

inline std::uint64_t MixHash(std::uint64_t hash, std::uint64_t value) {
	hash ^= value;
	hash *= 1099511628211ULL;
	return hash;
}

// Build operator footprints from reachable card IDs (skills / play effects).
inline OperatorClosure BuildOperatorClosure(
	const std::unordered_set<int>& reachableCards,
	std::uint64_t partitionSchemaHash = 0) {
	OperatorClosure closure;
	closure.reachableCards = reachableCards;
	closure.partitionSchemaHash = partitionSchemaHash;
	closure.reachableOperatorHash = 1469598103934665603ULL;
	closure.reachableOperatorHash = MixHash(closure.reachableOperatorHash, LivenessSchemaVersion);
	closure.reachableOperatorHash = MixHash(closure.reachableOperatorHash, partitionSchemaHash);
	std::vector<int> sorted(reachableCards.begin(), reachableCards.end());
	std::sort(sorted.begin(), sorted.end());
	for (int cardId : sorted) {
		closure.reachableOperatorHash = MixHash(closure.reachableOperatorHash, (std::uint64_t)cardId);
		const CardMaster* master = FindCardMaster(cardId);
		if (master == nullptr) {
			OperatorFootprint unknown;
			unknown.operatorCardId = cardId;
			unknown.observation = CardObservationKind::Unknown;
			unknown.mayTargetHand = true;
			unknown.mayTargetDeck = true;
			unknown.mayDiscardHand = true;
			unknown.mayReturnHandToDeck = true;
			unknown.maySearchDeckByIdentity = true;
			unknown.mayMoveCardZones = true;
			closure.footprints.push_back(unknown);
			closure.complete = false;
			continue;
		}
		auto considerSkill = [&](const Skill* skill) {
			if (skill == nullptr) return;
			for (const Effect& effect : skill->effects) {
				if (effect.isCondition) continue;
				OperatorFootprint fp;
				fp.operatorCardId = cardId;
				fp.effectType = effect.effectType;
				fp.observation = ObservationKindForEffect(effect.effectType);
				fp.mayTargetHand = TargetIncludesHand(effect.target);
				fp.mayTargetDeck = TargetIncludesDeck(effect.target);
				fp.mayMoveCardZones = effect.effectType == EffectType::ToTrash
					|| effect.effectType == EffectType::ToDeck
					|| effect.effectType == EffectType::ToDeckAndShuffle
					|| effect.effectType == EffectType::ToHand
					|| effect.effectType == EffectType::DeckToTrash
					|| effect.effectType == EffectType::SelectCard
					|| effect.effectType == EffectType::ForEach;
				fp.mayDiscardHand = fp.mayTargetHand
					&& (effect.effectType == EffectType::ToTrash
						|| effect.effectType == EffectType::SelectCard
						|| effect.effectType == EffectType::ForEach);
				fp.mayReturnHandToDeck = fp.mayTargetHand
					&& (effect.effectType == EffectType::ToDeck
						|| effect.effectType == EffectType::ToDeckAndShuffle
						|| effect.effectType == EffectType::ToDeckBottomClose
						|| effect.effectType == EffectType::ToDeckBottomReverse);
				fp.mayCountHandByType = fp.mayTargetHand
					&& !effect.target.conditions.empty();
				fp.maySearchDeckByIdentity = fp.mayTargetDeck
					&& EffectKindBlocksPassive(fp.observation);
				closure.reachableEffectTypes.insert((int)effect.effectType);
				closure.footprints.push_back(fp);
				if (fp.observation == CardObservationKind::Unknown)
					closure.complete = false;
			}
		};
		considerSkill(master->play);
		for (const Skill* skill : master->getSkills()) considerSkill(skill);
	}
	if (closure.complete == false && !closure.footprints.empty()) {
		// complete stays false if any unknown; otherwise mark true when all classified.
		bool anyUnknown = false;
		for (const auto& fp : closure.footprints)
			if (fp.observation == CardObservationKind::Unknown) anyUnknown = true;
		closure.complete = !anyUnknown;
	} else if (closure.footprints.empty()) {
		closure.complete = true;
	}
	return closure;
}

// Classify with mandatory operator closure. Passing a null/incomplete closure
// forces Unknown→Active (never Passive).
inline CardLivenessResult ClassifyCardId(
	const State& state, int actor, int cardId,
	const OperatorClosure& closure) {
	CardLivenessResult result;
	result.proof.reachableOperatorHash = closure.reachableOperatorHash;
	result.proof.partitionSchemaHash = closure.partitionSchemaHash;
	result.proof.actionIndependentResidual = true; // V4.0 context-free residual only

	const CardMaster* master = FindCardMaster(cardId);
	if (master == nullptr) {
		result.liveness = CardLiveness::Unknown;
		result.reasonMask |= UnknownEffect | UnsupportedTarget | ClosureRequired;
		return result;
	}
	if (!closure.complete) {
		result.liveness = CardLiveness::Unknown;
		result.reasonMask |= ProofIncomplete | ClosureRequired;
		return result;
	}

	// Operator-reachable identities remain Active.
	if (closure.reachableCards.contains(cardId)) {
		result.liveness = CardLiveness::Active;
		result.reasonMask |= InReachableSet | MayBecomePlayable;
		result.proof.handActionInvariant = false;
		return result;
	}

	bool handTargetOk = true;
	bool zoneOk = true;
	bool deckRemovalOk = true;
	bool semanticOk = true;
	for (const OperatorFootprint& fp : closure.footprints) {
		if (fp.observation == CardObservationKind::Unknown) {
			handTargetOk = zoneOk = deckRemovalOk = semanticOk = false;
			result.reasonMask |= UnknownEffect;
			break;
		}
		if (fp.mayTargetHand && EffectKindBlocksPassive(fp.observation)) {
			handTargetOk = false;
			result.reasonMask |= HandTargetObserved | ReferencedByReachableTarget;
		}
		if (fp.mayDiscardHand || fp.mayReturnHandToDeck || fp.mayCountHandByType) {
			handTargetOk = false;
			result.reasonMask |= MayBeDiscarded | MayBeUsedAsCost | HandTargetObserved;
		}
		if (fp.mayMoveCardZones && fp.mayTargetHand) {
			zoneOk = false;
			result.reasonMask |= MayMoveZone;
		}
		if (fp.maySearchDeckByIdentity || (fp.mayTargetDeck && EffectKindBlocksPassive(fp.observation))) {
			deckRemovalOk = false;
			semanticOk = false;
			result.reasonMask |= DeckIdentityObserved;
		}
	}

	const PlayerState& player = state.players[actor];
	bool handActionOk = true;

	if (IsEnergy(master->cardType)) {
		if (!(state.energyPlayed
			|| (player.thisTurn.cannotPlaySpecialEnergy
				&& master->cardType == CardType::SpecialEnergy))) {
			handActionOk = false;
			result.reasonMask |= CurrentlyPlayable | MayBeAttached;
		} else {
			result.reasonMask |= EnergyOnceConsumed;
		}
	} else if (master->cardType == CardType::Supporter) {
		if (!(state.supporterPlayed || player.thisTurn.cannotPlaySupporter
			|| (state.turn <= 1 && !master->canPlayFirstTurn))) {
			handActionOk = false;
			result.reasonMask |= CurrentlyPlayable | MayTriggerEffect;
		} else {
			result.reasonMask |= SupporterLocked;
		}
	} else if (master->cardType == CardType::Stadium) {
		if (!(state.stadiumPlayed || player.cannotPlayStadium || player.thisTurn.cannotPlayStadium)) {
			handActionOk = false;
			result.reasonMask |= CurrentlyPlayable;
		} else {
			result.reasonMask |= StadiumLocked;
		}
	} else if (master->cardType == CardType::Pokemon) {
		if (master->evolutionType == EvolutionType::Basic) {
			handActionOk = false;
			result.reasonMask |= CurrentlyPlayable | MayChangeLegality;
		} else if (state.turn <= 2) {
			result.reasonMask |= EvolutionBlocked;
		} else {
			handActionOk = false;
			result.reasonMask |= MayBecomePlayable | MayBeEvolved;
		}
	} else {
		// Items / Tools: only handActionOk if proven unplayable this turn AND
		// no reachable operator observes/moves the card. Default fail closed.
		if (!(player.thisTurn.cannotPlayItem || player.cannotPlayItem)) {
			bool turnLocked = false;
			if (master->play != nullptr) {
				for (const Effect& effect : master->play->effects) {
					if (!effect.isCondition) break;
					if (effect.conditionType != ConditionType::Turn) continue;
					const int need = effect.values[0];
					if (effect.comparatorType == ComparatorType::GreaterEqual && state.turn < need) turnLocked = true;
					if (effect.comparatorType == ComparatorType::Greater && state.turn <= need) turnLocked = true;
					if (effect.comparatorType == ComparatorType::Equal && state.turn != need) turnLocked = true;
				}
			}
			if (!turnLocked) {
				handActionOk = false;
				result.reasonMask |= CurrentlyPlayable | MayTriggerEffect | MayMoveZone;
			}
		}
	}

	result.proof.handActionInvariant = handActionOk;
	result.proof.handTargetInvariant = handTargetOk;
	result.proof.zoneMovementInvariant = zoneOk;
	result.proof.deckRemovalInvariant = deckRemovalOk;
	result.proof.semanticInvariant = semanticOk;
	result.proof.actionIndependentResidual = true;

	std::uint64_t proofHash = 1469598103934665603ULL;
	proofHash = MixHash(proofHash, LivenessSchemaVersion);
	proofHash = MixHash(proofHash, (std::uint64_t)cardId);
	proofHash = MixHash(proofHash, closure.reachableOperatorHash);
	proofHash = MixHash(proofHash, handActionOk ? 1ull : 0ull);
	proofHash = MixHash(proofHash, handTargetOk ? 1ull : 0ull);
	proofHash = MixHash(proofHash, zoneOk ? 1ull : 0ull);
	proofHash = MixHash(proofHash, deckRemovalOk ? 1ull : 0ull);
	proofHash = MixHash(proofHash, semanticOk ? 1ull : 0ull);
	result.proof.proofHash = proofHash;

	if (result.proof.allProven()) {
		result.liveness = CardLiveness::Passive;
		result.reasonMask |= PassiveProven;
		return result;
	}
	if (!handActionOk || !handTargetOk || !zoneOk || !deckRemovalOk || !semanticOk) {
		result.liveness = CardLiveness::Active;
		result.reasonMask |= ProofIncomplete;
		return result;
	}
	result.liveness = CardLiveness::Unknown;
	result.reasonMask |= ProofIncomplete;
	return result;
}

struct HandSplit {
	std::vector<std::pair<int, int>> activeCounts;
	std::vector<std::pair<int, int>> passiveCounts;
	int unknownCount = 0;
	std::uint64_t proofHash = 0;
};

inline HandSplit SplitHandCounts(const State& state, int actor,
	const std::unordered_map<int, int>& handCounts,
	const OperatorClosure& closure) {
	HandSplit split;
	std::uint64_t hash = 1469598103934665603ULL;
	hash = MixHash(hash, LivenessSchemaVersion);
	hash = MixHash(hash, closure.reachableOperatorHash);
	for (const auto& item : handCounts) {
		if (item.second <= 0) continue;
		CardLivenessResult classified = ClassifyCardId(state, actor, item.first, closure);
		CardLiveness live = classified.liveness;
		if (live == CardLiveness::Unknown) {
			live = CardLiveness::Active;
			++split.unknownCount;
		}
		hash = MixHash(hash, (std::uint64_t)item.first);
		hash = MixHash(hash, (std::uint64_t)item.second);
		hash = MixHash(hash, (std::uint64_t)live);
		hash = MixHash(hash, classified.reasonMask);
		hash = MixHash(hash, classified.proof.proofHash);
		if (live == CardLiveness::Passive)
			split.passiveCounts.push_back(item);
		else
			split.activeCounts.push_back(item);
	}
	std::sort(split.activeCounts.begin(), split.activeCounts.end());
	std::sort(split.passiveCounts.begin(), split.passiveCounts.end());
	split.proofHash = hash;
	return split;
}

inline const char* LivenessName(CardLiveness live) {
	switch (live) {
	case CardLiveness::Active: return "Active";
	case CardLiveness::Passive: return "Passive";
	default: return "Unknown";
	}
}

} // namespace ExactCardLivenessV4
