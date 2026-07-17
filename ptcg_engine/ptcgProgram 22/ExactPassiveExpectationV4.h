// SPDX-FileCopyrightText: © Pokémon/Nintendo/Creatures/GAME FREAK TM, ®, and character names are trademarks of Nintendo.
// SPDX-License-Identifier: LicenseRef-PTCG-ABC-Competition-Use-Only
#pragma once

#include "ExactBigRational.h"
#include "ExactPassivePayloadV4.h"

#include <stdexcept>
#include <utility>
#include <vector>

// Exact hypergeometric expectations for Passive Residual integration.
// Floating point is forbidden; all mass uses ExactWeight / ExactBigRational.
namespace ExactPassiveExpectationV4 {

inline ExactWeight Choose(int n, int k) {
	if (k < 0 || n < 0 || k > n) return ExactWeight();
	if (k == 0 || k == n) return ExactWeight(1);
	if (k > n - k) k = n - k;
	ExactWeight result(1);
	for (int i = 1; i <= k; ++i) {
		result = ExactWeight::multiply(result, ExactWeight((unsigned long long)(n - k + i)));
		auto div = ExactWeight::divideRemainder(result, ExactWeight((unsigned long long)i));
		if (!div.second.zero()) throw std::runtime_error("choose not divisible");
		result = div.first;
	}
	return result;
}

inline ExactBigRational Ratio(ExactWeight numerator, ExactWeight denominator) {
	if (denominator.zero()) throw std::runtime_error("zero denominator");
	ExactBigRational value(0, 1);
	value.numerator = ExactBigSigned((long long)0);
	if (!numerator.zero()) {
		// Build n/d via scale from 1 * num / den.
		value = ExactBigRational(1, 1);
		value.scale(numerator, denominator);
	}
	return value;
}

inline ExactBigRational RatioSigned(long long signedNumerator, ExactWeight denominator) {
	if (denominator.zero()) throw std::runtime_error("zero denominator");
	if (signedNumerator == 0) return ExactBigRational(0, 1);
	ExactBigRational value(signedNumerator < 0 ? -1 : 1, 1);
	ExactWeight mag((unsigned long long)(signedNumerator < 0 ? -signedNumerator : signedNumerator));
	value.scale(mag, denominator);
	return value;
}

// E[X_i] = k * N_i / N
inline ExactBigRational ExpectedCount(int poolSize, int take, int cardCopies) {
	if (poolSize <= 0 || take <= 0 || cardCopies <= 0) return ExactBigRational(0, 1);
	ExactWeight num = ExactWeight::multiply(ExactWeight((unsigned long long)take),
		ExactWeight((unsigned long long)cardCopies));
	return Ratio(std::move(num), ExactWeight((unsigned long long)poolSize));
}

// E[X_i X_j] = k(k-1) N_i N_j / (N(N-1)) for i != j
inline ExactBigRational ExpectedProductDistinct(int poolSize, int take, int copiesI, int copiesJ) {
	if (poolSize <= 1 || take <= 1 || copiesI <= 0 || copiesJ <= 0) return ExactBigRational(0, 1);
	ExactWeight num = ExactWeight::multiply(ExactWeight((unsigned long long)take),
		ExactWeight((unsigned long long)(take - 1)));
	num = ExactWeight::multiply(num, ExactWeight((unsigned long long)copiesI));
	num = ExactWeight::multiply(num, ExactWeight((unsigned long long)copiesJ));
	ExactWeight den = ExactWeight::multiply(ExactWeight((unsigned long long)poolSize),
		ExactWeight((unsigned long long)(poolSize - 1)));
	return Ratio(std::move(num), std::move(den));
}

// E[C(X_i,2)] = C(k,2) * C(N_i,2) / C(N,2)
inline ExactBigRational ExpectedChoose2(int poolSize, int take, int copiesI) {
	if (poolSize <= 1 || take <= 1 || copiesI <= 1) return ExactBigRational(0, 1);
	ExactWeight num = ExactWeight::multiply(Choose(take, 2), Choose(copiesI, 2));
	ExactWeight den = Choose(poolSize, 2);
	return Ratio(std::move(num), std::move(den));
}

struct MixedDrawTerm {
	int activeTake = 0;
	int passiveTake = 0;
	ExactWeight weight;
};

inline std::vector<MixedDrawTerm> EnumerateActivePassiveTakes(
	int activePool, int passivePool, int take) {
	std::vector<MixedDrawTerm> terms;
	const int pool = activePool + passivePool;
	if (pool <= 0 || take <= 0 || take > pool) return terms;
	for (int activeTake = 0; activeTake <= take; ++activeTake) {
		const int passiveTake = take - activeTake;
		if (activeTake > activePool || passiveTake > passivePool) continue;
		ExactWeight w = ExactWeight::multiply(Choose(activePool, activeTake), Choose(passivePool, passiveTake));
		if (w.zero()) continue;
		MixedDrawTerm term;
		term.activeTake = activeTake;
		term.passiveTake = passiveTake;
		term.weight = std::move(w);
		terms.push_back(std::move(term));
	}
	return terms;
}

inline ExactBigRational ScaleRational(ExactBigRational value, long long coeff) {
	if (coeff == 0 || value.numerator.sign == 0) return ExactBigRational(0, 1);
	const unsigned long long mag = (unsigned long long)(coeff < 0 ? -coeff : coeff);
	value.scale(ExactWeight(mag), ExactWeight(1));
	if (coeff < 0) value.numerator.sign = -value.numerator.sign;
	return value;
}

// Expected passive residual for drawing `passiveTake` from a passive-only pool.
// cardValueById: (cardId -> r_i), pairs: sparse gamma. Values may be signed.
inline ExactBigRational ExpectedPassiveResidual(
	int passivePool, int passiveTake,
	const std::vector<std::pair<int, long long>>& cardValueById,
	const std::vector<ExactPassivePairWeightV4>& pairs,
	const std::vector<std::pair<int, int>>& passiveCopies) {
	ExactBigRational total(0, 1);
	if (passivePool <= 0 || passiveTake <= 0) return total;
	auto copiesOf = [&](int cardId) {
		for (const auto& item : passiveCopies) if (item.first == cardId) return item.second;
		return 0;
	};
	auto valueOf = [&](int cardId) -> long long {
		for (const auto& item : cardValueById) if (item.first == cardId) return item.second;
		return 0;
	};
	for (const auto& item : passiveCopies) {
		long long ri = valueOf(item.first);
		if (ri == 0) continue;
		total = ExactBigRational::add(total,
			ScaleRational(ExpectedCount(passivePool, passiveTake, item.second), ri));
	}
	for (const auto& pair : pairs) {
		if (pair.weight == 0) continue;
		int na = copiesOf(pair.cardA), nb = copiesOf(pair.cardB);
		ExactBigRational expected = (pair.cardA == pair.cardB)
			? ExpectedChoose2(passivePool, passiveTake, na)
			: ExpectedProductDistinct(passivePool, passiveTake, na, nb);
		total = ExactBigRational::add(total, ScaleRational(std::move(expected), pair.weight));
	}
	return total;
}

} // namespace ExactPassiveExpectationV4
