#pragma once

// Test signals keyed with D17's edges (design notes, "Detection, amplitude, frequency and the mark's shape", part 14;
// the Python generator's training/kz4ap_synth/generate.py, "start" and "centered" placements): the rise starts at
// key-down and the fall at key-up, each a raised cosine of full length E = rise_10_90_s / 0.590334, so a mark keeps its
// keyed length at half amplitude. test_signals.hpp's keyed_signal_of keeps milestone 1's 5 ms edges inside each mark;
// this header is for the new decoder's tests.

#include "../test_signals.hpp"
#include "kz4ap/types.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kz4ap::test {

// Where an edge lies against the interval's end. Start (D17): the rise starts at key-down and the fall at key-up, so the
// envelope is at half amplitude E / 2 after each. Centered (VE3NEA's convention): each edge is centered on the
// interval's end, half amplitude at the end itself.
enum class Placement { Start, Centered };

// The fraction of a raised cosine's full length its 10-90% time takes, (pi - 2 acos 0.8) / pi = 0.590334 (derived).
inline double raised_cosine_10_90() { return (std::numbers::pi - 2.0 * std::acos(0.8)) / std::numbers::pi; }

// The envelope (1, key-down amplitude) of the key-down intervals (s) at the n instants i / rate, i = 0 ... n - 1:
// the sum over marks of c(t - on) (1 - c(t - off)), c(x) = 1/2 - 1/2 cos(pi clip(x / E, 0, 1)), E = rise_10_90_s /
// 0.590334 (s); a gap shorter than E sums a fall and the next rise, which stays at or below 1. For Centered the
// interval's ends are moved by E / 2 earlier.
inline std::vector<double> keying_envelope_d17(const std::vector<std::pair<double, double>>& intervals, double rate,
                                               std::size_t n, double rise_10_90_s = 0.0025,
                                               Placement placement = Placement::Start) {
    if (!(rate > 0.0) || !(rise_10_90_s > 0.0)) {
        throw std::invalid_argument("keying_envelope_d17: rate and rise must be positive");
    }
    const double edge_s = rise_10_90_s / raised_cosine_10_90();
    const double shift_s = placement == Placement::Centered ? edge_s / 2.0 : 0.0;
    auto c = [&](double x) { return 0.5 - 0.5 * std::cos(std::numbers::pi * std::clamp(x / edge_s, 0.0, 1.0)); };
    std::vector<double> env(n, 0.0);
    for (const auto& [on_s, off_s] : intervals) {
        const double on = on_s - shift_s;
        const double off = off_s - shift_s;
        const auto first = static_cast<long long>(std::floor(on * rate));
        const auto last = static_cast<long long>(std::ceil((off + edge_s) * rate)) + 1;
        for (long long i = std::max(0LL, first); i < std::min(last, static_cast<long long>(n)); ++i) {
            const double t = static_cast<double>(i) / rate;
            env[static_cast<std::size_t>(i)] += c(t - on) * (1.0 - c(t - off));
        }
    }
    return env;
}

// A carrier keyed by the given key-down intervals (s) with D17's edges, at freq_hz (0 = baseband), plus complex white
// noise of total power noise_sigma^2 per sample. drift_hz_per_s: the carrier's frequency is freq_hz + drift_hz_per_s *
// t, Hz (t in s from the first sample); as keyed_signal_of.
inline std::vector<Sample> keyed_signal_d17(const std::vector<std::pair<double, double>>& intervals, double rate,
                                            double duration_s, double freq_hz = 0, double amplitude = 1.0,
                                            double noise_sigma = 0.0, unsigned seed = 1, double drift_hz_per_s = 0.0,
                                            double rise_10_90_s = 0.0025, Placement placement = Placement::Start) {
    const auto n = static_cast<std::size_t>(duration_s * rate);
    const std::vector<double> env = keying_envelope_d17(intervals, rate, n, rise_10_90_s, placement);
    std::mt19937 rng(seed);
    std::normal_distribution<double> gauss(0.0, std::max(noise_sigma, 1e-12) / std::sqrt(2.0));
    std::vector<Sample> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t_i = static_cast<double>(i) / rate;
        double ph = 2 * std::numbers::pi * freq_hz * static_cast<double>(i) / rate;
        if (drift_hz_per_s != 0) ph += std::numbers::pi * drift_hz_per_s * t_i * t_i;
        const double a = amplitude * env[i];
        Sample s(static_cast<float>(a * std::cos(ph)), static_cast<float>(a * std::sin(ph)));
        if (noise_sigma > 0) s += Sample(static_cast<float>(gauss(rng)), static_cast<float>(gauss(rng)));
        x[i] = s;
    }
    return x;
}

