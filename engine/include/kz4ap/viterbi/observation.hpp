// The new decoder's statistics of the channel signal (design notes, topic 3, item 1; topic 4, items 2 and 7; topic 5,
// item 3): the noise of a sum of channel samples, and the evidence of a block of a mark, how much better a carrier
// explains it than noise alone, with the carrier's level known, uncertain (a Gaussian), or for a newborn story equally
// likely anywhere in a range of dB S500 (the flat prior).
//
// A block is m consecutive channel samples; S (FS) is their complex sum and V (FS^2) its noise variance. A carrier of
// amplitude a (FS, in the channel) and unknown phase has, against noise alone, the log-likelihood ratio, in nats,
//     Lambda(S; a) = -b^2 + ln I0(2 b z),    z = |S| / sqrt(V),    b = m a / sqrt(V),
// the noncoherent matched filter's (Proakis and Salehi, section 4.5-1); b^2 is the energy of the carrier's sum over
// the noise variance of the sum.
#pragma once

#include "kz4ap/viterbi/config.hpp"

#include <optional>
#include <span>
#include <vector>

namespace kz4ap::viterbi {

// c(m), Hz, at index m for m = 0 ... max_samples (c(0) = 0, the empty sum). With white noise of N0 (FS^2/Hz) at the
// channelizer's input, sigma^2 = N0 x input_rate_hz per complex sample, a sum of m consecutive channel samples has the
// noise variance V(m) = N0 c(m), where, for the filter's taps h and its decimation D (derived, exact; design notes,
// topic 4, item 7),
//     c(m) = sum_{|k| < m} (m - |k|) R_k,    R_k = input_rate_hz x sum_j h[j] h[j + D k],
// R_k N0 being the channel noise's autocovariance at a lag of k channel samples. On the channelizer's filter R_0 =
// 251.86 Hz (its noise bandwidth), R_k = 0 from 33 lags on, and the R_k add up to 1 500 Hz, the channel rate, so
// c(m) / m comes within 0.5% of 1 500 Hz from m = 300 on. Throws std::invalid_argument for no taps, an input_rate_hz
// that is not positive and finite, a decimation below 1 or a negative max_samples.
std::vector<double> sum_noise_hz(std::span<const double> taps, double input_rate_hz, int decimation, int max_samples);

// ln I0(x), nats, for every x (I0 is even): Abramowitz and Stegun 9.8.1 for |x| <= 3.75 and 9.8.2 above, the latter
// in the log with one logarithm, |x| + ln(polynomial / sqrt|x|), so that it never overflows (I0 itself passes the
// largest double at x = 714). The errors A&S state make it exact to 4.8e-7 nats (derived; 4.7e-7 measured against
// std::cyl_bessel_i). +infinity at infinity, NaN for NaN.
double ln_i0(double x);

// Lambda(S; a), nats: the evidence of a block of m channel samples whose sum has the magnitude s_abs (FS) and the
// noise variance v (FS^2), for a carrier of known amplitude a (FS). Throws std::invalid_argument for m < 1, a v that is
// not positive and finite, or an s_abs or a that is negative or not finite.
double block_llr(double s_abs, int m, double a, double v);

// Lambda_P, nats: the evidence of the same block when the level is uncertain, a ~ N(mu, p) (mu in FS, p in FS^2)
// truncated to a >= 0 and renormalized, the Kalman predictive density (design notes, topic 3, item 1):
//     Lambda_P = ln integral_0^inf N(a; mu, p) e^Lambda(S; a) da - ln Phi(mu / sqrt(p)),
// Phi the standard normal distribution function. By adaptive Gauss-Hermite quadrature with `order` nodes (Liu and
// Pierce 1994; Pinheiro and Bates 1995), in b, where the prior is N(beta, q), beta = m mu / sqrt(v), q = m^2 p / v:
// the nodes are centered at the mode b^ of the integrand e^g(b) (prior x likelihood) and scaled by its curvature
// there, sigma^ = (-g''(b^))^(-1/2), not spread over the prior. Newton's method finds b^, starting from the
// precision-weighted mean of the prior and the likelihood's peak, (beta + 2 q z) / (1 + 2 q), which is never below it
// (g' <= 0 there, as d ln I0 / dx < 1), with bisection as a safeguard. Where N(b^, sigma^^2) has mass below 0
// (b^ < 8.5 sigma^), the nodes are those of that Gaussian truncated at 0: node k is b^ + sigma^ Phi^-1(Phi(-tau) +
// (1 - Phi(-tau)) Phi(t_k)), tau = b^ / sigma^, the standard normal mapped onto the truncated Gaussian through their
// distribution functions. The rule then integrates e^g divided by that truncated Gaussian's density, a constant when
// e^g is Gaussian, so it is exact for a Gaussian integrand however much the truncation cuts off (an extension of the
// standard form; above 8.5 sigma^ the two agree to the double's precision). The truncated nodes are read from a table
// in tau (steps of 1/256 from 0 to 8.5, linear interpolation; built once per order on first use, 8.5 KB per node, 85 KB
// at order 5), which costs a truncated call about 510 ns instead of 800 ns (measured). Against the nodes computed by
// Phi^-1 (detail::block_evidence_direct_nodes) the evidence moves by at most 4.8e-8 nats on the tests' grid below (15
// of its 64 points are truncated) and 1.2e-7 nats over orders 2 to 20, b of 0.02 to 200, relative spreads of 0.03 to 5
// and z of 0 to 3 b; the posterior's mean by at most 7.4e-7 of its standard deviation and its variance by 7.8e-7
// relatively (measured); the mode not at all, since it is found first. The standard nodes put part of the integral
// below 0: dropping the nodes there misses a dense integral by up to 0.355 nats on the tests' grid, and by 0.62 with
// the remaining weights renormalized (measured, at b = 30, z = 0 and a relative spread of 0.5). The nodes'
// terms are summed in the log (log-sum-exp), so no share underflows into an empty sum. A level known exactly, p = 0
// or a prior narrower than 1e-9 of max(beta, 1), gives block_llr at mu. The 1e-9 is a heuristic margin: the
// quadrature's error from rounding its nodes' offsets grows as epsilon b / sigma^ and is still 7e-9 nats at that width
// at b = 10, against 5.7e-5 at 1e-12, 3.2e-3 at 1e-14 and 1.09 nats from about 1e-16, where the nodes coincide.
//
// Where the prior is broad against the likelihood, the curvature at the mode degenerates: with beta = 0 the mode
// leaves b = 0 at z* = sqrt(1 + 1/(2 q)), where -g'' vanishes while the integrand's width is set by the quartic term of
// ln I0 (x^2 / 4 - x^4 / 64), and curvature-scaled nodes spread far wider than the integrand. Near z = 1, at order 5,
// they missed by up to 1.2 nats for strong stations (beta of 3 000 to 1e6) at relative spreads of 0.5 to 2 and by 0.14
// to 0.41 nats for mu = 0 at sqrt(q) of 0.3 to 10, and put the posterior's variance at 0 to 3.2 times its true value
// at spreads of 1 to 30; at beta = 1e5 and a spread of 30, order 3 missed by 168 nats and order 5 by 0.84 (measured).
// An exact rescaling serves there instead. In b, N(b; beta, q) = N(0; beta, q) e^(kappa b - b^2 / (2 q)) with
// kappa = beta / q; the prior's e^(-b^2 / (2 q)) joins the likelihood's e^-b^2 as e^(-s^2 b^2), where
// s = sqrt(1 + 1/(2 q)), and u = s b turns the integral into J(z', kappa') / s, with z' = z / s, kappa' = kappa / s and
// J(z', kappa') = integral_0^inf e^(kappa' u - u^2) I0(2 u z') du:
//     Lambda_P = ln N(0; beta, q) - ln Phi(beta / sqrt(q)) + ln J(z', kappa') - ln s,
// and the posterior of b is that weight's distribution in u, over s (exact at every q, derived). Where z' <= 4 and
// kappa' <= 1 (the broad-prior regime), ln J and the weight's mean and variance come from tables: steps of 0.01 in z'
// and kappa', 40 501 nodes, bilinear interpolation of each less its closed form at z' = 0 (so that an empty block is
// exact), built once (31 ms in a Release build, 0.97 MB) and shared by every caller. The curvature degenerates around
// z' = 1 at kappa' = 0, where -g''(0) = 2 s^2 (1 - z'^2) vanishes (derived); the regime's bounds are a heuristic margin
// around that point, and outside them the adaptive quadrature holds within 0.002 nats where measured (below). The
// mode comes from Newton's method in both.
//
// Accuracy at order 5, measured against dense integrals:
//   - in the regime, within 3.4e-5 nats, and the posterior's mean and variance within 3e-5 relatively, at every q from
//     1e-6 to 1e12 and at every order: strong stations (beta of 100 to 1e6 at relative spreads of 0.3 to 2), weak broad
//     priors, mu = 0 and blocks of z = 0 to 4 included;
//   - outside it, within 0.0011 nats on the tests' grid (beta = m mu / sqrt(v) of 1 to 30, z = 0, b/2, b and 2 b,
//     relative spreads sqrt(p) / mu of 0.03 to 0.5: 61 of its 64 points; the other 3 lie in the regime); within
//     0.002 nats where kappa' > 1 (kappa' to 1 000, z to 10), with the mean within 0.005 posterior standard deviations
//     and the variance within 1.1%; and within 2e-6 nats where z' > 4 (z' to 12);
//   - across the regime's edges Lambda_P moves by at most 0.004 nats between points 2e-15 apart relatively (at
//     kappa' = 1 and q = 1e12, where up to 0.002 nats of it is Lambda_P's own change over that step) and by at most
//     2e-5 nats at z' = 4.
// These were measured where beta^2 / q is at most 1e10. Where the mode lies far below beta, Lambda_P is about
// -beta^2 / (2 q) and carries its rounding, epsilon beta^2 / q, which reaches 0.01 nats from beta^2 / q of about 1e14.
// Newton takes at most 4 steps on the tests' grid and 6 over b = 0 to 1e4, z to 3 b, spreads to 2; where the
// curvature degenerates, more: 7 to 12 for mu = 0 at z = 1 and sqrt(q) of 3 to 20, 17 at beta = 1e6, z = 1 and a
// spread of 3, and 21 at most over b and z up to 1e100 and spreads up to 1e6, cold or started at 0, within its cap of
// 60 (measured). Throws std::invalid_argument for what block_llr refuses, a mu or p that is negative or not finite,
// or an order outside 1 ... 20.
double block_llr_uncertain(double s_abs, int m, double mu, double p, double v, int order);

// The mean and variance of a distribution.
struct Moments {
    double mean;
    double var;
};

// The level after the block: the mean (FS) and variance (FS^2) of a under the same posterior, prior x likelihood, by
// the same evaluation (moment matching), the update of a story's level after a finished block (design notes, topic 3,
// item 1). At high SNR it is the Kalman update, mean mu + p / (p + v / (2 m^2)) (s_abs / m - mu) and variance
// p (v / (2 m^2)) / (p + v / (2 m^2)): within 0.1% for b >= 30 at a relative spread of 0.1. At order 5, on the grid
// above, the mean is within 0.0025 of the posterior's standard deviation and the variance within 0.6% of a dense
// integral (measured). In the broad-prior regime (above) the moments come from the tables, within 3e-5 relatively; the
// variance after a block of noise (z = 1) is near 0.36 there (units of b^2), where curvature-scaled nodes had put it at
// 0 to 3.2 times that. For a level known exactly it is the prior, {mu, p} ({mu, 0} with p = 0); for mu > 0 the true
// posterior differs from it by about q relatively, for mu = 0 (a half-normal of mean 0.80 sqrt(p) and variance 0.36 p)
// by order 1 relatively but by under 1e-9 in units of b. Throws for what block_llr_uncertain refuses, and for an order
// below 2 (one node holds no variance).
Moments level_posterior(double s_abs, int m, double mu, double p, double v, int order);

// What one evaluation gives of a block: the evidence and the level after it, and where the posterior peaks.
struct BlockEvidence {
    double llr;      // nats: Lambda_P, as block_llr_uncertain gives it
    Moments level;   // the posterior mean (FS) and variance (FS^2) of a, as level_posterior gives them
    double mode_a;   // FS: the mode of prior x likelihood
};

// block_llr_uncertain and level_posterior in one call, from the same evaluation (the search makes one per story and
// piece). start_a (FS), when given, is where Newton's method starts, clamped into its bracket [0, the precision-
// weighted mean]: the story's mode_a from the previous piece. The result does not depend on it beyond Newton's
// tolerance, a last step below the larger of 1e-10 sigma^ and 4 epsilon b (a few spacings of doubles at b), including
// at b = 0 when mu = 0, where a zero slope may be a minimum. For a level known exactly: {block_llr, {mu, p}, mu}, so
// {block_llr, {mu, 0}, mu} with p = 0. At every order from 2 its values are finite for b and z below 1e100 (beyond
// about 1e154, b^2 leaves the doubles, as in block_llr). Throws for what level_posterior refuses and for a start_a
// that is not finite.
BlockEvidence block_evidence(double s_abs, int m, double mu, double p, double v, int order,
                             std::optional<double> start_a = std::nullopt);

namespace detail {

// block_evidence with every truncated node computed by Phi^-1 rather than read from the table (the form before the
// table), for the test that the table matches it.
BlockEvidence block_evidence_direct_nodes(double s_abs, int m, double mu, double p, double v, int order,
                                          std::optional<double> start_a = std::nullopt);

}  // namespace detail

// The evidence of a newborn story's first block (design notes, topic 5, item 3): its level x, dB S500, is equally
// likely anywhere in [level_min_db, level_max_db], with a^2 = 10^(x / 10) N0 x 500 Hz. For a block of n pieces of
// samples_per_piece channel samples each, m = samples_per_piece x n (3 x n for pieces of 2 ms at 1 500 samples/s),
// b^2 = 10^(x / 10) x 500 Hz x m^2 / c(m), whatever N0, and
//     Lambda_flat(n, z) = ln[ integral e^Lambda(S; a(x)) dx / (level_max_db - level_min_db) ],
// with the posterior mean and variance of b beside it, in units of sqrt(V(m)) / m and V(m) / m^2 (the caller scales
// to FS: mean_a = mean_b sqrt(V) / m, var_a = var_b V / m^2).
//
// Tables for n = 1 ... max_pieces and z = 0, 0.01, ..., 40, read with linear interpolation in z. They come from one
// set of cumulative integrals: in u = ln b the integrand, e^(-e^(2u)) I0(2 e^u z), does not depend on n, which sets
// only the lower limit, u_lo(n) = ln b at level_min_db (the upper limit lies far above every tabulated z), so one
// integral per z from the top of a grid in u down, by the trapezoid rule in steps of 0.01 dB of x, read at every
// u_lo(n), serves every n: within 4.3e-5 nats of a direct integration over x, and built in 0.22 s for 195 pieces
// (measured, on the channelizer's filter at 48 kHz). The grid's nodes are whole multiples of the step in u, so an
// entry depends only on m: blocks of the same number of samples read the same values, whatever the piece. Above
// z = 40, the large-z form (Laplace's method)
//     Lambda_flat = z^2 - ln(2 z^2) + ln(20 / (ln 10 x (level_max_db - level_min_db))),
// with level_max_db - level_min_db the range's width in dB S500. The form meets the table within 6.3e-4 nats at
// z = 40 (its error falls as 1 / z^2: 0.0011 at 30, 0.010 at 10). The posterior of b there is proportional to
// e^-(b - z)^2 b^(-3/2) (the likelihood's e^-(b - z)^2 / sqrt(b) over a prior flat in ln b), and Laplace's method one
// order further gives its mean, z - 3 / (4 z), and variance, 1/2 + 3 / (8 z^2), which meet the table's at z = 40
// within 2e-5. The form takes the level to lie inside the range, b well below its value at level_max_db (26 002 for
// one piece, at the default range and on the channelizer's filter); above that it does not cut the prior off.
//
// One FlatPrior serves every decoder of a configuration and channel filter, which hold it as
// std::shared_ptr<const FlatPrior>: it holds 3 x 4 001 doubles per block length (18.7 MB for 195 pieces) and is not
// copied.
class FlatPrior {
public:
    // c: sum_noise_hz's c(m), Hz, up to m = samples_per_piece x max_pieces at least. Throws std::invalid_argument for
    // max_pieces or samples_per_piece below 1, a c shorter than that or not positive and finite where read, a level
    // range that is not finite or empty, or one the tables cannot hold: for some block length, b at level_min_db above
    // 6 (at 6 the integrand there falls by e every 12 steps of the grid, and the trapezoid and the reading between
    // nodes lose up to 0.0014 nats, derived) or b at level_max_db below 50 (the table's top, z = 40, plus 10).
    FlatPrior(const ViterbiConfig& config, std::span<const double> c, int max_pieces, int samples_per_piece);
    FlatPrior(const FlatPrior&) = delete;
    FlatPrior& operator=(const FlatPrior&) = delete;

