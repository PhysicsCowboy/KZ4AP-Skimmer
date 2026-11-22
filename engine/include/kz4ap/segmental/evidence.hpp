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
//    to the search), a grid of levels every 1.5 dB relative to the noise with ln a held constant over blocks of 8 samples (two
//    frames of 4 joined; one frame each while the walk is fast, text-fast-1), a random walk of ln a from block to block and a
//    small jump to anywhere. Key-up scores 1 and key-down exp(x_k(l)),
//    x_k(l) = w Lambda(a_l; e_k). The key's memory matters: with a key independent from sample to sample, a low level
//    gains ln(1 / (1 - pi)) nats per sample of a gap over the true level, and the belief leaves the true level in every long
//    gap (measured in stage 1: CER above 1 at +20 dB S500). The forward message runs on every sample; the backward
//    message runs every commit block over the look-ahead. The belief a sample's evidence uses is extrinsic: the forward
//    message into its block times the backward message into its block's end, key summed out on each side, so that the block's
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
#include <complex>
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

// samples: the most tail positions after a fall's window (SegmentalConfig::echo_max_s at the widest filter, 48 ms).
inline constexpr int kTailMax = 18;

// What the search reads, sample by sample, as a running sum (1 above). Samples are numbered from 0 as they are pushed.
class RunningSums {
public:
    // Appends sample k = committed(): its key-down score l1 (nats) and its scores under each position of the rise's and
    // the fall's window (nats; index m - 5). The context, for elements shorter than 6 samples, is empty if not given.
    // tail[q], q < tail_count: its score under the channel's tail at position m = 11 + q after a fall claimed at k - 11 - q,
    // against key-up (nats; EchoEstimator).
    void push(double l1, const std::array<double, kEdgeCount>& rise, const std::array<double, kEdgeCount>& fall,
              const SampleContext& context = {}, const double* tail = nullptr, int tail_count = 0);
    // nats: the tail's score over the first n samples after the window of a fall claimed at sample i (i + 11 ... i + 10 + n),
    // as far as they have been pushed and the tail reaches; 0 with no tail. i within the last kRing samples.
    double tail(std::int64_t i, int n) const {
        if (n <= 0) return 0.0;
        const std::size_t c = static_cast<std::size_t>(i);
        const int filled = tail_filled_[c & kMask];
        if (n >= filled) return tail_whole_[c & kMask];
        return tail_prefix_[(c & kTailMask) * (kTailMax + 1) + static_cast<std::size_t>(n)];
    }
    // The tail positions of the claim at i pushed so far.
    int tail_filled(std::int64_t i) const { return tail_filled_[static_cast<std::size_t>(i) & kMask]; }
    // samples: the tail positions of the last sample pushed (0 with no tail).
    int tail_positions() const { return tail_positions_; }
    // samples: the tail positions that follow a fall claimed at i, those pushed with sample i + 11, its first tail position
    // (text-tail-2); the last sample's while that sample is still to come. i within the last kRing samples.
    int tail_length(std::int64_t i) const {
        if (i + kEdgeDone >= count_) return tail_positions_;
        return tail_count_[static_cast<std::size_t>(i + kEdgeDone) & kMask];
    }
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
    // The tail's prefix sums by claim (the last 1 024), its positions filled and its whole (the last kRing); left empty while no
    // tail is pushed.
    static constexpr std::size_t kTailMask = 1023;
    std::vector<double> tail_prefix_;
    std::vector<std::uint8_t> tail_filled_ = std::vector<std::uint8_t>(kRing, 0);
    std::vector<double> tail_whole_ = std::vector<double>(kRing, 0.0);
    int tail_positions_ = 0;
    std::vector<std::uint8_t> tail_count_ = std::vector<std::uint8_t>(kRing, 0);  // by sample: the tail positions pushed with it
};

// The channel's tail after a fall, as power re the mark's key-down power at the filter's output (text-fast-1; config.hpp,
// echo_max_s). Empty (on false) for a clean channel.
// With `absolute` (SegmentalConfig::echo_absolute, text-tail-2) the values are re the stream's mean mark power, and the tail's power
// at a sample is the value times M-bar, the stream's recent mark power (EchoEstimator::level_fs2), whatever the level.
struct EchoShape {
    bool on = false;
    bool absolute = false;
    // at the fall window's positions m = 5 ... 10: added to the template's variance v_m (absolute: to the noise)
    std::array<double, kEdgeCount> in_fall{};
    // at the positions m = 11 ... 10 + tail.size() after the claimed fall
    std::vector<double> tail;
};

