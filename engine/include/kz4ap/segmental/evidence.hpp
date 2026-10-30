// The segmental decoder's evidence, computed once per channel per sample (see search.hpp for how the search reads it).
//
// 1. Running sums (RunningSums). Scoring is against "key up": a sample of key-up scores 0 nats, a sample of key-down
//    L1_k nats. Because a claimed edge at sample j shows only from j + 5 (s_4 is 8e-4, s_5 is 0.05) and is complete at
//    j + 11 (s_10 = 0.9825, s_11 = 1.0023), with the template's samples outside 5 ... 10 taken as 0 and 1, a mark from a
//    rise claimed at i to a fall claimed at j scores
//        rise(i) + C[j + 5] - C[i + 11] + fall(j),   C[m] = sum over k < m of L1_k,
//    rise(i) = sum over m = 5 ... 10 of R_(i+m)[m - 5], fall(j) = sum over m = 5 ... 10 of F_(j+m)[m - 5]:
//    the key-down score of sample k with the template s_m (R) or 1 - s_m (F) in place of 1. One subtraction and two
//    reads, whatever the mark's length. It holds for a mark and a gap of at least 6 samples (a dot of 16 ms, 75 WPM), for
//    which no two edges' windows overlap; below that the same formula counts the overlap by inclusion and exclusion (an
//    approximation).
//
// 2. The shared level (LevelEvidence). The per-story Kalman amplitude is replaced by one belief per channel over
//    (key state x level): frames of 8 samples with ln a held constant, a grid of levels every 1.5 dB relative to the
//    noise, a factor per frame
//        F_t(l) = sum over samples k in t of ln((1 - pi) + pi exp(x_k(l))),   x_k(l) = w Lambda(a_l; e_k),
//    (each sample key-down with probability pi, independently: the key's own timing is left to the search), a random
//    walk of ln a from frame to frame and a small jump to anywhere. The forward message runs on every frame; the backward
//    message runs every commit block over the look-ahead. The belief a sample's evidence uses is extrinsic: the forward
//    message into its frame times the backward message into its frame, so that the frame's own samples are not counted
//    twice. Then
//        L1_k = ln sum over l of P_t(l) exp(x_k(l)),
//    and R and F likewise with V = sigma^2 + a^2 (f^2 s^2 + v_m). Lambda is the Rician log-likelihood ratio against noise
//    alone, tempered by w = w_strong as in the viterbi decoder (viterbi/observation.hpp).
//
// The evidence reaches the search only after the look-ahead (level_lookahead_s + level_commit_s at most), so the text
// lags by about 1 s. A new over needs no newborn: the backward message gives its first mark the level of the marks after it.
#pragma once

#include "kz4ap/segmental/config.hpp"
#include "kz4ap/viterbi/front_filter.hpp"
#include "kz4ap/viterbi/mark_template.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace kz4ap::segmental {

// samples: the edge's window within a claim's template, and the sample after it.
inline constexpr int kEdgeFirst = 5;
inline constexpr int kEdgeCount = 6;
inline constexpr int kEdgeDone = kEdgeFirst + kEdgeCount;  // 11

// What the search reads, sample by sample, as a running sum (1 above). Samples are numbered from 0 as they are pushed.
class RunningSums {
public:
    // Appends sample k = committed(): its key-down score l1 (nats) and its scores under each position of the rise's and
    // the fall's window (nats; index m - 5).
    void push(double l1, const std::array<float, kEdgeCount>& rise, const std::array<float, kEdgeCount>& fall);

    // Samples pushed.
    std::int64_t committed() const { return count_; }
    // C[m], m <= committed(), m within the last kRing samples: nats.
    double sum(std::int64_t m) const { return sum_[static_cast<std::size_t>(m) & kMask]; }
    // rise(i) and fall(j), nats: whole once sample i + 10 has been pushed; they read the last kRing samples.
    double rise(std::int64_t i) const { return rise_[static_cast<std::size_t>(i) & kMask]; }
    double fall(std::int64_t j) const { return fall_[static_cast<std::size_t>(j) & kMask]; }
    // The score of a mark from a rise claimed at i to a fall claimed at j (formula 1 above), nats.
    double mark(std::int64_t i, std::int64_t j) const {
        return rise(i) + sum(j + kEdgeFirst) - sum(i + kEdgeDone) + fall(j);
    }

private:
    static constexpr std::size_t kRing = 8192;
    static constexpr std::size_t kMask = kRing - 1;
    std::vector<double> sum_ = std::vector<double>(kRing, 0.0);
    std::vector<double> rise_ = std::vector<double>(kRing, 0.0);
    std::vector<double> fall_ = std::vector<double>(kRing, 0.0);
    std::int64_t count_ = 0;
};

