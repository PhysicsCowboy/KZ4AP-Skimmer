// The new decoder's front filter and envelope samples (design notes, "Detection, amplitude, frequency and the mark's
// shape", parts 2 to 4; plan 1b, Task 3). The decoder runs it (search.hpp; viterbi_decoder.hpp).
//
// A channel's stream x[n] (the channelizer's complex output, FS, at the channel rate r = 1 500 samples/s) goes through
// one 25-tap filter and is read every 4th channel sample:
//
// 1. The taps (front_filter_taps; derived). The filter is matched to a real 100 WPM dot: a dot keyed for T = 12 ms whose
//    rise starts at key-down and whose fall starts at key-up, each a raised cosine of full length E = rise_10_90_s /
//    0.590334 = 4.2349 ms (0.590334 = (pi - 2 acos 0.8) / pi is a raised cosine's 10-90% time as a fraction of its
//    length, derived):
//        p(t) = c(t) (1 - c(t - T)),   c(x) = 1/2 - 1/2 cos(pi clip(x / E, 0, 1)),
//        h[j] = p(t_c + j / r) / sum p,   j = -M ... M,   t_c = (T + E) / 2 = 8.12 ms the pulse's center,
//    M = floor((T + E) r / 2) = 12: 25 taps, symmetric, unity gain at 0 Hz, so a steady carrier of amplitude a gives
//    |y| = a. The pulse is zero beyond T + E, so the taps are its samples across its whole length.
//
// 2. The samples (EnvelopeSampler). Run causally, y[n] = sum over i = 0 ... 24 of h[i - 12] x[n - i] (the delay is 12
//    channel samples, 8.000 ms), at every channel sample of absolute index n (counted from the stream's start) that is a
//    multiple of 4, k = n / 4, t_k = 4 k / r: one envelope sample e_k = |y| (FS) every Delta = 4 / r = 2.667 ms, 375
//    samples/s. The history before the first sample is zero.
//
// 3. The output noise (output_noise; derived). White complex input noise of density N0 (FS^2/Hz) at the input rate f_s
//    has power N0 f_s per sample. The channel filter g (taps real, sum 1) and the decimation D give the channel stream the
//    autocovariance
//        R_c[d] = N0 f_s sum_n g[n] g[n + d D]      (d channel samples, d >= 0),
//    and the front filter h the output's
//        R_y[l] = sum_a sum_b h[a] h[b] R_c[|l + a - b|]      (l channel samples).
//    Then sigma_y^2 = R_y[0], reported as sigma2_per_n0_hz = R_y[0] / N0 (Hz); rho_m =
//    R_y[4 m] / R_y[0] for m = 0 ... 6 (the lags of Delta); and the two limits of the weight that counts correlated
//    samples once:
//        w_strong = 1 / (1 + 2 sum_{m=1..6} rho_m)      (a statistic linear in the noise),
//        w_weak   = 1 / (1 + 2 sum_{m=1..6} rho_m^2)    (a statistic quadratic in the noise: powers of complex Gaussian
//                                                        noise correlate as rho_m^2).
//    The correlation beyond lag 6 is taken as 0, as the design does (rho_6 = -0.005 on the default chain). On the
//    default chain: sigma_y^2 / N0 = 74.50 Hz, rho_1 ... rho_6 = 0.8776, 0.6256, 0.3666, 0.1380, 0.0138, -0.0052,
//    w_strong = 0.1987 and w_weak = 0.2754 (derived; the design notes quote 74.48 Hz and 0.1986 from a 48 kHz model of the
//    channel filter and the continuous pulse, 0.03% apart).
#pragma once

#include "kz4ap/types.hpp"

#include <array>
#include <complex>
#include <cstdint>
#include <span>
#include <vector>

namespace kz4ap::viterbi {

// The front filter's taps h[j], j = -M ... M, listed from j = -M, for a channel stream of channel_rate_hz (samples/s), a
// dot of dot_s (s) and edges of rise_10_90_s (s, 10-90% time): p(t_c + j / r) / sum p with the pulse p of the header's
// stage 1 (derived). 2 floor((T + E) r / 2) + 1 taps. Throws std::invalid_argument for a non-positive or non-finite
// argument, and, at channel_rate_hz = 1 500 samples/s (within 1e-9, relatively), unless that is 25 taps (the decoder's
// fixed size: other dots and rises need the decoder's other constants changed with them).
std::vector<double> front_filter_taps(double channel_rate_hz, double dot_s, double rise_10_90_s);

// The output noise of the channel filter and the front filter on white input noise of density N0 (stage 3 above).
struct OutputNoise {
    double sigma2_per_n0_hz;       // sigma_y^2 / N0, Hz (74.50 Hz on the default chain)
    std::array<double, 7> rho;     // rho_0 = 1 ... rho_6, at lags of Delta (4 channel samples)
    double w_strong;               // 1 / (1 + 2 sum rho_m), 0.1987 on the default chain
    double w_weak;                 // 1 / (1 + 2 sum rho_m^2), 0.2754 on the default chain
};

// channel_taps: the channel filter's taps at input_rate_hz (samples/s; channel_filter_taps), decimation: the input
// samples per channel sample, front_taps: the front filter's taps, channel_samples_per_sample: Delta in channel samples
// (4). Throws std::invalid_argument for an empty tap set or a non-positive rate, decimation or spacing.
OutputNoise output_noise(std::span<const double> channel_taps, double input_rate_hz, int decimation,
                         std::span<const double> front_taps, int channel_samples_per_sample);

// One envelope sample: k = n / 4 (the channel sample of absolute index 4 k, at t_k = 4 k / r), the front filter's complex
// output y (FS) and e = |y| (FS).
struct EnvelopeSample {
    std::int64_t k;
    std::complex<double> y_fs;
    double e_fs;
};

// The front filter run causally on a channel stream, giving a sample at every channel sample whose absolute index (from
// the stream's start) is a multiple of channel_samples_per_sample. It keeps taps.size() - 1 samples of history across
// blocks; the result does not depend on how the stream is split. A block whose first_index does not follow the previous
// block (the first block of a channel born mid-stream, or a gap) starts with zero history.
class EnvelopeSampler {
public:
    // taps: the front filter's taps, listed from j = -M. Throws std::invalid_argument for empty taps or a spacing below 1.
    EnvelopeSampler(std::vector<double> taps, int channel_samples_per_sample);

    // Appends the samples for the channel samples block[0 ...], whose absolute indices are first_index ... .
    void push(std::span<const Sample> block, std::uint64_t first_index, std::vector<EnvelopeSample>& out);

    // Forgets the history; the next block starts with zero history.
    void reset();

private:
    std::vector<double> taps_;
    int spacing_;
    std::vector<std::complex<double>> work_;  // history followed by the current block
    std::vector<std::complex<double>> history_;
    std::uint64_t next_index_ = 0;
    bool has_next_ = false;
};

}  // namespace kz4ap::viterbi