// Learns a stream's tail from its envelope (text-fast-1), as kz4ap-fading measures it: a run is the samples above
// echo_run_db re the floor; for a run of at least echo_min_run_s, its median power M and its last sample at or above M / 4
// (-6 dB re the mark), and from there the mean of (e^2 - floor) / M at each later sample until the next run begins, kept with
// an exponential memory in runs. The last -6 dB sample stands at the fall template's half point, sample h after the claimed fall
// (h the last m with 1 - s_m >= 0.5), so position m after the claim reads the mean at m - h. The tail is on once its value at
// m = 11 (the first sample past the fall's window) is at least echo_on_db; it is kept where at least echo_end_db; the fall's
// window takes that first value at every position, the tail's start being flat (the clips: -10 dB re the mark from 4 to 12 ms).
class EchoEstimator {
public:
    EchoEstimator(const SegmentalConfig& config, const viterbi::TemplateTables& tables);
    // One sample: e^2 and the floor, FS^2. True when the shape has changed (every echo_min_runs / 3 runs at most once it is in).
    bool push(double e2_fs2, double floor_fs2);
    const EchoShape& shape() const { return shape_; }
    double runs() const { return runs_; }
    // FS^2: M-bar, the mean of the strong runs' median e^2 with a memory of echo_level_runs runs (text-tail-2); 0 before the first.
    double level_fs2() const { return level_mean_; }
    // Whether a shape has been made yet (on or off): from echo_min_runs strong runs.
    bool decided() const { return decided_; }

private:
    void end_run(std::int64_t end);
    void close_profile();
    void update_shape();

