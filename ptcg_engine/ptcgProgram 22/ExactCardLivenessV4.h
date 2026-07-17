// SPDX-FileCopyrightText: © Pokémon/Nintendo/Creatures/GAME FREAK TM, ®, and character names are trademarks of Nintendo.
// SPDX-License-Identifier: LicenseRef-PTCG-ABC-Competition-Use-Only
#pragma once

#include "Card.h"
#include "State.h"
#include "Types.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Conservative turn-remainder liveness for V4 Passive Residual.
// Unknown / unsupported effects force Active. Never invent Passive without proof.
namespace ExactCardLivenessV4 {

static constexpr int LivenessSchemaVersion = 1;

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
};

struct CardLivenessResult {
	CardLiveness liveness = CardLiveness::Unknown;
	std::uint64_t reasonMask = 0;
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
		// Unclassified EffectType must fail closed for Passive proofs.
		return CardObservationKind::Unknown;
	}
}

inline bool EffectKindBlocksPassive(CardObservationKind kind) {
	return kind == CardObservationKind::CardIdentity
		|| kind == CardObservationKind::CardOrder
		|| kind == CardObservationKind::Unknown;
}

// Classify a single card identity for the remainder of the actor's turn.
// `reachableIds` comes from turnDependencyPartition reachable set (operator-
// visible / may-act identities). Cards in that set are never Passive.
inline CardLivenessResult ClassifyCardId(const State& state, int actor, int cardId,
	const std::unordered_set<int>* reachableIds = nullptr) {
	CardLivenessResult result;
	const CardMaster* master = FindCardMaster(cardId);
	if (master == nullptr) {
		result.liveness = CardLiveness::Unknown;
		result.reasonMask |= UnknownEffect | UnsupportedTarget;
		return result;
	}
	if (reachableIds != nullptr && reachableIds->contains(cardId)) {
		result.liveness = CardLiveness::Active;
		result.reasonMask |= InReachableSet | MayBecomePlayable;
		return result;
	}

	const PlayerState& player = state.players[actor];
	bool provenPassive = false;

	if (IsEnergy(master->cardType)) {
		if (state.energyPlayed) {
			result.reasonMask |= EnergyOnceConsumed | PassiveProven;
			provenPassive = true;
		} else if (player.thisTurn.cannotPlaySpecialEnergy
			&& master->cardType == CardType::SpecialEnergy) {
			result.reasonMask |= EnergyOnceConsumed | PassiveProven;
			provenPassive = true;
		} else {
			result.liveness = CardLiveness::Active;
			result.reasonMask |= CurrentlyPlayable | MayBeAttached;
			return result;
		}
	} else if (master->cardType == CardType::Supporter) {
		if (state.supporterPlayed || player.thisTurn.cannotPlaySupporter
			|| (state.turn <= 1 && !master->canPlayFirstTurn)) {
			result.reasonMask |= SupporterLocked | PassiveProven;
			provenPassive = true;
		} else {
			result.liveness = CardLiveness::Active;
			result.reasonMask |= CurrentlyPlayable | MayTriggerEffect;
			return result;
		}
	} else if (master->cardType == CardType::Stadium) {
		if (state.stadiumPlayed || player.cannotPlayStadium || player.thisTurn.cannotPlayStadium) {
			result.reasonMask |= StadiumLocked | PassiveProven;
			provenPassive = true;
		} else {
			result.liveness = CardLiveness::Active;
			result.reasonMask |= CurrentlyPlayable;
			return result;
		}
	} else if (master->cardType == CardType::Pokemon) {
		if (master->evolutionType == EvolutionType::Basic) {
			result.liveness = CardLiveness::Active;
			result.reasonMask |= CurrentlyPlayable | MayChangeLegality;
			return result;
		}
		if (state.turn <= 2) {
			result.reasonMask |= EvolutionBlocked | PassiveProven;
			provenPassive = true;
		} else {
			bool hasMatch = false;
			auto consider = [&](CardRef ref) {
				if (ref.isNull() || hasMatch) return;
				const CardMaster& field = state.getCard(ref).getMaster();
				if (master->evolvesFrom == field.name || master->evolvesFrom == field.nameEn)
					hasMatch = true;
			};
			for (CardRef ref : player.active) consider(ref);
			for (CardRef ref : player.bench) consider(ref);
			if (hasMatch) {
				result.liveness = CardLiveness::Active;
				result.reasonMask |= CurrentlyPlayable | MayBeEvolved;
				return result;
			}
			// Bench/search later in the turn may enable evolution — conservative Active
			// unless the card cannot enter play via any remaining Item/search. Items that
			// search Basics are still reachable, so without a full closure we treat Stage
			// cards without a field match as Unknown→Active rather than proven Passive.
			result.liveness = CardLiveness::Active;
			result.reasonMask |= MayBecomePlayable | MayBeEvolved;
			return result;
		}
	} else {
		// Items / Tools / anything else.
		if (player.thisTurn.cannotPlayItem || player.cannotPlayItem) {
			result.reasonMask |= MayChangeLegality | PassiveProven;
			provenPassive = true;
		} else if (master->play != nullptr) {
			bool turnLocked = false;
			for (const Effect& effect : master->play->effects) {
				if (!effect.isCondition) break;
				if (effect.conditionType != ConditionType::Turn) continue;
				const int need = effect.values[0];
				if (effect.comparatorType == ComparatorType::GreaterEqual && state.turn < need) turnLocked = true;
				if (effect.comparatorType == ComparatorType::Greater && state.turn <= need) turnLocked = true;
				if (effect.comparatorType == ComparatorType::Equal && state.turn != need) turnLocked = true;
			}
			if (turnLocked) {
				result.reasonMask |= MayBecomePlayable | PassiveProven;
				provenPassive = true;
			} else {
				result.liveness = CardLiveness::Active;
				result.reasonMask |= CurrentlyPlayable | MayTriggerEffect | MayMoveZone;
				return result;
			}
		} else {
			result.liveness = CardLiveness::Active;
			result.reasonMask |= CurrentlyPlayable | MayTriggerEffect | MayMoveZone;
			return result;
		}
	}

	if (provenPassive) {
		result.liveness = CardLiveness::Passive;
		return result;
	}
	result.liveness = CardLiveness::Unknown;
	result.reasonMask |= UnknownEffect;
	return result;
}

struct HandSplit {
	std::vector<std::pair<int, int>> activeCounts;   // sorted (cardId, count)
	std::vector<std::pair<int, int>> passiveCounts;
	int unknownCount = 0;
	std::uint64_t proofHash = 0;
};

inline HandSplit SplitHandCounts(const State& state, int actor,
	const std::unordered_map<int, int>& handCounts,
	const std::unordered_set<int>* reachableIds = nullptr) {
	HandSplit split;
	std::uint64_t hash = 1469598103934665603ULL;
	auto mix = [&](std::uint64_t value) { hash ^= value; hash *= 1099511628211ULL; };
	mix(LivenessSchemaVersion);
	for (const auto& item : handCounts) {
		if (item.second <= 0) continue;
		CardLivenessResult classified = ClassifyCardId(state, actor, item.first, reachableIds);
		CardLiveness live = classified.liveness;
		if (live == CardLiveness::Unknown) {
			live = CardLiveness::Active;
			++split.unknownCount;
		}
		mix((std::uint64_t)item.first);
		mix((std::uint64_t)item.second);
		mix((std::uint64_t)live);
		mix(classified.reasonMask);
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
