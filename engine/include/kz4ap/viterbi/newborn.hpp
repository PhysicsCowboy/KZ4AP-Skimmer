// The newborn integral (design notes, "Detection, amplitude, frequency and the mark's shape", part 8 as amended
// 2026-10-26, D21 and D22; plan 1b, Task 9). Library code: the decoder does not run it yet.
//
// A newborn story's first mark is scored against the owner's prior on its level, flat in u = ln(a / 1 FS) over
// [u_lo, u_hi] (-15 to +85 dB S500, 11.51 nepers; the bounds fixed from N0 at the story's birth, a = sqrt(10^(L/10) N0
// x 500 Hz); plan 1b, O10), with the amplitude held constant, until its belief is narrow enough to switch to N(mu, P).
// Its score at sample k is ln Z_k - ln Z_(k-1),
//
//     Z_k = integral exp(sum_(j<=k) w_j(a) Lambda_j(a)) p0(u) du,      p0(u) = 1 / (u_hi - u_lo),
//     Lambda_j(a) = -b_j^2 + ln I0(2 b_j z_j),   b_j = |s_j| a / sqrt(V'_j),   z_j = e_j / sqrt(V'_j)      (part 4),
//     w_j(a) = w(q),   q = a^2 s_j^2 / sigma_y^2                                                       (part 4; D21),
//
// e_j the envelope sample (FS), s_j the story's template (1; its sign does not matter, |s| is used, N5), V'_j the
// sample's error budget with the systematic term (FS^2; systematic.hpp) and sigma_y^2 the front filter's output noise
// power (FS^2). Z_0 = 1. The weight is the curve of weight.hpp evaluated at each amplitude of the integral, the rule of an
// ordinary story evaluated at the parameter (D21; or one constant w in the fixed-weight variant, O12).
//
// 1. The grid (D22; the point-mass method of Bucy and Senne 1971). N points u_i = u_lo + i h, i = 0 ... N - 1, even over
//    the prior, h = (u_hi - u_lo) / (N - 1): N = 128, h = 11.51 / 127 = 0.0906 nepers (0.79 dB). Each point holds the
//    log of its unnormalized belief, F_i = sum_j w_j(a_i) Lambda_j(a_i), and Z_k is the trapezoid sum
//        Z_k = h p0 sum_i c_i exp(F_i) = (1 / (N - 1)) sum_i c_i exp(F_i),   c_0 = c_(N-1) = 1/2, else c_i = 1,
//    computed as a log-sum-exp about the largest F_i. 128 is heuristic with a derived bound and a measured check: the
//    trapezoid rule's relative error on a belief of width sigma (in u) is about 2 exp(-2 pi^2 sigma^2 / h^2) (Poisson
//    summation; derived), 4e-7 at sigma = 0.08, the narrowest measured at handover; measured on 36 000 first marks
//    (noise and -10 to +40 dB S500) at most 0.003 nats per sample against a dense integral (the newborn study, 2026-10-26).
//    Where the belief is cut by the prior's top (a station near +85 dB S500), the error is instead the end-point term of
//    Euler-Maclaurin, about h^2 |F'(u_hi)| / 12 (derived), which the interior bound does not cover.
// 2. The live range (D22; thresholded support as in Simandl, Kralovec and Soderstrom 2006). Only the points lo ... hi are
//    evaluated; after each sample the range's end points more than live_range_nats (80 nats, heuristic: about twice the
//    deepest revival measured, 41.5 nats) below the largest F_i are dropped for good, so [lo, hi] only shrinks. A dropped
//    point counts as 0 in Z from then on.
// 3. The tables (D22; derived, their error measured). On a grid even in u the per-point arguments are one offset per
//    sample plus a multiple of the step:
//        ln x_i = ln(2 |s| e / V') + u_i,     ln q_i = 2 u_i + ln(s^2 / sigma_y^2),
//    so ln I0 and w at every point are two fixed tables shifted by one offset per sample, with one interpolation fraction
//    for all points (no logarithm, division or polynomial per point):
//    - ln I0(x) = x - (1/2) ln x + G(ln x), G(y) = ln I0(e^y) - e^y + y / 2 tabulated at steps of h/4 = 0.023 nepers of
//      ln x over [-20, +165] (extended below by the span, and above to at least the span plus 40, where G is constant
//      to double precision), read by 4-point Lagrange interpolation. G comes from ln_i0 (observation.hpp) up to
//      x = 10^6 and from the large-x series ln I0(x) = x - (1/2) ln(2 pi x) + ln(1 + 1/(8x) + 9/(128 x^2)) (Abramowitz
//      and Stegun 9.7.1) above, where ln_i0(x) - x loses digits to rounding (the two meet within 1e-9 nats at 10^6).
//      Where every live point has x < 1e-3, ln I0(x) = x^2 / 4 (error below 1e-13 nats).
//    - w tabulated against ln q at steps of 2h = 0.18 from sample_weight (Task 5's 129-node xi table), read by linear
//      interpolation; the table reaches past the curve's clamps at q = 1e-4 and 1e4 by the grid's span, beyond which w
//      is constant.
//    Against direct evaluation (ln_i0 and sample_weight at each point): at most 3.1e-4 nats per sample and 0.0037 nats
//    per first mark (measured in the study, heuristic steps).
// 4. The switch (owner, 2026-10-24 and 2026-10-25). After each sample the grid's own posterior mean u^ and standard
//    deviation sigma_u in u (trapezoid moments over the live points); the newborn switches when sigma_u < 0.1 nepers or,
//    at the latest, at the last scored sample of its first mark's template, j = 15 after the claimed key-up (should_switch),
//    to N(mu, P) by moment matching of the log-normal: mu = e^(u^ + sigma_u^2 / 2), P = mu^2 (e^(sigma_u^2) - 1) (derived;
//    below sigma_u = 0.1 the log-normal's skew costs about (3 sigma_u)^2 / 12 = 0.008 nats against the matched Gaussian,
//    an Edgeworth estimate).
// 5. Two counters (D22), for the statistics: the narrowest sigma_u (at handover, since sigma_u >= 0.1 before it) and the
//    deepest revival, the largest depth in nats below the largest F_i from which a point later came back to within
//    10 nats of it.
//
// The state per newborn: N values F_i (1 KB at 128 points in double precision), the live range, ln Z, the moments, the
// counters and the depth each point has fallen to (N floats, for the revival counter). A NewbornIntegral is a value;
// Task 10's Story holds it through a copy-on-write handle.
#pragma once