    bool on_ = false;
    double run_ratio_ = 10.0;
    double min_level_ = 100.0;  // re the floor
    std::size_t min_run_ = 12;
    double forget_ = 1.0;
    double min_runs_ = 30.0;
    double on_ratio_ = 0.0, end_ratio_ = 0.0;
    int half_ = 7;
    int positions_ = 0;  // tail positions at most
    int span_ = 0;       // profile samples read after the last -6 dB sample
    std::int64_t k_ = 0;
    bool in_run_ = false;
    std::vector<double> run_;  // the run's e^2 - floor ... and e^2, FS^2
    std::vector<double> run_floor_;
    // the profile being read: from sample `profile_from_`, its level M
    bool profiling_ = false;
    std::int64_t profile_from_ = 0;
    double profile_level_ = 0.0;
    std::vector<double> profile_;
    std::vector<double> sum_, count_;
    // absolute (text-tail-2): the tail's power (e^2 - floor, FS^2) and the runs' level M (FS^2), summed by position
    bool absolute_ = false;
    std::vector<double> power_sum_, level_sum_;
    double level_forget_ = 0.0, level_mean_ = 0.0, level_min_ = 100.0;
    double runs_ = 0.0;
    int since_update_ = 0;
    bool fresh_ = false;
    bool decided_ = false;
    EchoShape shape_;
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
    // a^2 / sigma_y^2 at table index i (level i - below() re sigma^2, the bottom clamp applied), the terms' f'^2, and the
    // template's s_m and v_m at m = 5 ... 10, for terms a stream builds itself (LevelEvidence::set_echo).
    int table_levels() const { return table_levels_; }
    double r2(int table_index) const { return r2_[static_cast<std::size_t>(table_index)]; }
    double f2() const { return f2_; }
    double edge_s(int position) const { return edge_s_[static_cast<std::size_t>(position)]; }
    double edge_v(int position) const { return edge_v_[static_cast<std::size_t>(position)]; }
    static Term make_term(double r2, double s, double f2, double v_m);

private:
    std::vector<double> r2_;
    double f2_ = 0.0;
    std::array<double, kEdgeCount> edge_s_{}, edge_v_{};
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

// The channel's Doppler spread from the widest filter's complex output (text-fast-1). A run is the samples above echo_run_db re
// the floor; within a run, in each stretch of samples at or above -10 dB re its median power, from doppler_end_guard_s inside its
// ends (past the front filter's edges; a gap a tail fills splits the run), the pairs k, k + tau for tau = 3 ... 8 samples (8 to
// 21.33 ms at the widest filter) give, per lag,
//     C(tau) = sum (y[k + tau] y*[k] - F rho_n(tau)),  |rho(tau)| = |C(tau)| / sqrt(sum (|y[k]|^2 - F) sum (|y[k + tau]|^2 - F)),
// rho_n the front filter's noise correlation (OutputNoise::rho; derived), kept with an exponential memory in runs; and the Gaussian
// spectrum's |rho(tau)| = exp(-2 pi^2 sigma_D^2 tau^2) gives f_D = 2 sigma_D (Watterson's and DeepCW's width) at tau = 8.
// Two checks tell a spread from what reads as one:
// - the shape: for a spread ln |rho(8)| / ln |rho(tau)| = (8 / tau)^2 at every lag; a neighbor's beat inside the filter, ln |rho| ~
//   r (cos 2 pi df tau - 1), cannot follow that at all five lags 3 ... 7 at once beyond about 28 Hz (derived; shape_ok());
// - each run's own: a constant frequency offset turns every product of a run alike and leaves its |C|, but offsets that differ
//   between the stations a channel holds in turn (a run station and his callers) turn the runs' C apart and read as a spread when
//   the runs are summed as vectors; summed as each run's |C| they do not (runs_hz).
// The level is the runs' median power re the floor, dB, averaged with the same memory.
class DopplerEstimator {
public:
    // noise_rho: the front filter's noise correlation at lags 0 ... 6 samples (OutputNoise::rho).
    DopplerEstimator(const SegmentalConfig& config, const std::array<double, 7>& noise_rho);
    // One sample: y, FS, and the floor, FS^2. True at the end of every run the estimate takes, once it holds doppler_min_runs.
    bool push(std::complex<double> y_fs, double floor_fs2);
    // Hz: f_D = 2 sigma_D at the longest lag, from the runs summed as vectors; and from each run's |C| summed: 0.62 to 0.79 of it for
    // a Gaussian spread of 3 to 30 Hz (measured, runs of 100 ms), about 0 where stations a few Hz apart take turns in the channel.
    double spread_hz() const { return spread_hz_; }
    double runs_hz() const { return runs_hz_; }
    // Whether ln |rho(8)| / ln |rho(tau)| lies within [lo, hi] times (8 / tau)^2 at every lag tau = 3 ... 7.
    bool shape_ok(double lo, double hi) const;
    // The ratio at a lag over its Gaussian value, 1 for a Gaussian spread; 0 where it cannot be read.
    double shape(int tau) const;
    double level_db() const { return level_db_; }
    // runs: the estimator's memory's weight, at most 1 / (1 - forget).
    double runs() const { return runs_; }
    std::int64_t taken() const { return taken_; }

private:
    static constexpr int kFirstLag = 3, kLags = 6;  // tau = 3 ... 8 samples at the widest filter
    // True if the run entered the estimate.
    bool end_run();
    bool on_ = false;
    double run_ratio_ = 10.0;
    int guard_ = 3;
    double interval_s_ = 0.0;
    double forget_ = 1.0, min_runs_ = 20.0;
    std::array<double, kLags> noise_rho_{};
    bool in_run_ = false;
    std::vector<std::complex<double>> run_;
    std::vector<double> run_floor_;
    std::array<std::complex<double>, kLags> cross_{};
    std::array<double, kLags> power0_{}, power1_{}, rho_{};
    double cross_runs_ = 0.0;  // the runs' |C(8)|, each run's own, summed
    double level_sum_ = 0.0, runs_ = 0.0;
    std::int64_t taken_ = 0;
    double spread_hz_ = 0.0, runs_hz_ = 0.0, level_db_ = -1e9;
};

class LevelEvidence {
public:
    LevelEvidence(const SegmentalConfig& config, const LevelTables& tables, RunningSums& sums);