// keyed_signal_d17 for text at wpm (PARIS timing from start_s), as test_signals.hpp's keyed_signal.
inline std::vector<Sample> keyed_text_d17(const std::string& text, double wpm, double rate, double duration_s,
                                          double freq_hz = 0, double amplitude = 1.0, double noise_sigma = 0.0,
                                          unsigned seed = 1, double start_s = 0.5, double drift_hz_per_s = 0.0,
                                          double rise_10_90_s = 0.0025, Placement placement = Placement::Start) {
    return keyed_signal_d17(keying(text, wpm, start_s), rate, duration_s, freq_hz, amplitude, noise_sigma, seed,
                            drift_hz_per_s, rise_10_90_s, placement);
}

// n samples at `rate` of a flat Rayleigh fading gain: complex Gaussian, unit mean power (E|g|^2 = 1), whose Doppler power
// spectrum is Gaussian, S(f) ~ exp(-f^2 / (2 s^2)), with two-sigma width 2 s = spread_hz (Hz; the Python generator's
// "gaussian" shape, training/kz4ap_synth/fading.py). Made as the generator does: white complex Gaussian noise at a slow
// rate, shaped by the Gaussian amplitude response exp(-f^2 / (4 s^2)) (here as its Gaussian impulse response, a finite
// sum), then linearly interpolated to `rate`. The slow rate is max(50, 10 spread_hz) samples/s, at most `rate`.
inline std::vector<std::complex<double>> rayleigh_gain(std::size_t n, double rate, double spread_hz, unsigned seed) {
    if (!(rate > 0.0) || !(spread_hz > 0.0)) {
        throw std::invalid_argument("rayleigh_gain: rate and spread must be positive");
    }
    const double slow_rate = std::min(rate, std::max(50.0, 10.0 * spread_hz));
    const double sigma_f = spread_hz / 2.0;  // Hz
    // h(t) ~ exp(-4 pi^2 sigma_f^2 t^2): the amplitude response's Fourier transform; taken to 5 time constants.
    const double reach_s = 5.0 / (2.0 * std::numbers::pi * sigma_f);
    const auto half = static_cast<long long>(std::ceil(reach_s * slow_rate));
    std::vector<double> h(static_cast<std::size_t>(2 * half + 1));
    double norm = 0.0;
    for (long long j = -half; j <= half; ++j) {
        const double t = static_cast<double>(j) / slow_rate;
        const double v = std::exp(-4.0 * std::numbers::pi * std::numbers::pi * sigma_f * sigma_f * t * t);
        h[static_cast<std::size_t>(j + half)] = v;
        norm += v * v;
    }
    for (double& v : h) v /= std::sqrt(norm);  // white input of unit power gives output of unit power
    const auto m = static_cast<std::size_t>(std::ceil(static_cast<double>(n) / rate * slow_rate)) + 2;
    std::mt19937 rng(seed);
    std::normal_distribution<double> gauss(0.0, std::sqrt(0.5));
    std::vector<std::complex<double>> white(m + h.size() - 1);
    for (auto& w : white) w = {gauss(rng), gauss(rng)};
    std::vector<std::complex<double>> slow(m);
    for (std::size_t i = 0; i < m; ++i) {
        std::complex<double> acc = 0.0;
        for (std::size_t j = 0; j < h.size(); ++j) acc += h[j] * white[i + j];
        slow[i] = acc;
    }
    std::vector<std::complex<double>> g(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double pos = static_cast<double>(i) / rate * slow_rate;
        const auto i0 = static_cast<std::size_t>(pos);
        const double frac = pos - static_cast<double>(i0);
        g[i] = slow[i0] * (1.0 - frac) + slow[std::min(i0 + 1, m - 1)] * frac;
    }
    return g;
}

}  // namespace kz4ap::test