#include "kz4ap/viterbi/amplitude.hpp"
#include "kz4ap/viterbi/front_filter.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace kz4ap::viterbi {

inline constexpr int kNewbornGridPoints = 128;           // N (heuristic, derived bound, measured check; D22)
inline constexpr double kNewbornLiveRangeNats = 80.0;    // nats (heuristic, measured margin; D22)
// The prior's span (nepers): 100 dB (-15 to +85 dB S500, placeholders) of amplitude, 100 ln(10) / 20 = 11.5129.
inline constexpr double kNewbornPriorSpanNepers = 5.0 * 2.302585092994045684;
inline constexpr double kNewbornSmallX = 1e-3;           // 1: below it at every live point, ln I0(x) = x^2 / 4
inline constexpr double kNewbornGLowestLnX = -20.0;      // nepers of ln x: the G table's bottom (at the default span)
inline constexpr double kNewbornGHighestLnX = 165.0;     // nepers of ln x: the G table's top (at the default span)
inline constexpr double kNewbornRevivalNats = 10.0;      // nats: "came back" means to within this of the largest

// The tables every newborn of a configuration shares (built once, in ViterbiShared, Task 10).
class NewbornTables {
public:
    // noise: the front filter's output noise (front_filter.hpp); sigma2_per_n0_hz: sigma_y^2 / N0 (Hz); fixed_weight: the
    // fixed-weight variant's constant w (1; O12), or none for the curve of weight.hpp; points: N, the grid's points
    // (kNewbornGridPoints); span_nepers: u_hi - u_lo, the prior's width (nepers; kNewbornPriorSpanNepers). The tables
    // depend on the grid (N and its step h = span / (N - 1)) and on fixed_weight only: w comes from the generated xi
    // table (weight.hpp), and the noise's figures are kept for the caller (the interface of plan 1b, Task 9). Throws
    // std::invalid_argument for a sigma2_per_n0_hz or noise.sigma2_per_n0_hz that is not positive and finite, a fixed
    // weight that is not positive and finite, points below 2 or a span that is not positive and finite.
    NewbornTables(const OutputNoise& noise, double sigma2_per_n0_hz, std::optional<double> fixed_weight,
                  int points = kNewbornGridPoints, double span_nepers = kNewbornPriorSpanNepers);

    int points() const { return points_; }
    double span_nepers() const { return span_; }
    double step_nepers() const { return step_; }  // h
    std::optional<double> fixed_weight() const { return fixed_weight_; }
    double sigma2_per_n0_hz() const { return sigma2_per_n0_hz_; }
    const OutputNoise& noise() const { return noise_; }

