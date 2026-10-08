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

// Key-down intervals (start_s, end_s) for text at wpm, PARIS timing, beginning at start_s.
inline std::vector<std::pair<double, double>> keying(const std::string& text, double wpm, double start_s) {
    const double dit = 1.2 / wpm;
    std::vector<std::string> words;
    std::string word;
    for (char c : text) {
        if (c == ' ') {
            if (!word.empty()) words.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) words.push_back(word);

    std::vector<std::pair<double, double>> out;
    double t = start_s;
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
            if (ci + 1 < codes.size()) t += 3 * dit;
        }
        if (wi + 1 < words.size()) t += 7 * dit;
    }
    return out;
}

// A keyed carrier at freq_hz (0 = baseband) with 5 ms raised-cosine edges, plus
// complex white noise of total power noise_sigma^2. drift_hz_per_s: the carrier's frequency
// is freq_hz + drift_hz_per_s * t, Hz (t in s from the first sample).
inline std::vector<Sample> keyed_signal(const std::string& text, double wpm, double rate, double duration_s,
                                        double freq_hz = 0, double amplitude = 1.0, double noise_sigma = 0.0,
                                        unsigned seed = 1, double start_s = 0.5, double drift_hz_per_s = 0.0) {
    const auto n = static_cast<std::size_t>(duration_s * rate);
    std::vector<double> env(n, 0.0);
    constexpr double kRise = 0.005;
    for (auto [on, off] : keying(text, wpm, start_s)) {
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

// Duration that fits text at wpm starting at 0.5 s, plus trailing silence.
inline double duration_for(const std::string& text, double wpm, double tail_s = 1.5) {
    return keying(text, wpm, 0.5).back().second + tail_s;
}

}  // namespace kz4ap::test
