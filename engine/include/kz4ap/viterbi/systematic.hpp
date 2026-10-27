// The systematic term of a sample's error budget (design notes, "Detection, amplitude, frequency and the mark's shape",
// part 9; plan 1b, Task 5). Library code: the decoder does not run it yet.
//
// The model's own mismatch (the template's spread over the edge's position and the transmitter's rise, and what the
// model leaves out: fading other than modeled, transmitter ripple, the receiver's AGC, a neighbor's leakage) is a
// systematic effect added in quadrature to the noise, proportional to the carrier's amplitude (D15, D16, D17):
//
//     V'_k = sigma_y^2 + C_k^2 (f^2 s_k^2 + v_k)      (FS^2),
//
// on both sides of Lambda_k's ratio (b_k = |s_k| a / sqrt(V'_k), z_k = e_k / sqrt(V'_k); observation.hpp,
// sample_evidence), never divided by w. sigma_y^2 is the front filter's output noise power (FS^2), s_k the story's
// template and v_k its edge term (1; mark_template.hpp), f the floor (1; 0.02, a placeholder until the prototype measures
// it; owner, 2026-10-23).
//
// 1. C_k^2 (FS^2), from the data, never from a story's level: the larger of
//    - the channel's largest e^2 over a symmetric window of 21 samples, 10 back (the memory, 26.67 ms) and 10 ahead (the
//      look-ahead, 26.67 ms) around sample k (derived: the last sample at which the leeway can equal the noise after a
//      mark's last full-carrier sample, at +85 dB S500, over every edge position, is 10 samples; owner, 2026-10-23); the
//      window has fewer samples at the stream's ends (CarrierWindow);
//    - the sample's own e_k^2 / s_k^2, on every sample with s_k != 0 (owner, 2026-10-25, "B"), with no threshold.
//    Where s_k = 0 only the window's peak counts, and where also v_k = 0 (a space beyond the template's tail) V'_k =
//    sigma_y^2.
// 2. The caps (derived, for any data; the prior integrates to 1): w Lambda_k <= w z_k^2 and the own part keeps
//    C_k^2 >= e_k^2 / s_k^2, so z_k^2 <= s_k^2 / (f^2 s_k^2 + v_k) and
//        score_k <= w s_k^2 / (f^2 s_k^2 + v_k)      (nats):
//    on a single rise (w = 0.1986, f = 0.02, the 192 kHz template) 0.50, 2.51, 8.65, 19.8, 51.6, 265 and 493 nats at
//    j = 5 ... 11, and w / f^2 = 496.5 nats on the plateau.
#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <utility>

namespace kz4ap::viterbi {

// The channel's running peak of e^2 over a symmetric window, by a monotonic deque (amortized O(1) per sample): for the
// oldest sample k not yet searched, the largest e^2 (FS^2) over samples k - back_samples ... k + ahead_samples that
// exist (fewer at the stream's start, and at its end after finish()). A peak is ready once sample k + ahead_samples has
// been pushed, or after finish(); the stream may be pushed in any blocks.
class CarrierWindow {
public:
    // back_samples: the memory (10 samples, 26.67 ms), ahead_samples: the look-ahead (10 samples, 26.67 ms). Throws
    // std::invalid_argument for a negative count.
    CarrierWindow(int back_samples, int ahead_samples);

    // Appends the next sample's e^2 (FS^2). Throws std::invalid_argument for a value that is negative or not finite and
    // std::logic_error after finish().
    void push(double e2_fs2);

    // The peak (FS^2) for the oldest sample not yet searched, if ready; that sample is then searched.
    std::optional<double> next_peak_fs2();

    // Ends the stream: the last ahead_samples samples become ready, with the samples that exist after them.
    void finish();

private:
    int back_;
    int ahead_;
    bool finished_ = false;
    std::size_t pushed_ = 0;                          // samples pushed
    std::size_t next_ = 0;                            // the oldest sample not yet searched
    std::deque<double> pending_;                      // pushed samples not yet admitted, from index admitted_
    std::size_t admitted_ = 0;                        // samples admitted into the deque
    std::deque<std::pair<std::size_t, double>> peaks_;  // (index, e^2), values decreasing from the front
};

// V'_k (FS^2) = sigma2_fs2 + C^2 (floor^2 s^2 + v), C^2 = max(window_peak_fs2, e_fs^2 / s^2) where s != 0 and
// window_peak_fs2 where s = 0 (header, 1). sigma2_fs2: sigma_y^2 (FS^2); window_peak_fs2: CarrierWindow's peak (FS^2);
// e_fs: the sample's envelope (FS); s, v: the story's template and edge term (1); floor: f (1).
double v_prime_fs2(double sigma2_fs2, double window_peak_fs2, double e_fs, double s, double v, double floor);

// The cap on a sample's score (nats), w s^2 / (floor^2 s^2 + v) (header, 2); 0 where s = 0 (no evidence), +infinity where
// s != 0 and floor^2 s^2 + v = 0 (no systematic term, no cap).
double score_cap_nats(double w, double s, double v, double floor);

}  // namespace kz4ap::viterbi
