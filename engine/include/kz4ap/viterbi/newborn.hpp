// The newborn integral (design notes, "Detection, amplitude, frequency and the mark's shape", part 8 as amended
// 2026-10-26 (D22), 2026-10-29 (D23) and 2026-10-30 (D24); plan 1b, Tasks 9 and 10). The decoder runs it (search.hpp;
// viterbi_decoder.hpp).
//
// A newborn story's first mark is scored against the owner's prior on its level, flat in u = ln(a / 1 FS) over
// [u_lo, u_hi] (-15 to +85 dB S500, 11.51 nepers; the bounds fixed from N0 at the story's birth, a = sqrt(10^(L/10) N0
// x 500 Hz); plan 1b, O10), until its belief is narrow enough to switch to N(mu, P). Its score at sample k is
// ln Z_k - ln Z_(k-1), Z_k the integral over the prior of the belief after sample k, Z_0 = 1. Each scored sample
// multiplies the belief at amplitude a by exp(w l_j(a)), with part 4's full density against the reference shared by
// every story (D23):
//
//     l_j(a) = ln p(e_j | a, V_j(a)) - ln p0(e_j | sigma_y^2)
//            = -ln(V_j / sigma_y^2) - (e_j^2 + s_j^2 a^2) / V_j + e_j^2 / sigma_y^2 + ln I0(2 e_j |s_j| a / V_j)  (nats),
//     V_j(a) = sigma_y^2 + a^2 (f^2 s_j^2 + v_j)                                                            (FS^2),
//
// e_j the envelope sample (FS), s_j the story's template and v_j its edge term (1; |s| is used where the sign would
// enter, N5), f the systematic floor (1; 0.02, placeholder) and sigma_y^2 the front filter's output noise power (FS^2):
// the systematic variance at the amplitude itself (part 9). w is one constant for every story and sample, the newborn
// included: the strong limit w_strong = 1 / (1 + 2 sum_m rho_m) of the output noise (0.1987 on the decided chain, the
// design's 0.1986; derived; D23).
//
// 1. The grid (D22; the point-mass method of Bucy and Senne 1971). N points u_i = u_lo + i h, i = 0 ... N - 1, even over
//    the prior, h = (u_hi - u_lo) / (N - 1): N = 128, h = 11.51 / 127 = 0.0906 nepers (0.79 dB). Each point holds the
//    log of its unnormalized belief F_i, and Z_k is the trapezoid sum
//        Z_k = h p0 sum_i c_i exp(F_i) = (1 / (N - 1)) sum_i c_i exp(F_i),   c_0 = c_(N-1) = 1/2, else c_i = 1,
//    computed as a log-sum-exp about the largest F_i. 128 is heuristic with a derived bound and a measured check: the
//    trapezoid rule's relative error on a belief of width sigma (in u) is about 2 exp(-2 pi^2 sigma^2 / h^2) (Poisson
//    summation; derived), 5e-3 at sigma = 0.05 nepers. Without the caps (removed by D23) the floor alone limits one
//    sample's precision, to about f / sqrt(2w) = 0.03 nepers (rough), where 128 points err about 0.17 nats; the build
//    records the narrowest sigma_u at handover, and one below 0.05 nepers goes to the owner (part 8; D23).
//    Where the belief is cut by the prior's top (a station near +85 dB S500), the error is instead the end-point term of
//    Euler-Maclaurin, about h^2 |F'(u_hi)| / 12 (derived), which the interior bound does not cover.
// 2. The live range (D22; thresholded support as in Simandl, Kralovec and Soderstrom 2006). Only the points lo ... hi are
//    evaluated; after each sample the range's end points more than live_range_nats (160 nats, heuristic: about twice the
//    deepest revival measured on correctly timed stories under fading, 67.9 nats at 5 Hz) below the largest F_i are
//    dropped for good, so [lo, hi] only shrinks. A dropped point counts as 0 in Z from then on.
//    KNOWN LIMITATION (measured, Task 10, 200 first marks per case, 25 WPM dash): on stories that claim their key-down
//    late the grid is not the flat prior's integral. At 160 nats a story claimed 2 samples late at +40 dB S500 loses up
//    to 97 nats and one claimed 3 late up to 224 nats (the live range only lowers a misaligned story's score, since
//    dropping points only removes mass). The grid's own error, which no live range removes, has both signs: up to
//    0.03 nats per sample 1 sample late at +40 and +60 dB S500, 0.4 nats 2 late and 3.4 nats 3 late at +60 dB S500 (the
//    belief at the prior's top edge). Correctly timed claims stay within 8.3e-4 nats per sample with a live-range loss
//    below 1e-7 nats on 40 000 first marks under fading of 0 to 5 Hz, and over 6 000 pairs the error never changed the
//    order of an aligned claim and a claim one sample off (Newborn.AlignedClaimsStayAheadWithinTheTie).
// 3. The tables (derived, their error measured). With r_i = V_j(a_i) / sigma_y^2 = 1 + gamma e^(2 i h),
//    gamma = a_lo^2 (f^2 s^2 + v) / sigma_y^2:
//    - ln r_i = softplus(ln gamma + 2 i h), softplus(x) = ln(1 + e^x), tabulated at steps of h over
//      [-40 - 2 span, 40 + 2 span] and read by 4-point Lagrange interpolation with one fraction for every point (the
//      design's proposal, part 8 as amended by D23); below -40 at every point r_i = 1 and above +40 ln r_i = x (each
//      within e^-40 = 4e-18);
//    - 1 / r_i by one division per point, x_i = 2 e |s| a_i / V_j(a_i) = (2 e |s| a_lo / sigma_y^2) e^(i h) / r_i;
//    - ln I0(x) = x - (1/2) ln x + G(ln x), ln x_i = ln(2 e |s| a_lo / sigma_y^2) + i h - ln r_i, no longer one shift for
//      every point (since D23), so G is read per point: G(y) = ln I0(e^y) - e^y + y / 2 tabulated at steps of h/8 =
//      0.0113 nepers of ln x over [-20, +165] (extended below by the span, and above to at least the span plus 40, where
//      G is constant to double precision), by linear interpolation at each point's own fraction (heuristic, for CPU:
//      4.3e-6 nats against ln_i0 over x of 1e-3 to 1e6, measured; Task 9's 4-point Lagrange at h/4, within 1e-6, cost
//      about 235 ns more per newborn-sample read per point, measured in Task 10's Step 0). ln r_i goes to a scratch
//      buffer in a loop of its own before the evidence's. G comes from
//      ln_i0 (observation.hpp) up to x = 10^6 and from the large-x series ln I0(x) = x - (1/2) ln(2 pi x) +
//      ln(1 + 1/(8x) + 9/(128 x^2)) (Abramowitz and Stegun 9.7.1) above, where ln_i0(x) - x loses digits to rounding
//      (the two meet within 1e-9 nats at 10^6). Where x_i < 1e-3, ln I0(x) = x^2 / 4 (error below 1e-13 nats).
//    The term w e^2 / sigma_y^2, the same at every point, is summed apart from F_i (common_nats), so that F_i stays
//    small near the belief's peak at strong levels (e^2 / sigma_y^2 is 6.7e6 per sample at +60 dB S500).
//    Against direct evaluation (ln_i0 and std::log at each point) the tests bound them at 5e-4 nats per sample and 0.005
//    nats per first mark (Newborn.TablesMatchDirectEvaluation).
// 4. The prediction step (D24; the point-mass filter's prediction, Bucy and Senne 1971; Kushner and Dupuis 2001, the
//    Markov-chain approximation). On every interval-th scored sample (s != 0) of the newborn, counted from its birth,
//    after the sample's moments and before the live-range trim, the belief in u is diffused by Kushner's three-point
//    chain on p_i = e^(F_i - m) (m the largest F_i, written by the moments loop into a scratch buffer):
//        p_i <- p_i + eps (p_(i-1) - 2 p_i + p_(i+1)),   then F_i = m + ln p_i,
//    with the reflecting ghost mirrored about the end node at the grid's ends (p_-1 = p_1, p_N = p_(N-2): the trapezoid
//    mass, ends halved, and Z are conserved exactly; derived) and zeros outside the live range;
//        eps = interval x var / (2 h^2),   var = (1/2)(1 - rho(Delta)^2)   (nepers^2 per sample; derived),
//    var from the decoder's own FadingTables at lag 1 (Part 6's variance with Omega_bar = a_i^2 at each point:
//    1.404e-4 at 2 Hz), interval 8 samples (21.33 ms; heuristic, chosen by measurement, D24): eps = 0.0684 at 2 Hz. The
//    step is applied as n = ceil(eps / (1/4)) explicit substeps of eps / n each (at eps <= 1/4 no mode of the chain
//    changes sign and every p_i stays positive; at exactly 1/2 an isolated point goes to 0; derived): one at 2 Hz, two
//    of 0.2135 at 5 Hz. Part 6's drift (toward 0.886 a_i) and the Ito term are dropped (about -1.1e-4 nepers per sample,
//    derived). A handover between two steps applies none of the variance accumulated since the last one (at most 7
//    samples' worth), and the moments handed over are those before a step due on the handover sample, as in the
//    newborn-fading study's code. The scratch buffer is per thread: the per-newborn state is unchanged.
// 5. The switch (owner, 2026-10-24 and 2026-10-25). After each sample the grid's own posterior mean u^ and standard
//    deviation sigma_u in u (trapezoid moments over the live points, before a step due on that sample); the newborn
//    switches when sigma_u < 0.1 nepers or, at the latest, at the last scored sample of its first mark's template, j = 15
//    after the claimed key-up (should_switch), to N(mu, P) by moment matching of the log-normal: mu = e^(u^ + sigma_u^2
//    / 2), P = mu^2 (e^(sigma_u^2) - 1) (derived; below sigma_u = 0.1 the log-normal's skew costs about (3 sigma_u)^2 /
//    12 = 0.008 nats against the matched Gaussian, an Edgeworth estimate).
// 6. Two counters (D22), for the statistics: the narrowest sigma_u (at handover, since sigma_u >= 0.1 before it) and the
//    deepest revival, the largest depth in nats below the largest F_i from which a point later came back to within
//    10 nats of it.
//
// The state per newborn: N values F_i (1 KB at 128 points in double precision), the live range, ln Z, the moments, the
// counters, the scored-sample count and the depth each point has fallen to (N floats, for the revival counter). A
// NewbornIntegral is a value; the search's Story holds it through a copy-on-write handle.
#pragma once

