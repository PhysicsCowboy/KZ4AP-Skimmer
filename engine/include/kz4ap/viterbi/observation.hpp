// The evidence per sample (design notes, "Detection, amplitude, frequency and the mark's shape", parts 4 and 9; plan
// 1b, Tasks 5 and 10): how much better a story's carrier explains one envelope sample than noise alone, with the
// story's amplitude uncertain (its Gaussian prediction), and the amplitude after the sample.
//
// A story whose template at sample k is s (1; mark_template.hpp) and whose amplitude is a (FS) explains the envelope
// sample e (FS; front_filter.hpp) against noise alone by the log-likelihood ratio (derived; the Rician envelope of a
// carrier of unknown phase in complex Gaussian noise; Proakis and Salehi, section 4.5-1)
//     Lambda(a) = -b^2 + ln I0(2 b z),   b = |s| a / sqrt(V'),   z = e / sqrt(V')      (nats),
// V' the sample's variance (FS^2; since D23 the story's V_k, systematic.hpp, never divided by w). Lambda is even in b, so a negative s
// (the channel filter's precursor, s_3 and s_4 = -0.00064 and -0.00080) scores as |s|. The sample's score is
//     score = ln integral_0^inf e^(w Lambda(a)) N(a; mu, P) da - ln Phi(mu / sqrt(P))      (nats),
// the prior N(mu, P) (the story's prediction for this sample; amplitude.hpp) truncated to a >= 0 and renormalized, and
// the likelihood tempered by w (one constant, the output noise's w_strong; D23; derived as the composite-likelihood magnitude adjustment). The posterior is
// proportional to e^(w Lambda(a)) N(a; mu, P) on a >= 0; its mean and variance are the story's amplitude after the
// sample (moment matching).
//
// The quadrature (plan 1b, Ruling 60; the first prototype's Rulings 11 and 15), in b, where the prior is N(beta, q),
// beta = |s| mu / sqrt(V'), q = s^2 P / V', and the integrand is e^g(b), g(b) = -(b - beta)^2 / (2 q) + w (-b^2 +
// ln I0(2 b z)):
//   - Newton's method finds the mode b^ (from start_a_fs, the previous sample's mode, when given, clamped into its
//     bracket; else from the bracket's top, the precision-weighted mean of the prior and the likelihood's peak,
//     (beta / q + 2 w z) / (1 / q + 2 w), where g' <= 0 since d ln I0 / dx < 1), with bisection as a safeguard; it
//     stops at a step below the larger of 1e-10 sigma^ and 4 epsilon b;
//   - adaptive Gauss-Hermite of the given order (Liu and Pierce 1994; Pinheiro and Bates 1995) centered at the mode and
//     scaled by the curvature there, sigma^ = (-g''(b^))^(-1/2); where N(b^, sigma^^2) has mass below 0
//     (b^ < 8.5 sigma^), its nodes are those of that Gaussian truncated at 0, node k at b^ + sigma^ Phi^-1(Phi(-tau) +
//     (1 - Phi(-tau)) Phi(t_k)), tau = b^ / sigma^ (an extension of the standard form, exact for a Gaussian integrand
//     however much the truncation cuts off), read from a table in tau (steps of 1/256 from 0 to 8.5, linear
//     interpolation; 17.4 KB per node, 87.1 KB at order 5, built once per order). The rule integrates e^g over that
//     truncated Gaussian's density, its terms summed in the log (log-sum-exp). Dropping the nodes below 0 instead had
//     missed a dense integral by up to 0.355 nats (0.62 with the weights renormalized; measured in the first
//     prototype's Task 2).
//   - A prior known exactly (P = 0, or narrower than 1e-9 of max(beta, 1): a heuristic margin, since the quadrature's
//     error from rounding its nodes' offsets grows as epsilon b / sigma^, 7e-9 nats at that width at b = 10) gives
//     w Lambda(mu) and the prior {mu, P}.
//
// The broad-prior regime (plan 1b, O9; measured). Where the prior is broad against the likelihood the curvature at the
// mode degenerates (with beta = 0 the mode leaves b = 0 where y = sqrt(w) z / s_w passes 1, s_w = sqrt(w + 1/(2 q)),
// and -g'' vanishes there), and curvature-scaled nodes spread far wider than the integrand: without the regime the
// quadrature missed the dense integral by more than 0.01 nats at 60 of Evidence.ScoreMatchesADenseIntegral's 2 800
// points, all at z = 1 with broad priors, by up to 0.216 nats (w = 1, b = 30, relative spread 3), with the posterior's
// mean off by up to 29% and its variance by 78% (measured, Task 5). An exact rescaling serves there instead: with
// u = s_w b,
//     score = ln N(0; beta, q) - ln Phi(beta / sqrt(q)) + ln J_w(z / s_w, kappa / s_w) - ln s_w,
//     J_w(zeta, kappa') = integral_0^inf e^(kappa' u - u^2) I0(2 u zeta)^w du,   kappa = beta / q,
// exact at every q and w (derived), read from tables where y <= 4 and kappa' <= 2: steps of 0.01 in y and kappa'
// (80 601 nodes per node of w), Simpson's rule over u in [0, 12] in steps of 0.02 (the weight is e^-49 of its peak there
// at the far corner, y = 4, kappa' = 2, w = 1), bilinear interpolation, each quantity less its closed form on an empty
// sample; 5 nodes of w uniform over [0.1986, 0.2755] (linear interpolation between them), 9.7 MB, built once (about
// 0.26 s in a Release build on the desktop; build_evidence_tables builds them ahead), and one node at w = 1 (read only at
// w = 1 exactly, which no story uses: the untempered form the tests compare with), built on its first use. The grid
// steps (0.01 in y and kappa'), the bound kappa' <= 2 and the 5 nodes of w are heuristic, set by measurement: with
// kappa' <= 1 the quadrature's posterior mean missed by 1.1e-3 relatively at kappa' = 1.40 (w = 1, z = 5, b = 0.1,
// spread 1); with 2 nodes of w the mean missed by 2.6e-3, with 3 by 7.2e-4, with 5 by 1.9e-4
// (Evidence.BroadPriorsMatchDenseIntegralsAcrossW). Elsewhere in w (between 0.2755 and 1, or below 0.1986) only the
// quadrature serves, unmeasured there.
//
// Accuracy at order 5, measured against dense integrals over a >= 0:
//   - within 6.8e-4 nats, the posterior's mean within 5.7e-4 and its variance within 0.55%, relatively, over z of 0 to
//     100, s of -0.0008 to 1, b of 0.1 to 30, relative spreads of 0.01 to 3 and w of 0.1986, 0.23, 0.2754 and 1
//     (Evidence.ScoreMatchesADenseIntegral, 2 800 points, Task 5);
//   - within 9.1e-4 nats, 1.9e-4 and 0.037% across the regime's tables at 17 values of w and at 1
//     (Evidence.BroadPriorsMatchDenseIntegralsAcrossW);
//   - at w = 1 on the first prototype's grids (Observation.*, ported from the block form in Task 10): within 0.01 nats
//     on b of 1 to 30, z of 0 to 2 b and relative spreads of 0.03 to 0.5, the mean within 0.01 posterior standard
//     deviations and the variance within 2%; exact to 1e-9 nats on an empty sample (e = 0), where the integral has a
//     closed form; the Kalman update within 1% for b >= 30; the mode within 0.01 posterior standard deviations of the
//     dense integral's; the truncated nodes' table within 1e-6 nats of the nodes computed by Phi^-1; the start of
//     Newton's method changing nothing beyond 1e-9 (relatively); across the regime's edges (kappa' = 2 and y = 4) a move
//     of under 0.01 nats between points 1e-15 apart, relatively.
// Newton takes at most 4 steps on the tests' grid and 6 over b = 0 to 1e4, z to 3 b, spreads to 2; where the curvature
// degenerates, more: 21 at most over b and z up to 1e100 and spreads up to 1e6, within its cap of 60 (measured in the
// first prototype, on the same code). The score, the posterior and the mode are finite for q up to 2.1e9 (+85 dB S500)
// and priors from 1e-12 to 1e4 times mu^2 (Evidence.StaysFiniteAtTheExtremes).
#pragma once

