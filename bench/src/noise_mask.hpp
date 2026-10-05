// The spectrum noise estimate's mask, measured (Plan B, B4b; stage 1's Task 5 method on the C++ estimator):
// the per-branch mask bias b_mask,k in white noise, and the mask's acceptance on a given stream (segments offered,
// accepted, kept fraction). Used by the kz4ap-noise-mask tool and by the test that pins BankConfig::mask_bias.
//
// The estimate is driven as the channel drives it: P = |boxcar(u, N_k)|^2 rounded to float32, one update per
// block of round(block_s r) samples, sigma2() after each block.
#pragma once

#include "kz4ap/bank/bank_config.hpp"

#include <complex>
#include <cstdint>
#include <span>
#include <vector>

namespace kz4ap::bench {

// White complex Gaussian noise of 1 FS^2 per complex sample (0.5 FS^2 per real component): std::mt19937_64 seeded
// with `seed`, two 53-bit uniforms per Box-Muller pair. Defined without std::normal_distribution, whose algorithm
// differs between standard libraries, so the same seed gives the same noise on Windows and Linux (up to libm's last
// bit in log, sqrt, cos and sin).
std::vector<std::complex<double>> white_noise(std::size_t samples, std::uint64_t seed);

// One stream through the "spectrum" estimate. Counts are cumulative from the stream's start; `window_*` count only
// the segments offered in [from_s, to_s) of stream time (the segment's first sample), for acceptance rates while a
// station sends.
struct MaskRun {
    double seconds = 0.0;                // stream length, s
    int segments = 0;                    // accepted, whole stream
    int segments_offered = 0;            // offered (after the three-tap warm-up), whole stream
    double kept_fraction_sum = 0.0;      // sum over offered segments of the unflagged fraction
    std::vector<double> masked_power;    // masked_branch_power(), sigma_v,k^2 per real component, FS^2 (empty if
                                         // no segment entered)
    double window_s = 0.0;               // to_s - from_s, s (0: no window)
    int window_segments = 0;             // accepted in the window
    int window_offered = 0;              // offered in the window
    double window_kept_fraction_sum = 0.0;
};

// Runs the "spectrum" estimate of cfg over u at rate_hz. from_s < to_s selects the counting window (segments whose
// first sample lies in it); otherwise no window.
MaskRun run_mask(const bank::BankConfig& cfg, std::span<const std::complex<double>> u, double rate_hz,
                 double from_s = 0.0, double to_s = 0.0);

// b_mask,k by stage 1's Task 5 method: for each seed, `seconds` of white noise (1 FS^2); the segment-weighted mean
// over seeds of masked_power divided by the true sigma_v,k^2 = 1 / (2 N_k) (derived).
struct MaskBias {
    std::vector<double> bias;                   // pooled b_mask,k, dimensionless
    std::vector<std::vector<double>> per_seed;  // each seed's own ratio, dimensionless
    std::vector<double> scatter;                // per-seed standard deviation / pooled value, dimensionless
    int segments = 0;                           // accepted, all seeds
    int segments_offered = 0;
    double kept_fraction = 0.0;                 // mean over offered segments
};
MaskBias measure_mask_bias(const bank::BankConfig& cfg, double rate_hz, const std::vector<std::uint64_t>& seeds,
                           double seconds);

}  // namespace kz4ap::bench