#include "kz4ap/viterbi/amplitude.hpp"
#include "kz4ap/viterbi/front_filter.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kz4ap::viterbi {

inline constexpr int kNewbornGridPoints = 128;           // N (heuristic, derived bound, measured check; D22)
inline constexpr double kNewbornLiveRangeNats = 160.0;   // nats (heuristic, measured margin; decision log, 2026-10-30)
// The prior's span (nepers): 100 dB (-15 to +85 dB S500, placeholders) of amplitude, 100 ln(10) / 20 = 11.5129.
inline constexpr double kNewbornPriorSpanNepers = 5.0 * 2.302585092994045684;
inline constexpr double kNewbornSmallX = 1e-3;           // 1: below it, ln I0(x) = x^2 / 4
inline constexpr double kNewbornGLowestLnX = -20.0;      // nepers of ln x: the G table's bottom (at the default span)
inline constexpr double kNewbornGHighestLnX = 165.0;     // nepers of ln x: the G table's top (at the default span)
inline constexpr double kNewbornSoftplusReach = 40.0;    // 1: softplus is e^x below -40 and x above +40 (to 4e-18)
inline constexpr double kNewbornRevivalNats = 10.0;      // nats: "came back" means to within this of the largest
inline constexpr double kNewbornLargestSubstep = 0.25;   // 1: the prediction step's largest eps per substep (derived)

