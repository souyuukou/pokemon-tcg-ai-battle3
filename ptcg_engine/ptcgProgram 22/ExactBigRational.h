#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>

class ExactBigUnsigned {
public:
	static constexpr std::uint32_t Base = 1'000'000'000U;
	std::vector<std::uint32_t> digit;

	ExactBigUnsigned(unsigned long long value = 0) {
		while (value) { digit.push_back((std::uint32_t)(value % Base)); value /= Base; }
	}
	bool zero() const { return digit.empty(); }
	void trim() { while (!digit.empty() && digit.back() == 0) digit.pop_back(); }
	static int compare(const ExactBigUnsigned& a, const ExactBigUnsigned& b) {
		if (a.digit.size() != b.digit.size()) return a.digit.size() < b.digit.size() ? -1 : 1;
		for (size_t i = a.digit.size(); i-- > 0;) if (a.digit[i] != b.digit[i]) return a.digit[i] < b.digit[i] ? -1 : 1;
		return 0;
	}
	static ExactBigUnsigned add(const ExactBigUnsigned& a, const ExactBigUnsigned& b) {
		ExactBigUnsigned out; out.digit.resize(std::max(a.digit.size(), b.digit.size()));
		std::uint64_t carry = 0;
		for (size_t i = 0; i < out.digit.size(); ++i) {
			std::uint64_t value = carry + (i < a.digit.size() ? a.digit[i] : 0) + (i < b.digit.size() ? b.digit[i] : 0);
			out.digit[i] = (std::uint32_t)(value % Base); carry = value / Base;
		}
		if (carry) out.digit.push_back((std::uint32_t)carry); return out;
	}
	static ExactBigUnsigned subtract(const ExactBigUnsigned& a, const ExactBigUnsigned& b) {
		ExactBigUnsigned out; out.digit.resize(a.digit.size()); std::int64_t borrow = 0;
		for (size_t i = 0; i < a.digit.size(); ++i) {
			std::int64_t value = (std::int64_t)a.digit[i] - (i < b.digit.size() ? b.digit[i] : 0) - borrow;
			if (value < 0) { value += Base; borrow = 1; } else borrow = 0;
			out.digit[i] = (std::uint32_t)value;
		}
		out.trim(); return out;
	}
	static ExactBigUnsigned multiply(const ExactBigUnsigned& a, const ExactBigUnsigned& b) {
		if (a.zero() || b.zero()) return {};
		ExactBigUnsigned out; out.digit.assign(a.digit.size() + b.digit.size(), 0);
		for (size_t i = 0; i < a.digit.size(); ++i) {
			std::uint64_t carry = 0;
			for (size_t j = 0; j < b.digit.size(); ++j) {
				std::uint64_t value = out.digit[i + j] + (std::uint64_t)a.digit[i] * b.digit[j] + carry;
				out.digit[i + j] = (std::uint32_t)(value % Base); carry = value / Base;
			}
			size_t at = i + b.digit.size();
			while (carry) {
				if (at == out.digit.size()) out.digit.push_back(0);
				std::uint64_t value = out.digit[at] + carry;
				out.digit[at++] = (std::uint32_t)(value % Base); carry = value / Base;
			}
		}
		out.trim(); return out;
	}
	std::string text() const {
		if (zero()) return "0";
		std::ostringstream out; out << digit.back();
		for (size_t i = digit.size() - 1; i-- > 0;) out << std::setw(9) << std::setfill('0') << digit[i];
		return out.str();
	}
};