#include <optional>

namespace kz4ap::viterbi {

// ln I0(x), nats, for every x (I0 is even): Abramowitz and Stegun 9.8.1 for |x| <= 3.75 and 9.8.2 above, the latter
// in the log with one logarithm, |x| + ln(polynomial / sqrt|x|), so that it never overflows (I0 itself passes the
// largest double at x = 714). The errors A&S state make it exact to 4.8e-7 nats (derived; 4.7e-7 measured against
// std::cyl_bessel_i). +infinity at infinity, NaN for NaN.
double ln_i0(double x);

// The mean and variance of a distribution.
struct Moments {
    double mean;
    double var;
};

// The broad-prior regime's w axis (1): its tables serve w in [kBroadPriorLowestW, kBroadPriorHighestW] (the constant
// w's 0.1986 to the 192 kHz chain's weak limit 0.27544, rounded up) and w = 1. Since D23 the decoder reads one w, 0.1987.
inline constexpr double kBroadPriorLowestW = 0.1986;
inline constexpr double kBroadPriorHighestW = 0.2755;

struct SampleEvidence {
    double score_nats;  // nats
    Moments amplitude;  // the posterior mean (FS) and variance (FS^2) of a
    double mode_a_fs;   // FS: the mode of prior x tempered likelihood
};

// The score of a sample (above): e_fs, the envelope sample (FS); s, the template (1); mu_fs and p_fs2, the prior's mean
// (FS) and variance (FS^2); v_prime_fs2, V' (FS^2); w, the weight (1); order, the quadrature's (5; 2 to 20);
// start_a_fs, where Newton's method starts (FS). s = 0 returns {0, {mu, P}, start_a_fs, or mu without one}. A prior known
// exactly (P = 0, or narrower than 1e-9 of max(beta, 1)) gives w Lambda(mu) and {mu, P}. Throws std::invalid_argument
// for an e that is negative or not finite, an s that is not finite, a mu or P that is negative or not finite, a V' that
// is not positive and finite, a w that is not positive and finite, an order outside 2 ... 20 or a start that is not
// finite.
SampleEvidence sample_evidence(double e_fs, double s, double mu_fs, double p_fs2, double v_prime_fs2, double w,
                               int order, std::optional<double> start_a_fs = std::nullopt);

// Builds, once for every caller, the tables sample_evidence reads: the broad-prior regime's for w over [0.1986,
// 0.2755] (about 0.26 s, 9.7 MB) and the truncated nodes' of this order (87.1 KB at order 5). Each is otherwise built
// on its first use, a stall in the middle of a stream; the decoder calls this when it is made. (The regime's node at
// w = 1, which no story reads, is left to its first use.) Thread-safe, and a no-op once built. Throws
// std::invalid_argument for an order outside 2 ... 20.
void build_evidence_tables(int order);

// The score of a sample by a story against the reference every story shares (design notes, part 4 as amended by D23):
//     score = ln integral exp(w [ln p(e | a, V) - ln p0(e | sigma_y^2)]) N(a; mu, P) da
//           = sample_evidence(e, s, mu, P, V, w).score + w [e^2 / sigma_y^2 - e^2 / V - ln(V / sigma_y^2)]    (derived),
// V (FS^2; systematic.hpp) not depending on a. The posterior and the mode are sample_evidence's: the added terms leave
// the integral. s = 0 scores exactly 0. Throws as sample_evidence does, and for a sigma_y^2 that is not positive and finite.
SampleEvidence story_evidence(double e_fs, double s, double mu_fs, double p_fs2, double v_fs2, double sigma2_fs2, double w,
                              int order, std::optional<double> start_a_fs = std::nullopt);

namespace detail {

// sample_evidence with every truncated node computed by Phi^-1 rather than read from the table, for the test that the
// table matches it.
SampleEvidence sample_evidence_direct_nodes(double e_fs, double s, double mu_fs, double p_fs2, double v_prime_fs2,
                                            double w, int order, std::optional<double> start_a_fs = std::nullopt);

}  // namespace detail

}  // namespace kz4ap::viterbi