    // The constructor's values: the longest block, in pieces, and the channel samples in a piece.
    int max_pieces() const { return max_pieces_; }
    int samples_per_piece() const { return samples_per_piece_; }

    // Lambda_flat(n, z), nats, for a block of `pieces` pieces (1 ... max_pieces) with z = |S| / sqrt(V(m)) >= 0.
    // Throws std::out_of_range for pieces outside 1 ... max_pieces and std::invalid_argument for a z that is negative
    // or not finite.
    double llr(int pieces, double z) const;
    // The posterior mean and variance of b, in units of sqrt(V(m)) / m and V(m) / m^2. The same errors as llr.
    Moments posterior(int pieces, double z) const;

private:
    struct Node {
        double llr_less_z2;  // nats: Lambda_flat - z^2, which is smoother in z than Lambda_flat
        double mean;         // of b
        double var;          // of b
    };
    // The block length's row of the table, after checking the arguments.
    const Node* row(int pieces, double z) const;

    std::vector<Node> nodes_;  // pieces-major: (pieces - 1) x 4 001 + i, at z = 0.01 i
    int max_pieces_;
    int samples_per_piece_;
    double large_z_constant_;  // nats: ln(20 / (ln 10 x the range's width in dB S500))
};

}  // namespace kz4ap::viterbi
