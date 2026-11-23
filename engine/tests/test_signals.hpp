#pragma once

#include "kz4ap/morse.hpp"
#include "kz4ap/types.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kz4ap::test {

// One stretch of text sent at its own timing: elements and element gaps at wpm (a dot of 1.2 s / wpm), the gaps between
// letters and between words stretched by `factor`, 3 and 7 dots x factor. Farnsworth spacing for characters at c WPM in
// an overall speed of s WPM has the factor farnsworth_factor(c, s); factor 1 is standard spacing.
struct Segment {
    std::string text;
    double wpm;
    double factor = 1.0;
};

// The Farnsworth factor of characters at c WPM sent at s WPM overall: (50 c / s - 31) / 19 (ARRL, Bloom 1990: one
// PARIS, 31 dots of elements and element gaps and 19 of gaps between letters and words, takes 60 / s s; the bench's
// T_g / T). 18 / 5 WPM gives 7.84.
inline double farnsworth_factor(double character_wpm, double overall_wpm) {
    return (50.0 * character_wpm / overall_wpm - 31.0) / 19.0;
}

// Key-down intervals (start_s, end_s) for segments sent one after another from start_s, each with its own timing. The
// word gap between two segments is timed by the earlier one: the sender's change comes with the next mark.
inline std::vector<std::pair<double, double>> keying_segments(const std::vector<Segment>& segments, double start_s) {
    std::vector<std::pair<double, double>> out;
    double t = start_s;
    for (std::size_t si = 0; si < segments.size(); ++si) {
        const Segment& segment = segments[si];
        const double dit = 1.2 / segment.wpm;
        std::vector<std::string> words;
        std::string word;
        for (char c : segment.text) {
            if (c == ' ') {
                if (!word.empty()) words.push_back(word);
                word.clear();
            } else {
                word += c;
            }
        }
        if (!word.empty()) words.push_back(word);
        for (std::size_t wi = 0; wi < words.size(); ++wi) {
            std::vector<std::string_view> codes;
            for (const auto symbol : morse::symbols(words[wi])) {
                const auto p = morse::encode(symbol);
                if (!p.empty()) codes.push_back(p);
            }
            for (std::size_t ci = 0; ci < codes.size(); ++ci) {
                for (std::size_t ei = 0; ei < codes[ci].size(); ++ei) {
                    const double len = codes[ci][ei] == '.' ? dit : 3 * dit;
                    out.emplace_back(t, t + len);
                    t += len;
                    if (ei + 1 < codes[ci].size()) t += dit;
                }
                if (ci + 1 < codes.size()) t += 3 * dit * segment.factor;
            }
            if (wi + 1 < words.size()) t += 7 * dit * segment.factor;
        }
        if (si + 1 < segments.size()) t += 7 * dit * segment.factor;
    }
    return out;
}

// Key-down intervals (start_s, end_s) for text at wpm, PARIS timing, beginning at start_s: one segment at standard
// spacing (factor 1, so 3 and 7 dots exactly).
inline std::vector<std::pair<double, double>> keying(const std::string& text, double wpm, double start_s) {
    return keying_segments({{text, wpm}}, start_s);
}