// The tables every newborn of a configuration shares (built once, in ViterbiShared).
class NewbornTables {
public:
    // noise: the front filter's output noise (front_filter.hpp), whose w_strong is the constant w; sigma2_per_n0_hz:
    // sigma_y^2 / N0 (Hz); points: N, the grid's points (kNewbornGridPoints); span_nepers: u_hi - u_lo, the prior's width
    // (nepers; kNewbornPriorSpanNepers). Throws std::invalid_argument for a sigma2_per_n0_hz or noise.sigma2_per_n0_hz
    // that is not positive and finite, a w_strong that is not positive and finite, points below 2 or a span that is not
    // positive and finite.
    NewbornTables(const OutputNoise& noise, double sigma2_per_n0_hz, int points = kNewbornGridPoints,
                  double span_nepers = kNewbornPriorSpanNepers);

    int points() const { return points_; }
    double span_nepers() const { return span_; }
    double step_nepers() const { return step_; }  // h
    double weight() const { return w_; }          // w (1), noise.w_strong
    double sigma2_per_n0_hz() const { return sigma2_per_n0_hz_; }
    const OutputNoise& noise() const { return noise_; }

    // For tests: ln I0(x) through the G table, x - (1/2) ln x + G(ln x) read as the integral reads it (4-point Lagrange
    // at y = ln x); x^2 / 4 below kNewbornSmallX. x must be positive and finite.
    double ln_i0_from_table(double x) const;
    // For tests: softplus(x) = ln(1 + e^x) through its table (4-point Lagrange), as the integral reads it; e^x below
    // -40 - 2 span and x above 40 + 2 span. x must be finite.
    double softplus_from_table(double x) const;

private:
    friend class NewbornIntegral;

