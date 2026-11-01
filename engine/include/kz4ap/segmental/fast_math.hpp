// The level filter's and the evidence's exp, log and ln I0: about 180 exp and 150 ln I0 per envelope sample (segmental/evidence.cpp), each result a term of a sum
// normalized at once or held in a float. Plain code and tables: the same formulas on every compiler; the tables `log_center` and `i0_rest` are filled at start from the C library and differ in low bits between MSVC and glibc (real30 and live2 read the same text on both). The errors are derived (bounds below) and checked by
// a test (SegmentalFastMath):
//   exp_neg: relative 6e-10 from the cubic's truncation, r^4 / 24 at |r| <= ln 2 / 64; below -40 nats it reads 0, which is under the rounding of a sum holding a term of
//            1 (e^-40 = 4e-18 against 2^-53 = 1.1e-16).
//   log:     absolute 4e-12, u^4 / 4 at |u| <= 1 / 512.
//   ln_i0:   absolute 1.6e-5 nats below 32, h^2 / 8 x |f''| with h = 1 / 64 and |f''| <= 0.5, f(x) = ln I0(x) - x, against viterbi::ln_i0, itself good to 1.9e-7 relative in
//            I0; about 1e-7 nats from 32 up. A float at 100 nats, how x_k(l) is held, has a step of 7.6e-6 nats, and each term is then multiplied by w, about 0.2.
#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace kz4ap::segmental::fast {

struct Tables {
    Tables();
    std::array<double, 32> two_to;        // 2^(i / 32)
    std::array<double, 256> log_center;   // ln c_i, c_i = 1 + (i + 0.5) / 256
    std::array<double, 256> inverse_center;  // 1 / c_i
    std::array<double, 2049> i0_rest;     // ln I0(x) - x at x = i / 64
};
extern const Tables tables;

// e^y, y at most 0 (nats); 0 below -40 nats.
inline double exp_neg(double y) {
    if (y < -40.0) return 0.0;
    constexpr double kPerLn2 = 46.16624130844683;  // 32 / ln 2
    constexpr double kLn2Over32 = 0.021660849392498290;
    const int k = static_cast<int>(y * kPerLn2 - 0.5);  // rounds to the nearest, y being at most 0
    const double r = y - k * kLn2Over32;
    const double polynomial = 1.0 + r * (1.0 + r * (0.5 + r * (1.0 / 6.0)));
    const auto bits = std::bit_cast<std::uint64_t>(tables.two_to[static_cast<unsigned>(k) & 31u]) + (static_cast<std::uint64_t>(static_cast<std::int64_t>(k >> 5)) << 52);
    return std::bit_cast<double>(bits) * polynomial;
}

// ln x, x positive and normal.
inline double log(double x) {
    constexpr double kLn2 = 0.69314718055994531;
    const auto bits = std::bit_cast<std::uint64_t>(x);
    const int exponent = static_cast<int>(bits >> 52) - 1023;
    const auto i = static_cast<unsigned>((bits >> 44) & 255u);
    const double mantissa = std::bit_cast<double>((bits & 0x000fffffffffffffull) | 0x3ff0000000000000ull);
    const double u = mantissa * tables.inverse_center[i] - 1.0;
    return exponent * kLn2 + tables.log_center[i] + u * (1.0 + u * (-0.5 + u * (1.0 / 3.0)));
}

// ln I0(x), x at least 0.
inline double ln_i0(double x) {
    if (x < 32.0) {
        const double position = x * 64.0;
        const int i = static_cast<int>(position);
        const double a = tables.i0_rest[static_cast<unsigned>(i)];
        return x + a + (position - i) * (tables.i0_rest[static_cast<unsigned>(i) + 1u] - a);
    }
    // I0(x) ~ e^x / sqrt(2 pi x) x (1 + 1 / (8 x) + 9 / (128 x^2) + 225 / (3072 x^3)); its logarithm to x^-3.
    const double inverse = 1.0 / x;
    constexpr double kHalfLnTwoPi = 0.91893853320467267;
    return x - kHalfLnTwoPi - 0.5 * log(x) + inverse * (0.125 + inverse * (0.0625 + inverse * (25.0 / 384.0)));
}

}  // namespace kz4ap::segmental::fast
