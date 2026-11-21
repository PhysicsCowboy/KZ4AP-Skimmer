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
//    which no two edges' windows overlap. A shorter element (LevelTables::short_score) has its two edges' superposition
//    scored sample by sample with the belief's few levels: computed only when a story could end one, at most once per
//    (kind, length) per boundary, and it adds nothing to a slow signal's cost once the fast speeds have left the beam.
//
// 2. The shared level (LevelEvidence). The per-story Kalman amplitude is replaced by one belief per channel over
//    (key state x level): a two-state key with a switch probability tau per sample (no timing: the key's timing is left
//    to the search), a grid of levels every 1.5 dB relative to the noise with ln a held constant over frames of 8 samples, a
//    random walk of ln a from frame to frame and a small jump to anywhere. Key-up scores 1 and key-down exp(x_k(l)),
//    x_k(l) = w Lambda(a_l; e_k). The key's memory matters: with a key independent from sample to sample, a low level
//    gains ln(1 / (1 - pi)) nats per sample of a gap over the true level, and the belief leaves the true level in every long
//    gap (measured in stage 1: CER above 1 at +20 dB S500). The forward message runs on every sample; the backward
//    message runs every commit block over the look-ahead. The belief a sample's evidence uses is extrinsic: the forward
//    message into its frame times the backward message into its frame, key summed out on each side, so that the frame's
//    own samples are not counted twice. Then
//        L1_k = ln sum over l of P_t(l) exp(x_k(l)),
//    and R and F likewise with V = sigma^2 + a^2 (f^2 s^2 + v_m). Lambda is the Rician log-likelihood ratio against noise
//    alone, tempered by w = w_strong as in the viterbi decoder (viterbi/observation.hpp). V carries the systematic floor f
//    and the grid's own relative variance, step^2 / 12 in nepers^2 (derived; config.hpp, level_grid_in_v).
//
// The evidence reaches the search only after the look-ahead (level_lookahead_s + level_commit_s at most): the evidence lags
// by 0.34 s and the search's 17 samples, 0.387 s at most. The text's own lag is longer: a publisher must stay 0.75 s behind
// the present to read the final text's CER (measured; docs/decision-log.md, 2026-10-31). A new over needs no newborn: the backward message gives its first
// mark the level of the marks after it. The approximation in all of this: the belief is one per channel, conditioned on
// no story's text, and it multiplies each sample's marginal rather than integrating one amplitude over a mark; what that
// costs is measured against the viterbi decoder (docs/decision-log.md, 2026-10-30).
#pragma once

#include "kz4ap/segmental/config.hpp"
#include "kz4ap/viterbi/front_filter.hpp"
#include "kz4ap/viterbi/mark_template.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kz4ap::segmental {

// samples: the edge's window within a claim's template, and the sample after it.
inline constexpr int kEdgeFirst = 5;
inline constexpr int kEdgeCount = 6;
inline constexpr int kEdgeDone = kEdgeFirst + kEdgeCount;  // 11
// samples: boundary j is searched when the evidence through j + kHorizon exists (search.hpp).
inline constexpr int kHorizon = 16;

// A sample as the short elements' templates read it: e / sigma_y and the belief's few levels with their probabilities.
inline constexpr int kEdgeLevels = 8;
struct SampleContext {
    double z = 0.0;
    std::uint8_t count = 0;
    std::uint8_t shift = 0;  // grid steps sigma^2 stands above the level's floor (LevelEvidence::push)
    std::array<std::uint8_t, kEdgeLevels> level{};
    std::array<float, kEdgeLevels> probability{};
};

// What the search reads, sample by sample, as a running sum (1 above). Samples are numbered from 0 as they are pushed.
class RunningSums {
public:
    // Appends sample k = committed(): its key-down score l1 (nats) and its scores under each position of the rise's and
    // the fall's window (nats; index m - 5). The context, for elements shorter than 6 samples, is empty if not given.
    void push(double l1, const std::array<double, kEdgeCount>& rise, const std::array<double, kEdgeCount>& fall,
              const SampleContext& context = {});
    // The context of a sample within the last 1 024.
    const SampleContext& context(std::int64_t k) const { return context_[static_cast<std::size_t>(k) & kContextMask]; }

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
    static constexpr std::size_t kContextMask = 1023;
    std::vector<SampleContext> context_ = std::vector<SampleContext>(kContextMask + 1);
    std::int64_t count_ = 0;
};