// The constants of the level's grid, shared by every channel of a configuration.
class LevelTables {
public:
    LevelTables(const SegmentalConfig& config, const viterbi::OutputNoise& noise, const viterbi::TemplateTables& tables);

    // One Rician term's constants at a level and a template value, with z = e / sigma_y:
    // x = w (z^2 (1 - 1 / v) - r_s^2 / v - ln v + ln I0(2 r_s z / v)), v = 1 + r^2 (f^2 s^2 + v_m), r_s = s r, r = a / sigma_y.
    struct Term {
        float z2_coeff;  // 1 - 1 / v
        float constant;  // -r_s^2 / v - ln v
        float i0_coeff;  // 2 r_s / v
    };
    int levels() const { return levels_; }
    const Term& plain(int level) const { return plain_[static_cast<std::size_t>(level)]; }
    // position 0 ... 5: m = 5 ... 10
    const Term& rise(int position, int level) const {
        return rise_[static_cast<std::size_t>(position * levels_ + level)];
    }
    const Term& fall(int position, int level) const {
        return fall_[static_cast<std::size_t>(position * levels_ + level)];
    }
    double w() const { return w_; }
    double sigma2_per_n0_hz() const { return sigma2_per_n0_hz_; }
    // The kernel of the frame-to-frame step: three taps (p, 1 - 2 p, p) applied `passes` times, and the jump's probability.
    double walk_p() const { return walk_p_; }
    int walk_passes() const { return walk_passes_; }
    double jump() const { return jump_; }

private:
    int levels_ = 0;
    double w_ = 0.0;
    double sigma2_per_n0_hz_ = 0.0;
    double walk_p_ = 0.0;
    int walk_passes_ = 1;
    double jump_ = 0.0;
    std::vector<Term> plain_;
    std::vector<Term> rise_;
    std::vector<Term> fall_;
};

// The shared level (2 above): takes envelope samples, and commits each sample's scores to its RunningSums once its
// look-ahead has been read.
class LevelEvidence {
public:
    LevelEvidence(const SegmentalConfig& config, const LevelTables& tables, RunningSums& sums);

    // One envelope sample: e_fs, FS; sigma2_fs2, the noise at the front filter's output, FS^2.
    void push(double e_fs, double sigma2_fs2);
    // The stream's end: commits every sample left with the look-ahead there is, then 11 samples of no evidence, so that
    // the search can reach the last sample.
    void finish();

private:
    void end_frame();
    void commit(bool all);
    void step_level(std::vector<double>& x) const;
    void commit_frame(std::int64_t frame, const double* belief);

    const LevelTables& tables_;
    RunningSums& sums_;
    int levels_;
    int frame_samples_;
    int lookahead_frames_;
    int commit_frames_;
    int ring_frames_;
    double log_key_up_;
    double log_key_down_;

    std::int64_t samples_ = 0;
    std::int64_t frames_ = 0;       // frames ended
    std::int64_t next_commit_ = 0;  // the first frame not committed
    int frame_fill_ = 0;
    std::vector<double> factor_;     // F of the frame being filled
    std::vector<double> alpha_;      // forward message after the last frame ended
    std::vector<double> predicted_;  // by frame in the ring, then level: the forward message into the frame
    std::vector<double> factors_;    // by frame in the ring, then level: F, relative to its largest
    std::vector<float> x_;           // by sample in the ring, then level: x_k(l)
    std::vector<float> z_;           // by sample in the ring: e / sigma_y
    std::vector<double> beta_;       // work
    std::vector<double> belief_;     // work
    std::vector<double> work_;       // work
    std::vector<std::array<float, kEdgeCount>> pending_rise_;
    std::vector<std::array<float, kEdgeCount>> pending_fall_;
};

}  // namespace kz4ap::segmental