// A carrier keyed by the given key-down intervals (s), at freq_hz (0 = baseband) with 5 ms raised-cosine edges, plus
// complex white noise of total power noise_sigma^2. drift_hz_per_s: the carrier's frequency is freq_hz +
// drift_hz_per_s * t, Hz (t in s from the first sample).
inline std::vector<Sample> keyed_signal_of(const std::vector<std::pair<double, double>>& intervals, double rate,
                                           double duration_s, double freq_hz = 0, double amplitude = 1.0,
                                           double noise_sigma = 0.0, unsigned seed = 1, double drift_hz_per_s = 0.0) {
    const auto n = static_cast<std::size_t>(duration_s * rate);
    std::vector<double> env(n, 0.0);
    constexpr double kRise = 0.005;
    for (auto [on, off] : intervals) {
        const auto i0 = static_cast<std::size_t>(on * rate);
        const auto i1 = std::min(n, static_cast<std::size_t>(off * rate));
        for (std::size_t i = i0; i < i1; ++i) {
            const double t = i / rate;
            double e = 1.0;
            if (t - on < kRise) e = 0.5 - 0.5 * std::cos(std::numbers::pi * (t - on) / kRise);
            if (off - t < kRise) e = std::min(e, 0.5 - 0.5 * std::cos(std::numbers::pi * (off - t) / kRise));
            env[i] = e;
        }
    }
    std::mt19937 rng(seed);
    std::normal_distribution<double> gauss(0.0, std::max(noise_sigma, 1e-12) / std::sqrt(2.0));
    std::vector<Sample> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t_i = static_cast<double>(i) / rate;
        double ph = 2 * std::numbers::pi * freq_hz * static_cast<double>(i) / rate;
        if (drift_hz_per_s != 0) ph += std::numbers::pi * drift_hz_per_s * t_i * t_i;  // frequency freq_hz + drift * t
        const double a = amplitude * env[i];
        Sample s(static_cast<float>(a * std::cos(ph)), static_cast<float>(a * std::sin(ph)));
        if (noise_sigma > 0) s += Sample(static_cast<float>(gauss(rng)), static_cast<float>(gauss(rng)));
        x[i] = s;
    }
    return x;
}

// A keyed carrier at freq_hz (0 = baseband) with 5 ms raised-cosine edges, plus
// complex white noise of total power noise_sigma^2. drift_hz_per_s: the carrier's frequency
// is freq_hz + drift_hz_per_s * t, Hz (t in s from the first sample).
inline std::vector<Sample> keyed_signal(const std::string& text, double wpm, double rate, double duration_s,
                                        double freq_hz = 0, double amplitude = 1.0, double noise_sigma = 0.0,
                                        unsigned seed = 1, double start_s = 0.5, double drift_hz_per_s = 0.0) {
    return keyed_signal_of(keying(text, wpm, start_s), rate, duration_s, freq_hz, amplitude, noise_sigma, seed,
                           drift_hz_per_s);
}

// Duration that fits text at wpm starting at 0.5 s, plus trailing silence.
inline double duration_for(const std::string& text, double wpm, double tail_s = 1.5) {
    return keying(text, wpm, 0.5).back().second + tail_s;
}

// Adds complex white Gaussian noise of sigma_fs FS per real component (2 sigma_fs^2 FS^2 a sample) to x, the same samples with
// every standard library: Box-Muller on std::mt19937's 32-bit outputs, which the C++ standard fixes, the two outputs of one pair
// the real and imaginary parts of one sample, each draw its own statement. std::normal_distribution's algorithm is the library's
// own, so libstdc++ and MSVC's library draw different noise from the same seed (a test's two CI jobs then decode two different
// recordings), and two draws as the arguments of one call run in an order the language leaves unspecified. The same up to libm's
// last bit in log, cos and sin, which the rounding to float hides: a one-ulp difference in a double changes its float with
// probability 2^-29 (derived), and the 30 seeds of keying_coherence_test.cpp's pile-up (1.92 million samples each) give
// bit-identical samples with GCC 11.4 on Linux and with MSVC 14.51 on Windows (measured, ci-fix-2).
inline void add_portable_noise(std::vector<Sample>& x, double sigma_fs, unsigned seed) {
    std::mt19937 rng(seed);
    constexpr double kScale = 1.0 / 4294967296.0;  // 2^-32
    for (Sample& s : x) {
        const double u1 = (static_cast<double>(rng()) + 1.0) * kScale;  // (0, 1]
        const double u2 = static_cast<double>(rng()) * kScale;          // [0, 1)
        const double r = sigma_fs * std::sqrt(-2.0 * std::log(u1));
        const double phase = 2.0 * std::numbers::pi * u2;
        s += Sample(static_cast<float>(r * std::cos(phase)), static_cast<float>(r * std::sin(phase)));
    }
}

}  // namespace kz4ap::test
