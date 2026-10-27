// The weight w of a sample's evidence (design notes, "Detection, amplitude, frequency and the mark's shape", part 4,
// "The weight w_k follows each sample's expected SNR"; plan 1b, Task 5). Library code: the decoder does not run it yet.
//
// The envelope samples are correlated (the front filter's output, read every Delta = 2.667 ms), so each sample's
// log-likelihood is tempered by w, the composite-likelihood magnitude adjustment evaluated at the parameter (Chandler and
// Bate 2007; Varin, Reid and Firth 2011): the share of a sample's information about the amplitude a that is its own.
//
// 1. Its argument: the sample's expected signal-to-noise power ratio q = mu^2 s^2 / sigma_y^2 (1), mu the story's
//    predicted amplitude (FS), s the template (1) and sigma_y^2 the front filter's output noise power (FS^2), quoted in
//    dB SNR in the filter's 74.48 Hz.
// 2. Its two limits (derived; front_filter.hpp, output_noise): w_weak = 1 / (1 + 2 sum_m rho_m^2) = 0.2754 at q -> 0,
//    where Lambda is quadratic in the noise, and w_strong = 1 / (1 + 2 sum_m rho_m) = 0.1987 at q -> infinity, where it
//    is linear (rho_m the output noise's correlation at lags of Delta, m = 1 ... 6, from the 192 kHz chain).
// 3. The curve between them (measured; kz4ap-viterbi-tables weight, bench/src/viterbi_tables_main.cpp): for a steady
//    carrier at full template in the chain's own noise (complex white noise through the 192 kHz Channelizer and the
//    front filter, every 4th channel sample, 2^22 samples, seeds 1 and 2, one noise sequence per seed shared by every
//    node), with psi_k = dLambda_k/da at the true a,
//        w(q) = Var(psi) / (Var(psi) + 2 sum_{m=1..11} Cov(psi_k, psi_{k+m})),
//    the mean of the two seeds.
// 4. The table (heuristic grid, its adequacy measured): 129 nodes uniform in xi = q / (1 + q) on [0, 1], xi_i = i / 128,
//    the end nodes the derived limits w_weak (i = 0) and w_strong (i = 128) and the other 127 the curve; read by linear
//    interpolation in xi. q is clamped to [1e-4, 1e4] (-40 to +40 dB SNR in the filter's 74.48 Hz), where the curve is
//    within 0.0002 of the limits. The generated source, engine/src/viterbi/weight_table.cpp, records the command, the
//    seeds, the sample count, the chain and the interpolation error at the 128 midpoints.
#pragma once

#include <array>
#include <cstddef>

namespace kz4ap::viterbi {

inline constexpr int kWeightNodes = 129;          // nodes at xi = i / 128, i = 0 ... 128
inline constexpr double kWeightLowestQ = 1e-4;    // 1: -40 dB SNR in the filter's 74.48 Hz
inline constexpr double kWeightHighestQ = 1e4;    // 1: +40 dB SNR in the filter's 74.48 Hz

// The table's values w(xi_i) (1), generated (weight_table.cpp).
extern const std::array<double, kWeightNodes> kWeightTable;

// w (1) at the expected signal-to-noise power ratio q (1): q clamped to [kWeightLowestQ, kWeightHighestQ] (a q that is
// not a number reads as the lowest), xi = q / (1 + q), linear interpolation between the two nodes around xi. About
// 5 to 10 ns per call (rough estimate, design part 4).
inline double sample_weight(double q) {
    q = q > kWeightLowestQ ? (q < kWeightHighestQ ? q : kWeightHighestQ) : kWeightLowestQ;
    const double position = q / (1.0 + q) * (kWeightNodes - 1);
    int cell = static_cast<int>(position);
    if (cell > kWeightNodes - 2) cell = kWeightNodes - 2;
    const double fraction = position - cell;
    const double low = kWeightTable[static_cast<std::size_t>(cell)];
    return low + fraction * (kWeightTable[static_cast<std::size_t>(cell) + 1] - low);
}

}  // namespace kz4ap::viterbi