// The constants of the level's grid, shared by every channel of a configuration.
class LevelTables {
public:
    LevelTables(const SegmentalConfig& config, const viterbi::OutputNoise& noise, const viterbi::TemplateTables& tables);

    // One Rician term's constants at a level and a template value, with z = e / sigma_y:
    // x = w (z^2 (1 - 1 / v) - r_s^2 / v - ln v + ln I0(2 r_s z / v)), v = 1 + r^2 (f^2 s^2 + v_m), r_s = s r, r = a / sigma_y.
    // What is held and computed in floats is x less w z^2, which is common to every level and small at the level that fits;
    // w z^2 itself, in a double, passes 1e9 nats at the top of the grid, where a float's step is tens of nats.
    struct Term {
        float inverse_v;  // 1 / v
        float constant;  // -r_s^2 / v - ln v
        float i0_coeff;  // 2 r_s / v
    };
    int levels() const { return levels_; }
    // Grid steps the tables reach below the grid's bottom: a level is looked up at (level - shift), shift 0 ... below(), where
    // sigma^2 stands shift steps above the level's floor (LevelEvidence::push).
    int below() const { return below_; }
    // A level of the grid, 0 ... levels() - 1, at sigma^2 `shift` steps above the floor, 0 ... below().
    const Term& plain(int level, int shift = 0) const { return plain_[static_cast<std::size_t>(level - shift + below_)]; }
    // position 0 ... 5: m = 5 ... 10
    const Term& rise(int position, int level, int shift = 0) const {
        return rise_[static_cast<std::size_t>(position * table_levels_ + level - shift + below_)];
    }
    const Term& fall(int position, int level, int shift = 0) const {
        return fall_[static_cast<std::size_t>(position * table_levels_ + level - shift + below_)];
    }
    // The template of an element of length (1 ... 5 samples) shorter than the edge's window, whose two edges overlap: at
    // position (0 ... length + 5, the sample i + 5 + position after its first edge), for a mark
    // s = s_(p) - s_(p - length), for a gap 1 - s_(p) + s_(p - length), and the variance adds the pair's terms
    // (viterbi/mark_template.hpp, stage 4).
    const Term& short_term(bool gap, int length, int position, int level, int shift = 0) const {
        return short_[static_cast<std::size_t>((((gap ? 1 : 0) * 5 + length - 1) * kShortPositions + position) * table_levels_ + level -
                                               shift + below_)];
    }
    // The score of such an element's samples, whose first edge is at sample i of `sums`, nats.
    double short_score(bool gap, int length, std::int64_t i, const class RunningSums& sums) const;
    double w() const { return w_; }
    double sigma2_per_n0_hz() const { return sigma2_per_n0_hz_; }
    // dB: the grid's step; and whether the grid is relative to the floor (SegmentalConfig::level_on_floor)
    double step_db() const { return step_db_; }
    bool on_floor() const { return on_floor_; }
    // The kernel of the frame-to-frame step: three taps (p, 1 - 2 p, p) applied `passes` times, and the jump's probability.
    double walk_p() const { return walk_p_; }
    int walk_passes() const { return walk_passes_; }
    double jump() const { return jump_; }
    // The walk from each level when it depends on the level (SegmentalConfig::level_walk_high_nepers_per_sqrt_s): a levels x levels
    // matrix, row-major, K[to][from], each column a reflecting Gaussian of its level's variance; empty when the walk is the same
    // from every level (the three-tap passes).
    const std::vector<double>& walk_kernel() const { return walk_kernel_; }

private:
    int levels_ = 0;
    int below_ = 0;
    int table_levels_ = 0;  // levels_ + below_
    double step_db_ = 1.5;
    bool on_floor_ = true;
    double w_ = 0.0;
    double sigma2_per_n0_hz_ = 0.0;
    double walk_p_ = 0.0;
    int walk_passes_ = 1;
    double jump_ = 0.0;
    std::vector<double> walk_kernel_;
    std::vector<Term> plain_;
    std::vector<Term> rise_;
    std::vector<Term> fall_;
    static constexpr int kShortPositions = 11;
    std::vector<Term> short_;
};