    // For tests: ln I0(x) through the G table, x - (1/2) ln x + G(ln x) read as the integral reads it (4-point Lagrange
    // at y = ln x); x^2 / 4 below kNewbornSmallX. x must be positive and finite.
    double ln_i0_from_table(double x) const;
    // For tests: w (1) at ln q through the w table (linear interpolation), as the integral reads it.
    double weight_from_table(double ln_q) const;

private:
    friend class NewbornIntegral;

    int points_;
    double span_;
    double step_;
    double ln_intervals_ = 0.0;  // ln(N - 1): Z = (1 / (N - 1)) sum_i c_i exp(F_i)
    std::optional<double> fixed_weight_;
    double sigma2_per_n0_hz_;
    OutputNoise noise_;
    // G at y_j = g_lo_ + j g_step_, g_step_ = h / 4.
    std::vector<double> g_;
    double g_lo_ = 0.0;
    double g_step_ = 0.0;
    // w at ln q = w_lo_ + j w_step_, w_step_ = 2 h.
    std::vector<double> w_;
    double w_lo_ = 0.0;
    double w_step_ = 0.0;
    // e^(i h) and e^(2 i h), i = 0 ... N - 1: a_i = a_lo e^(i h).
    std::vector<double> e1_;
    std::vector<double> e2_;
};

// One newborn's integral. Copyable (its successors inside the first mark each get a copy; Task 10's copy on write).
class NewbornIntegral {
public:
    // tables: shared, must outlive the integral; u_lo, u_hi: the prior's bounds, ln(a / 1 FS) (nepers); points: N;
    // live_range_nats: the live range (nats; +infinity keeps every point). Throws std::invalid_argument for bounds that
    // are not finite or not increasing, points or a width u_hi - u_lo other than the tables' (within 1e-9 of the span,
    // relatively), or a live range that is not positive.
    NewbornIntegral(const NewbornTables& tables, double u_lo, double u_hi, int points, double live_range_nats);

    // Adds sample k: e_fs, the envelope (FS); s, the story's template (1; any sign, |s| used); v_prime_fs2, V' (FS^2);
    // sigma2_fs2, sigma_y^2 (FS^2). s = 0 adds no evidence (Lambda = 0 at every a): the score is 0 and nothing else
    // changes. Throws std::invalid_argument for an e that is negative or not finite, an s that is not finite, or a V' or
    // sigma_y^2 that is not positive and finite.
    void add(double e_fs, double s, double v_prime_fs2, double sigma2_fs2);

    // The last sample's score, ln Z_k - ln Z_(k-1) (nats); 0 before any sample.
    double last_score_nats() const { return last_score_; }
    // ln Z_k (nats), the sum of the scores so far.
    double ln_z() const { return ln_z_; }
    // The posterior's mean and standard deviation in u (nepers), the grid's trapezoid moments over the live points; before
    // any sample, the prior's.
    double mean_u() const { return mean_u_; }
    double sigma_u() const { return sigma_u_; }
    // Moment matching (header, 4): mu = e^(u^ + sigma_u^2 / 2) (FS), P = mu^2 (e^(sigma_u^2) - 1) (FS^2), t_m = k.
    Amplitude to_gaussian(std::int64_t k) const;
    // The narrowest sigma_u so far (nepers; +infinity before any sample) and the deepest revival (nats; 0 if none).
    double narrowest_sigma_u() const { return narrowest_sigma_u_; }
    double deepest_revival_nats() const { return deepest_revival_; }
    // True when sigma_u < switch_sigma_u (nepers; 0.1) or at the last scored sample of the first mark's template (j = 15
    // after the claimed key-up; the caller knows it from its EdgeHistory). Called after each add (Ruling 72, N11).
    bool should_switch(double switch_sigma_u, bool last_scored_sample_of_first_mark) const;
    // The live points, hi - lo + 1.
    int live_points() const { return hi_ - lo_ + 1; }

private:
    const NewbornTables* tables_;
    double u_lo_;
    double h_;
    double a_lo_;            // e^(u_lo), FS
    double live_range_;
    int lo_;
    int hi_;
    double ln_z_ = 0.0;
    double last_score_ = 0.0;
    double mean_u_;
    double sigma_u_;
    double narrowest_sigma_u_;
    double deepest_revival_ = 0.0;
    std::vector<double> f_;       // F_i (nats)
    std::vector<float> depth_;    // the lowest F_i - max_j F_j so far (nats, <= 0), for the revival counter
};

}  // namespace kz4ap::viterbi