    // One envelope sample: e_fs, FS; sigma2_fs2, the noise at the front filter's output, FS^2; floor_fs2, the channel's noise
    // floor there, FS^2, at most sigma2_fs2, what the level's grid is relative to (SegmentalConfig::level_on_floor). With
    // s = 10 log10(sigma^2 / floor) / step, unrounded: below LevelTables::below() steps, shift = the nearest whole number to s
    // (so below() itself for s in [below() - 0.5, below())) and sigma^2 is taken at the floor times 10^(shift x step / 10); from
    // below() steps on, shift = below() and sigma^2 is taken as it is. Level l is scored as level l - shift of the tables.
    // Without a floor, or with level_on_floor 0, the floor is sigma^2.
    // unknown: the impulse blanker's (impulse_blanker.hpp): the sample holds no evidence, key-down and key-up alike (its key-down
    // term 1 in the level's messages, 0 nats in every score the search reads); e_fs is not read.
    void push(double e_fs, double sigma2_fs2, double floor_fs2, bool unknown = false);
    void push(double e_fs, double sigma2_fs2) { push(e_fs, sigma2_fs2, sigma2_fs2); }
    // The stream's end: commits every sample left with the look-ahead there is, then 17 samples (kHorizon + 1) of no evidence,
    // so that the search can reach the last sample.
    void finish();
    // The channel's tail from here on (EchoEstimator): the fall's terms with its variance and the tail's terms after the fall.
    // An empty shape takes it out.
    void set_echo(const EchoShape& shape);
    // FS^2: M-bar for the samples pushed from here on, with an absolute tail (text-tail-2).
    void set_echo_level(double level_fs2) { echo_level_fs2_ = level_fs2; }
    // The level's walk from the next block on (text-fast-1; DopplerEstimator): fast, frames of level_fast_frame_s each a block and
    // a walk of walk_nepers_per_sqrt_s; or not, frames joined into blocks of level_frame_s and the configured walk, as before.
    // short_frames false keeps the frame at level_frame_s for now (the variant doppler_hold_frames: the tail not yet decided).
    void set_walk(bool fast, double walk_nepers_per_sqrt_s, bool short_frames = true);
    bool fast() const { return fast_; }

    // The level's step between frames, on a vector over the grid's levels: forward x' = J K x, backward its transpose
    // x' = K^T J x, K the walk (the three-tap passes, or LevelTables::walk_kernel when the walk depends on the level) and J the
    // jump, (1 - jump) I + jump / levels, which is symmetric. With the three-tap passes K is symmetric and its rows sum to 1, so
    // J and K commute and the step is the same both ways. Public for the tests.
    void step_level(double* x, bool backward = false) const { step_kind(x, 1, backward); }

private:
    void end_frame();
    void commit(bool all);
    // The step at a block's end, kind 1 (the configured walk over level_frame_s, step_level's), 2 (the fast walk over one frame) or 3
    // (the fast walk over level_frame_s): forward x' = J K x, backward x' = K^T J x, K the walk (the passes or a matrix) and J the jump.
    void step_kind(double* x, int kind, bool backward = false) const;
    void commit_frame(std::int64_t frame, const double* belief);

    const LevelTables& tables_;
    RunningSums& sums_;
    int levels_;
    int frame_samples_;  // a frame's samples: level_fast_frame_s's, a whole share of level_frame_s's
    int slow_block_;     // frames to a block when not fast: level_frame_s / the frame
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
    std::vector<std::uint8_t> unknown_;  // by sample in the ring: no evidence (push's unknown)
    std::vector<double> top_;        // by sample in the ring: the largest of 0 and w z^2 + x_k(l), nats
    std::vector<double> beta_;       // backward message: key up by level, then key down by level
    std::vector<double> belief_;     // by frame in the ring, then level: what its samples read
    // Blocks of frames over which the level is held (text-fast-1): by frame in the ring, the block's first frame and the step at
    // the frame's end (0 none, 1 slow, 2 fast); the block being filled; the mode and the fast walk's three-tap passes.
    std::vector<std::int64_t> block_start_;
    std::vector<std::uint8_t> step_after_;
    std::int64_t block_first_ = 0;
    int block_fill_ = 0;
    int block_frames_ = 1;
    bool fast_ = false, fast_next_ = false;
    bool short_ = true, short_next_ = true;
    double fast_walk_ = 0.0, fast_walk_next_ = 0.0;
    double fast_jump_ = 0.0;
    double fast_block_jump_ = 0.0;
    // the fast walk over a frame and over level_frame_s (where a tail keeps the frame long), as K[to][from]
    std::vector<double> fast_kernel_, fast_block_kernel_;
    double frame_s_ = 0.0;  // s
    std::vector<double> beta_block_;  // the backward message at the end of the block being read back, key summed out

    // The tail (set_echo): on, its fall terms by position and table level, and its tail terms (rho / (1 + rho), ln(1 + rho)) by
    // position and table level.
    bool echo_on_ = false;
    bool echo_absolute_ = false;
    double echo_level_fs2_ = 0.0;
    double echo_margin_ = 1e300;  // 10^(echo_level_margin_db / 10)
    std::vector<float> echo_scale_;  // by sample in the ring: M-bar / sigma^2 (absolute tail)
    std::array<double, kEdgeCount> echo_in_fall_{};
    std::vector<double> echo_tail_;
    int echo_positions_ = 0;
    std::vector<LevelTables::Term> echo_fall_;
    std::vector<float> echo_gain_, echo_cost_;
    double tail_scratch_[kTailMax]{};
};

}  // namespace kz4ap::segmental