// The shared level (2 above): takes envelope samples, and commits each sample's scores to its RunningSums once its
// look-ahead has been read.
// The channel's own noise floor. Key-up is pinned at the noise it is told, a key-up sample scoring 0 nats whatever it holds,
// and from about 4.8 dB below the channel's real floor key-down at a low level beats key-up on bare noise, so that a story
// earns by bridging its own gaps, however strong the station (measured; review F1). The hiss (viterbi/hiss.hpp) reads the
// quiet bins around the channel, so it reads low behind a receiver's passband or under a floor raised around the channel.
// Hence, for each sample,
//     sigma^2 = max(hiss sigma^2, q(e^2) / -ln(1 - q) x 10^(margin / 10)),
// q(e^2) the quantile q of every stride's e^2 over the last window, taken again every refresh, used once start is in. For
// noise alone e^2 is exponential of mean sigma^2, so its quantile q is -ln(1 - q) sigma^2 (derived; -ln 0.9 = 0.10536). The
// margin, window, quantile, stride, refresh and start are heuristics (config.hpp). A key-down carrier held for over
// 1 - q of the window reads as noise.
class ChannelFloor {
public:
    // Throws std::invalid_argument for a setting outside its range, a stride or a refresh that is not a whole number of samples.
    explicit ChannelFloor(const SegmentalConfig& config);
    // One envelope sample, e_fs, FS; hiss_sigma2_fs2, the hiss at the front filter's output, FS^2. What the sample's noise is: FS^2.
    double sigma2(double e_fs, double hiss_sigma2_fs2);

private:
    bool on_ = false;
    int stride_ = 1;   // samples
    int refresh_ = 1;  // samples
    std::size_t capacity_ = 1;  // kept values
    std::size_t minimum_ = 1;   // kept values before it is used
    double quantile_ = 0.0;
    double to_noise_ = 0.0;  // 10^(margin / 10) / -ln(1 - q)
    std::vector<double> ring_, work_;
    std::size_t written_ = 0;
    std::int64_t count_ = 0;  // samples
    double floor_ = 0.0;      // FS^2; 0 until it is used
};

class LevelEvidence {
public:
    LevelEvidence(const SegmentalConfig& config, const LevelTables& tables, RunningSums& sums);

    // One envelope sample: e_fs, FS; sigma2_fs2, the noise at the front filter's output, FS^2; floor_fs2, the channel's noise
    // floor there, FS^2, at most sigma2_fs2, what the level's grid is relative to (SegmentalConfig::level_on_floor). sigma^2 is
    // taken at the floor times 10^(shift x step / 10), shift the nearest whole number of grid steps, 0 ... LevelTables::below(),
    // and level l scored as level l - shift of the tables. Without a floor, or with level_on_floor 0, the floor is sigma^2.
    void push(double e_fs, double sigma2_fs2, double floor_fs2);
    void push(double e_fs, double sigma2_fs2) { push(e_fs, sigma2_fs2, sigma2_fs2); }
    // The stream's end: commits every sample left with the look-ahead there is, then 17 samples (kHorizon + 1) of no evidence,
    // so that the search can reach the last sample.
    void finish();

private:
    void end_frame();
    void commit(bool all);
    // forward: x' = K x; backward (the transpose): x' = K^T x. The same for the three-tap passes, which are symmetric.
    void step_level(double* x, bool backward = false) const;
    void commit_frame(std::int64_t frame, const double* belief);

    const LevelTables& tables_;
    RunningSums& sums_;
    int levels_;
    int frame_samples_;
    int lookahead_frames_;
    int commit_frames_;
    int ring_frames_;
    double key_switch_;  // probability per sample that the key changes state

    std::int64_t samples_ = 0;
    std::int64_t frames_ = 0;       // frames ended
    std::int64_t next_commit_ = 0;  // the first frame not committed
    int frame_fill_ = 0;
    std::vector<double> alpha_;      // forward message: key up by level, then key down by level
    std::vector<double> entering_;   // by frame in the ring, then level: the forward message into the frame, key summed out
    std::vector<float> x_;           // by sample in the ring, then level: x_k(l) less w z^2
    std::vector<double> z_;          // by sample in the ring: e / sigma_y
    std::vector<std::uint8_t> shift_;  // by sample in the ring: grid steps sigma^2 stands above the floor
    std::vector<double> top_;        // by sample in the ring: the largest of 0 and w z^2 + x_k(l), nats
    std::vector<double> beta_;       // backward message: key up by level, then key down by level
    std::vector<double> belief_;     // by frame in the ring, then level: what its samples read
};

}  // namespace kz4ap::segmental