struct ExactBigSigned {
	int sign = 0;
	ExactBigUnsigned magnitude;
	ExactBigSigned(long long value = 0) {
		if (value != 0) {
			sign = value < 0 ? -1 : 1;
			unsigned long long mag = value < 0 ? (unsigned long long)(-(value + 1)) + 1 : (unsigned long long)value;
			magnitude = ExactBigUnsigned(mag);
		}
	}
	void multiply(const ExactBigUnsigned& factor) {
		magnitude = ExactBigUnsigned::multiply(magnitude, factor); if (magnitude.zero()) sign = 0;
	}
	static ExactBigSigned add(const ExactBigSigned& a, const ExactBigSigned& b) {
		if (a.sign == 0) return b; if (b.sign == 0) return a;
		ExactBigSigned out;
		if (a.sign == b.sign) { out.sign = a.sign; out.magnitude = ExactBigUnsigned::add(a.magnitude, b.magnitude); }
		else {
			int cmp = ExactBigUnsigned::compare(a.magnitude, b.magnitude);
			if (cmp == 0) return out;
			out.sign = cmp > 0 ? a.sign : b.sign;
			out.magnitude = cmp > 0 ? ExactBigUnsigned::subtract(a.magnitude, b.magnitude)
				: ExactBigUnsigned::subtract(b.magnitude, a.magnitude);
		}
		return out;
	}
	std::string text() const { return sign < 0 ? "-" + magnitude.text() : magnitude.text(); }
};

struct ExactBigRational {
	static constexpr std::array<unsigned, 17> Primes{ 2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59 };
	ExactBigSigned numerator;
	std::array<std::uint16_t, Primes.size()> denominator{};

	ExactBigRational(long long n = 0, unsigned long long d = 1) : numerator(n) { addDenominator(d); }
	void addDenominator(unsigned long long value) {
		for (size_t i = 0; i < Primes.size(); ++i) while (value % Primes[i] == 0) {
			value /= Primes[i]; denominator[i]++;
		}
		// Every probability denominator is composed from counts no greater than
		// DECK_SIZE. Reaching this branch indicates corrupted arithmetic input.
		if (value != 1) throw std::runtime_error("unsupported exact denominator prime");
	}
	static ExactBigUnsigned factorProduct(const std::array<std::uint16_t, Primes.size()>& exponent) {
		ExactBigUnsigned out(1);
		for (size_t i = 0; i < Primes.size(); ++i) {
			ExactBigUnsigned factor(Primes[i]), power(1);
			unsigned n = exponent[i];
			while (n) {
				if (n & 1U) power = ExactBigUnsigned::multiply(power, factor);
				n >>= 1U; if (n) factor = ExactBigUnsigned::multiply(factor, factor);
			}
			out = ExactBigUnsigned::multiply(out, power);
		}
		return out;
	}
	void scale(unsigned long long weight, unsigned long long total) {
		numerator.multiply(ExactBigUnsigned(weight)); addDenominator(total);
	}
	static ExactBigRational add(const ExactBigRational& a, const ExactBigRational& b) {
		ExactBigRational out; out.denominator.fill(0);
		std::array<std::uint16_t, Primes.size()> am{}, bm{};
		for (size_t i = 0; i < Primes.size(); ++i) {
			out.denominator[i] = std::max(a.denominator[i], b.denominator[i]);
			am[i] = out.denominator[i] - a.denominator[i]; bm[i] = out.denominator[i] - b.denominator[i];
		}
		ExactBigSigned an = a.numerator, bn = b.numerator;
		an.multiply(factorProduct(am)); bn.multiply(factorProduct(bm)); out.numerator = ExactBigSigned::add(an, bn); return out;
	}
	static int compare(const ExactBigRational& a, const ExactBigRational& b) {
		if (a.numerator.sign != b.numerator.sign) return a.numerator.sign < b.numerator.sign ? -1 : 1;
		if (a.numerator.sign == 0) return 0;
		std::array<std::uint16_t, Primes.size()> am{}, bm{};
		for (size_t i = 0; i < Primes.size(); ++i) {
			std::uint16_t common = std::max(a.denominator[i], b.denominator[i]);
			am[i] = common - a.denominator[i]; bm[i] = common - b.denominator[i];
		}
		ExactBigUnsigned av = ExactBigUnsigned::multiply(a.numerator.magnitude, factorProduct(am));
		ExactBigUnsigned bv = ExactBigUnsigned::multiply(b.numerator.magnitude, factorProduct(bm));
		int cmp = ExactBigUnsigned::compare(av, bv); return a.numerator.sign < 0 ? -cmp : cmp;
	}
	std::string denominatorText() const { return factorProduct(denominator).text(); }
};