    int points_;
    double span_;
    double step_;
    double w_;
    double ln_intervals_ = 0.0;  // ln(N - 1): Z = (1 / (N - 1)) sum_i c_i exp(F_i)
    double sigma2_per_n0_hz_;
    OutputNoise noise_;
    // G at y_j = g_lo_ + j g_step_, g_step_ = h / 4.
    std::vector<double> g_;
    double g_lo_ = 0.0;
    double g_step_ = 0.0;
    // softplus at x_j = sp_lo_ + j h.
    std::vector<double> sp_;
    double sp_lo_ = 0.0;
    // e^(i h) and e^(2 i h), i = 0 ... N - 1: a_i = a_lo e^(i h).
    std::vector<double> e1_;
    std::vector<double> e2_;
};

// The prediction step's settings (header, 4), given to a newborn at its birth: every interval_samples-th scored sample,
// the per-sample variance var_per_sample_nepers2 in u. interval_samples = 0: no step (the grid of D23 alone; tests).
struct NewbornFade {
    int interval_samples = 0;             // samples (8 by default: newborn_fade_interval_s / sample_interval_s)
    double var_per_sample_nepers2 = 0.0;  // nepers^2: (1/2)(1 - rho(Delta)^2) from the decoder's FadingTables
};

// One newborn's integral. Copyable (its successors inside the first mark each get a copy; the search's copy on write).
class NewbornIntegral {
public:
    // tables: shared, must outlive the integral; u_lo, u_hi: the prior's bounds, ln(a / 1 FS) (nepers); points: N;
    // live_range_nats: the live range (nats; +infinity keeps every point); fade: the prediction step (none by default).
    // Throws std::invalid_argument for bounds that are not finite or not increasing, points or a width u_hi - u_lo other
    // than the tables' (within 1e-9 of the span, relatively), a live range that is not positive, a negative interval or
    // a variance that is negative or not finite.
    NewbornIntegral(const NewbornTables& tables, double u_lo, double u_hi, int points, double live_range_nats,
                    NewbornFade fade = {});

    // Adds sample k: e_fs, the envelope (FS); s, the story's template (1; any sign, |s| used); v, its edge term (1);
    // sigma2_fs2, sigma_y^2 (FS^2); floor, f (1). s = 0 adds no evidence (V = sigma_y^2 and l = 0 at every a): the score
    // is 0 and nothing else changes, nor is the sample counted. Throws std::invalid_argument for an e that is negative or
    // not finite, an s that is not finite, a v or floor that is negative or not finite, or a sigma_y^2 that is not
    // positive and finite.
    void add(double e_fs, double s, double v, double sigma2_fs2, double floor);

    // The last sample's score, ln Z_k - ln Z_(k-1) (nats); 0 before any sample.
    double last_score_nats() const { return last_score_; }
    // ln Z_k (nats), the sum of the scores so far.
    double ln_z() const { return ln_z_; }
    // The posterior's mean and standard deviation in u (nepers), the grid's trapezoid moments over the live points before
    // a step due on the last sample; before any sample, the prior's.
    double mean_u() const { return mean_u_; }
    double sigma_u() const { return sigma_u_; }
    // Moment matching (header, 5): mu = e^(u^ + sigma_u^2 / 2) (FS), P = mu^2 (e^(sigma_u^2) - 1) (FS^2), t_m = k.
    Amplitude to_gaussian(std::int64_t k) const;
    // The narrowest sigma_u so far (nepers; +infinity before any sample) and the deepest revival (nats; 0 if none).
    double narrowest_sigma_u() const { return narrowest_sigma_u_; }
    double deepest_revival_nats() const { return deepest_revival_; }
    // True when sigma_u < switch_sigma_u (nepers; 0.1) or at the last scored sample of the first mark's template (j = 15
    // after the claimed key-up; the caller knows it from its EdgeHistory). Called after each add (Ruling 72, N11).
    bool should_switch(double switch_sigma_u, bool last_scored_sample_of_first_mark) const;
    // The live points, hi - lo + 1, and the range's ends.
    int live_points() const { return hi_ - lo_ + 1; }
    int live_lo() const { return lo_; }
    int live_hi() const { return hi_; }
    // F_i (nats), the log of each point's unnormalized belief less common_nats(), the term every point shares (points
    // outside the live range are stale): ln Z = ln((1 / (N - 1)) sum_i c_i e^(F_i)) + common_nats().
    const std::vector<double>& log_belief() const { return f_; }
    double common_nats() const { return common_; }
    // The scored samples so far (s != 0), the steps applied so far, and whether the last add applied one.
    int scored_samples() const { return scored_; }
    int steps_applied() const { return steps_; }
    bool last_add_stepped() const { return last_stepped_; }
    // The step's eps per substep and its substeps (0 substeps: no step).
    double fade_epsilon_per_substep() const { return eps_; }
    int fade_substeps() const { return substeps_; }
    // bytes: the integral's own size and its grid's storage (about 1.6 KB at 128 points: 1 KB of F_i, 0.5 KB of depths).
    std::size_t bytes() const {
        return sizeof(*this) + f_.capacity() * sizeof(double) + depth_.capacity() * sizeof(float);
    }

private:
    void predict(double* p);  // header, 4: p = the scratch buffer's point 0, with one pad before and after

    const NewbornTables* tables_;
    double u_lo_;
    double h_;
    double a_lo_;            // e^(u_lo), FS
    double live_range_;
    int lo_;
    int hi_;
    int interval_ = 0;       // samples; 0: no step
    int substeps_ = 0;
    double eps_ = 0.0;       // per substep
    int scored_ = 0;
    int steps_ = 0;
    bool last_stepped_ = false;
    double common_ = 0.0;    // nats: sum_j w e_j^2 / sigma_y^2, the term every point shares (header, 3)
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
