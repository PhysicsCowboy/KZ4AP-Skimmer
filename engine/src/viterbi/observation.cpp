#include "kz4ap/viterbi/observation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <mutex>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <vector>

namespace kz4ap::viterbi {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

bool finite_and_not_negative(double x) { return x >= 0.0 && std::isfinite(x); }

// --- ln I0: Abramowitz and Stegun 9.8.1 and 9.8.2 ---

constexpr double kBesselSplit = 3.75;
// 9.8.1, |x| <= 3.75: I0(x) = sum_k kSmall[k] s^k with s = (x / 3.75)^2; |error| < 1.6e-7.
constexpr std::array<double, 7> kSmall = {{1.0, 3.5156229, 3.0899424, 1.2067492, 0.2659732, 0.0360768, 0.0045813}};
// 9.8.2, x >= 3.75: sqrt(x) e^-x I0(x) = sum_k kLarge[k] t^k with t = 3.75 / x; |error| < 1.9e-7.
constexpr std::array<double, 9> kLarge = {{0.39894228, 0.01328592, 0.00225319, -0.00157565, 0.00916281, -0.02057706,
                                           0.02635537, -0.01647633, 0.00392377}};

// sum_k c[k] t^k and its first and second derivatives in t, by Horner's rule.
template <std::size_t N>
double polynomial(const std::array<double, N>& c, double t) {
    double value = c[N - 1];
    for (std::size_t k = N - 1; k-- > 0;) value = value * t + c[k];
    return value;
}
template <std::size_t N>
double polynomial_slope(const std::array<double, N>& c, double t) {
    double value = static_cast<double>(N - 1) * c[N - 1];
    for (std::size_t k = N - 1; k-- > 1;) value = value * t + static_cast<double>(k) * c[k];
    return value;
}
template <std::size_t N>
double polynomial_curvature(const std::array<double, N>& c, double t) {
    double value = static_cast<double>((N - 1) * (N - 2)) * c[N - 1];
    for (std::size_t k = N - 1; k-- > 2;) value = value * t + static_cast<double>(k * (k - 1)) * c[k];
    return value;
}

// The first and second derivatives of ln_i0 at x >= 0, taken from the same fits (an equivalent of I1 / I0 and its
// derivative), so that Newton's method finds the maximum of exactly the integrand that is integrated. The second
// stays positive and near 1 / (2 x^2) for large x, where 1 - I1/I0 / x - (I1/I0)^2 would cancel.
struct Slopes {
    double first;
    double second;
};

Slopes ln_i0_slopes(double x) {
    if (x <= kBesselSplit) {
        constexpr double kScale = 1.0 / (kBesselSplit * kBesselSplit);
        const double s = x * x * kScale;
        const double p = polynomial(kSmall, s);
        const double p1 = polynomial_slope(kSmall, s);
        const double p2 = polynomial_curvature(kSmall, s);
        const double ds = 2.0 * x * kScale;  // ds/dx; d2s/dx2 = 2 kScale
        const double first = p1 * ds / p;
        return {first, (p2 * ds * ds + 2.0 * kScale * p1) / p - first * first};
    }
    const double t = kBesselSplit / x;
    const double p = polynomial(kLarge, t);
    const double r1 = polynomial_slope(kLarge, t) / p;
    const double r2 = polynomial_curvature(kLarge, t) / p;
    const double dt = -t / x;  // dt/dx; d2t/dx2 = 2 t / x^2
    return {1.0 - 0.5 / x + r1 * dt, 0.5 / (x * x) + (r2 - r1 * r1) * dt * dt + 2.0 * r1 * t / (x * x)};
}

// --- Gauss-Hermite rules for the standard normal ---

constexpr int kMaxOrder = 20;

struct Rule {
    std::array<double, kMaxOrder> node{};        // t_k, standard deviations
    std::array<double, kMaxOrder> log_weight{};  // ln w_k, the weights summing to 1: E f(t) ~ sum_k w_k f(t_k)
    std::array<double, kMaxOrder> tail{};        // Phi(-|t_k|), the standard normal's mass beyond the node
};

// The rule of order n: the roots x of the Hermite polynomial H_n (weight e^(-x^2)), by Newton's method on its
// orthonormal recurrence from the starting guesses of Numerical Recipes' gauher, with the weights 2 / H~_n'(x)^2; then
// t = sqrt(2) x and w / sqrt(pi) for the standard normal. Exact for polynomials of degree below 2 n.
Rule make_rule(int n) {
    std::array<double, kMaxOrder> x{};
    std::array<double, kMaxOrder> w{};
    double root = 0.0;
    for (int i = 0; i < (n + 1) / 2; ++i) {
        if (i == 0)
            root = std::sqrt(2.0 * n + 1.0) - 1.85575 * std::pow(2.0 * n + 1.0, -1.0 / 6.0);
        else if (i == 1)
            root -= 1.14 * std::pow(static_cast<double>(n), 0.426) / root;
        else if (i == 2)
            root = 1.86 * root - 0.86 * x[0];
        else if (i == 3)
            root = 1.91 * root - 0.91 * x[1];
        else
            root = 2.0 * root - x[static_cast<std::size_t>(i - 2)];
        double slope = 0.0;
        for (int iteration = 0; iteration < 100; ++iteration) {
            double p1 = 0.75112554446494248286;  // pi^(-1/4), the orthonormal H~_0
            double p2 = 0.0;
            for (int j = 1; j <= n; ++j) {
                const double p3 = p2;
                p2 = p1;
                p1 = root * std::sqrt(2.0 / j) * p2 - std::sqrt((j - 1.0) / j) * p3;
            }
            slope = std::sqrt(2.0 * n) * p2;
            const double step = p1 / slope;
            root -= step;
            if (std::fabs(step) <= 1e-14 * std::max(1.0, std::fabs(root))) break;
        }
        x[static_cast<std::size_t>(i)] = root;
        x[static_cast<std::size_t>(n - 1 - i)] = -root;
        w[static_cast<std::size_t>(i)] = w[static_cast<std::size_t>(n - 1 - i)] = 2.0 / (slope * slope);
    }
    Rule rule;
    for (std::size_t k = 0; k < static_cast<std::size_t>(n); ++k) {
        rule.node[k] = std::numbers::sqrt2 * x[k];
        rule.log_weight[k] = std::log(w[k] * std::numbers::inv_sqrtpi);
        rule.tail[k] = 0.5 * std::erfc(std::fabs(x[k]));  // Phi(-|t|) = erfc(|t| / sqrt(2)) / 2
    }
    return rule;
}

const Rule& rule_of_order(int order) {
    static const std::array<Rule, kMaxOrder + 1> rules = [] {
        std::array<Rule, kMaxOrder + 1> all{};
        for (int n = 1; n <= kMaxOrder; ++n) all[static_cast<std::size_t>(n)] = make_rule(n);
        return all;
    }();
    return rules[static_cast<std::size_t>(order)];
}

// Phi^-1(p) for 0 < p < 1: Acklam's rational approximation (relative error below 1.15e-9), refined by one step of
// Halley's method on Phi(x) - p, with Phi from std::erfc (to about 1e-11, measured against Wichura's AS 241).
double inverse_normal_cdf(double p) {
    constexpr double kA[] = {-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
                             1.383577518672690e+02,  -3.066479806614716e+01, 2.506628277459239e+00};
    constexpr double kB[] = {-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
                             6.680131188771972e+01, -1.328068155288572e+01};
    constexpr double kC[] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                             -2.549732539343734e+00, 4.374664141464968e+00,  2.938163982698783e+00};
    constexpr double kD[] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
                             3.754408661907416e+00};
    constexpr double kTail = 0.02425;
    const auto tail = [&](double r) {
        return (((((kC[0] * r + kC[1]) * r + kC[2]) * r + kC[3]) * r + kC[4]) * r + kC[5]) /
               ((((kD[0] * r + kD[1]) * r + kD[2]) * r + kD[3]) * r + 1.0);
    };
    double x = 0.0;
    if (p < kTail) {
        x = tail(std::sqrt(-2.0 * std::log(p)));
    } else if (p > 1.0 - kTail) {
        x = -tail(std::sqrt(-2.0 * std::log1p(-p)));
    } else {
        const double r = p - 0.5;
        const double s = r * r;
        x = (((((kA[0] * s + kA[1]) * s + kA[2]) * s + kA[3]) * s + kA[4]) * s + kA[5]) * r /
            (((((kB[0] * s + kB[1]) * s + kB[2]) * s + kB[3]) * s + kB[4]) * s + 1.0);
    }
    const double error = 0.5 * std::erfc(-x / std::numbers::sqrt2) - p;
    const double u = error * std::sqrt(2.0 * std::numbers::pi) * std::exp(0.5 * x * x);
    return x - u / (1.0 + 0.5 * x * u);
}

// --- The amplitude's posterior, in b = |s| a / sqrt(V') ---

// A sample and the amplitude's prior N(beta, q) truncated to b >= 0: the integrand on b >= 0 is e^g(b), prior x
// tempered likelihood without the prior's normalization, g(b) = -(b - beta)^2 / (2 q) + w (-b^2 + ln I0(2 b z)), the
// likelihood tempered by the sample's weight w (plan 1b, Task 5).
struct Integrand {
    double beta;  // the prior's mean, b
    double q;     // its variance, b^2
    double z;     // e / sqrt(V')
    bool exact;   // an amplitude known exactly: P = 0, or sqrt(q) <= 1e-9 max(beta, 1)
    double w;     // the likelihood's weight (1)
    double g(double b) const {
        const double d = b - beta;
        return -0.5 * d * d / q - w * b * b + w * ln_i0(2.0 * b * z);
    }
};

constexpr int kNewtonSteps = 60;  // a bound for the safeguarded iteration, never reached on the tests
// A Newton step below this many spacings of doubles at b, relatively, ends the iteration: one can be no smaller.
constexpr double kSpacings = 4.0 * std::numeric_limits<double>::epsilon();
// sqrt(q): a prior narrower than this share of its center, max(beta, 1), is an amplitude known exactly. A heuristic margin:
// the quadrature's error from rounding its nodes' offsets grows as epsilon b / sigma^, and is still 7e-9 nats at this
// width at b = 10 (5.7e-5 at 1e-12, 3.2e-3 at 1e-14, 1.09 nats from about 1e-16, where the nodes coincide).
constexpr double kExactPrior = 1e-9;
// sigma^: from here on the Gaussian at the mode has under Phi(-8.5) = 9.5e-18 of its mass below 0, under half the
// spacing of doubles at 1, so the truncated nodes are the standard ones; every rule's nodes (at most 7.62 for order
// 20) then lie above 0.
constexpr double kTruncationLimit = 8.5;

// Node k of the order's rule in the standard normal truncated below at -tau (the Gaussian at the mode truncated at
// b = 0), where below = Phi(-tau): the rule's node t_k mapped through the two distribution functions, Phi^-1(below +
// (1 - below) Phi(t_k)), taken from the tail t_k lies in.
double truncated_node(const Rule& rule, std::size_t k, double below) {
    return rule.node[k] < 0.0 ? inverse_normal_cdf(below + (1.0 - below) * rule.tail[k])
                              : -inverse_normal_cdf((1.0 - below) * rule.tail[k]);
}

// The truncated nodes against tau = b^ / sigma^ in [0, 8.5], at steps of 1/256 (2 177 rows: tau = j / 256 exactly),
// tau-major (row j holds the order's nodes), read by linear interpolation; built once per order on first use, for every
// caller (2 177 rows of 8 bytes, 17.4 KB, per node of the order: 87.1 KB at order 5). Exact at the rows; between them
// within the error the header states. They replace the order's Phi^-1 evaluations, the larger part of a truncated
// quadrature's cost.
constexpr int kNodeRowsPerTau = 256;
constexpr int kNodeSteps = static_cast<int>(kTruncationLimit * kNodeRowsPerTau);  // 2 176

const std::vector<double>& truncated_node_table(int order) {
    static std::array<std::once_flag, kMaxOrder + 1> built;
    static std::array<std::vector<double>, kMaxOrder + 1> tables;
    const auto n = static_cast<std::size_t>(order);
    std::call_once(built[n], [n, order] {
        const Rule& rule = rule_of_order(order);
        std::vector<double>& table = tables[n];
        table.resize((static_cast<std::size_t>(kNodeSteps) + 1) * n);
        for (int j = 0; j <= kNodeSteps; ++j) {
            const double tau = static_cast<double>(j) / kNodeRowsPerTau;
            const double below = 0.5 * std::erfc(tau / std::numbers::sqrt2);
            for (std::size_t k = 0; k < n; ++k)
                table[static_cast<std::size_t>(j) * n + k] = truncated_node(rule, k, below);
        }
    });
    return tables[n];
}

// The quadrature of the posterior: the nodes b_k and their shares of the integral, w_k e^(g(b_k) - g(b^) +
// s_k^2 / 2) over the largest of them, s_k the node's distance from the mode in sigma^, and the score.
struct Fit {
    std::array<double, kMaxOrder> b{};
    std::array<double, kMaxOrder> share{};  // the largest is 1
    double total = 0.0;                     // the sum of the shares
    double llr = 0.0;                       // the score, nats
};

// The mode b^ of the integrand e^g. start: where Newton's method starts, in units of b (clamped into its bracket); the
// bracket's top when empty.
double find_mode(const Integrand& integrand, std::optional<double> start) {
    const double beta = integrand.beta;
    const double q = integrand.q;
    const double z = integrand.z;
    const double w = integrand.w;
    // The mode b^ of g: Newton's method on g', by default from the precision-weighted mean of the prior (beta,
    // precision 1 / q) and the likelihood's peak (z, precision 2 w). There g' <= 0 since d ln I0 / dx < 1, while g'(0) =
    // beta / q >= 0, so b^ lies in the bracket [0, that mean]. It stops at a step below 1e-10 sigma^ or below a few
    // spacings of doubles at b, or one that leaves b where it is; a step past the bracket's top goes to the top, one
    // that would leave the bracket below, or one where g is not concave, bisects it instead, and one past b = 0 stops
    // there. A zero slope is the mode only where g is concave: elsewhere it can only be b = 0 with beta = 0
    // (g'(0) = beta / q), a minimum once z^2 > 1 + 1/(2 w q), and the mode lies above it.
    double low = 0.0;
    double high =
        q <= 1.0 ? (beta + 2.0 * w * q * z) / (1.0 + 2.0 * w * q) : (beta / q + 2.0 * w * z) / (1.0 / q + 2.0 * w);
    double b = start ? std::clamp(*start, low, high) : high;
    for (int step = 0; step < kNewtonSteps; ++step) {
        const Slopes s = ln_i0_slopes(2.0 * b * z);
        const double slope = -(b - beta) / q - 2.0 * w * b + 2.0 * w * z * s.first;  // g'(b)
        const double curvature = 1.0 / q + 2.0 * w - 4.0 * w * z * z * s.second;    // -g''(b)
        if (slope == 0.0 && curvature > 0.0) break;
        if (slope >= 0.0)
            low = b;
        else
            high = b;
        if (slope != 0.0 && curvature > 0.0) {
            const double newton = slope / curvature;
            const double next = std::max(0.0, b + newton);
            if (std::fabs(newton) <= std::max(1e-10 / std::sqrt(curvature), kSpacings * b) || next == b) {
                b = next;
                break;
            }
            if (next >= low && next <= high) {
                b = next;
                continue;
            }
            // Past the top from below: the top, where g' <= 0, from which Newton's steps run down to the mode.
            if (next > high && b < high) {
                b = high;
                continue;
            }
        }
        b = 0.5 * (low + high);
        if (high - low <= 1e-15 * high) break;
    }
    return b;
}

// The adaptive quadrature at the mode b, its truncated nodes read from the table or (tabulated = false) computed.
Fit fit_posterior(const Integrand& integrand, int order, double b, bool tabulated) {
    const double beta = integrand.beta;
    const double q = integrand.q;
    const double z = integrand.z;
    const double w = integrand.w;
    // -g''(b^) = 1 / q + excess. It is positive at a maximum; the fallback, the curvature of prior x e^(-w b^2) (so that
    // sigma^ < 1 / sqrt(2 w)), guards a degenerate one and a scale beyond the doubles' range.
    double excess = 2.0 * w - 4.0 * w * z * z * ln_i0_slopes(2.0 * b * z).second;
    if (!(q * excess > -1.0) || !std::isfinite(std::sqrt(q / (1.0 + q * excess)))) excess = 2.0 * w;
    const Rule& rule = rule_of_order(order);
    const double g_mode = integrand.g(b);
    Fit fit;
    // The nodes' terms in the log, ln w_k + g(b_k) - g(b^) + s_k^2 / 2, summed by log-sum-exp, so that no share
    // underflows into an empty sum. A term that is not a number (a node beyond the doubles' range) counts for nothing;
    // if no term counts, the nodes are laid again at the fallback's scale, within a few units of b of the mode.
    std::array<double, kMaxOrder> log_term{};
    double sigma = 0.0;
    double below = 0.0;
    double largest = -kInf;
    for (int attempt = 0; attempt < 2 && !(largest > -kInf); ++attempt) {
        if (attempt > 0) excess = 2.0 * w;
        sigma = std::sqrt(q / (1.0 + q * excess));
        const double tau = b / sigma;
        below = tau < kTruncationLimit ? 0.5 * std::erfc(tau / std::numbers::sqrt2) : 0.0;
        // Where the truncation matters (0 <= tau < 8.5), the table's two rows around tau.
        const double* row = nullptr;
        double fraction = 0.0;
        if (below > 0.0 && tabulated) {
            const double position = tau * kNodeRowsPerTau;
            const int cell = std::min(static_cast<int>(position), kNodeSteps - 1);
            fraction = position - cell;
            row = truncated_node_table(order).data() + static_cast<std::size_t>(cell) * static_cast<std::size_t>(order);
        }
        for (std::size_t k = 0; k < static_cast<std::size_t>(order); ++k) {
            // The node's quantile, in the Gaussian at the mode truncated at b = 0 when that truncation matters.
            double s = rule.node[k];
            if (below > 0.0)
                s = row ? row[k] + fraction * (row[k + static_cast<std::size_t>(order)] - row[k])
                        : truncated_node(rule, k, below);
            fit.b[k] = std::max(0.0, b + sigma * s);
            const double term = rule.log_weight[k] + integrand.g(fit.b[k]) - g_mode + 0.5 * s * s;
            log_term[k] = std::isnan(term) ? -kInf : term;
            largest = std::max(largest, log_term[k]);
        }
    }
    for (std::size_t k = 0; k < static_cast<std::size_t>(order); ++k) {
        fit.share[k] = std::exp(log_term[k] - largest);
        fit.total += fit.share[k];
    }
    // ln of the prior's integral against the likelihood, (2 pi q)^(-1/2) integral_0^inf e^g db, which is
    // ln(sigma^ / sqrt(q)) + ln(1 - Phi(-tau)) + g(b^) + ln(the sum of the terms), less ln Phi(beta / sqrt(q)) for
    // the truncation; the shares are the terms over the largest, so the sum's log is largest + ln(total).
    fit.llr = -0.5 * std::log1p(q * excess) + std::log1p(-below) + g_mode + largest + std::log(fit.total) -
              std::log1p(-0.5 * std::erfc(beta / std::sqrt(2.0 * q)));
    return fit;
}

// The posterior's mean and variance in b from the quadrature's nodes and shares.
Moments fit_moments(const Fit& fit, int order) {
    double mean = 0.0;
    for (std::size_t k = 0; k < static_cast<std::size_t>(order); ++k) mean += fit.share[k] * fit.b[k];
    mean /= fit.total;
    double var = 0.0;
    for (std::size_t k = 0; k < static_cast<std::size_t>(order); ++k) {
        const double d = fit.b[k] - mean;
        var += fit.share[k] * d * d;
    }
    var /= fit.total;
    return {mean, var};
}


// --- The broad-prior regime: an exact rescaling and tables (plan 1b, Task 5; O9) ---
//
// In b, N(b; beta, q) = N(0; beta, q) e^(kappa b - b^2 / (2 q)), kappa = beta / q, exactly. With the tempered
// likelihood, N(b; beta, q) e^(w (-b^2 + ln I0(2 b z))) = N(0; beta, q) e^(kappa b - s_w^2 b^2) I0(2 b z)^w,
// s_w = sqrt(w + 1 / (2 q)), and u = s_w b gives
//     integral_0^inf N(b; beta, q) e^(w Lambda(b)) db = N(0; beta, q) J_w(zeta, kappa') / s_w,
//     J_w(zeta, kappa') = integral_0^inf e^(kappa' u - u^2) I0(2 u zeta)^w du,   zeta = z / s_w, kappa' = kappa / s_w,
// and the posterior of b is the weight's distribution in u over s_w (derived, exact at every q and w). The curvature at
// b = 0 with beta = 0, -g''(0) = 2 s_w^2 (1 - w zeta^2), vanishes at y = sqrt(w) zeta = 1 (derived), so the tables
// are laid out in y: y in [0, 4] and kappa' in [0, 2] (heuristic bounds, set by measurement; observation.hpp), steps of
// 0.01 in each, Simpson's rule over u in [0, 12] in steps of 0.02 (the weight is e^-49 of its peak there at the far
// corner, y = 4, kappa' = 2, w = 1), each quantity less its value on an empty sample (z = 0, which does not depend on
// w). One table per node of w: kTemperedWNodes nodes uniform over [0.1986, 0.2755] (the fixed weight's 0.1986 to the
// 192 kHz chain's weak limit 0.27544, rounded up), read by linear interpolation in w between them, built once for every
// caller (1.93 MB per node, 9.7 MB; build_evidence_tables builds it ahead); and one at w = 1 (the untempered form, read
// only at w = 1 exactly, which no story uses: the tests' reference to the untempered quadrature), built on its first
// use only. Elsewhere in w the adaptive quadrature alone serves (unmeasured there).

constexpr double kTemperedLowestW = kBroadPriorLowestW;    // 1 (observation.hpp)
constexpr double kTemperedHighestW = kBroadPriorHighestW;  // 1 (observation.hpp)
constexpr int kTemperedWNodes = 5;         // nodes of w over [kTemperedLowestW, kTemperedHighestW]
constexpr double kTemperedTopY = 4.0;      // y = sqrt(w) zeta, at most
constexpr int kTemperedYSteps = 400;       // y = 0, 0.01, ..., 4
constexpr double kTemperedTopKappa = 2.0;  // kappa', at most
constexpr int kTemperedKappaSteps = 200;   // kappa' = 0, 0.01, ..., 2
constexpr double kTemperedTopU = 12.0;     // the weight's integral runs to u = 12
constexpr int kTemperedUSteps = 600;       // by Simpson's rule in steps of 0.02 of u

// The weight's ln J (nats), mean and variance in u.
struct WeightMoments {
    double ln_j;
    double mean;
    double var;
};

// At z = 0 the weight, e^(kappa' u - u^2), is N(kappa' / 2, 1/2) truncated at u = 0, with closed forms: J =
// e^(kappa'^2 / 4) sqrt(pi) Phi(alpha), mean kappa' / 2 + lambda / sqrt(2) and variance (1 - alpha lambda -
// lambda^2) / 2, alpha = kappa' / sqrt(2), lambda = phi(alpha) / Phi(alpha).
WeightMoments empty_sample_weight(double kappa) {
    const double alpha = kappa / std::numbers::sqrt2;
    const double cdf = 0.5 * std::erfc(-alpha / std::numbers::sqrt2);
    const double ratio = std::exp(-0.5 * alpha * alpha) / (std::sqrt(2.0 * std::numbers::pi) * cdf);
    return {0.25 * kappa * kappa + std::log(std::sqrt(std::numbers::pi) * cdf),
            0.5 * kappa + ratio / std::numbers::sqrt2, 0.5 * (1.0 - alpha * ratio - ratio * ratio)};
}

constexpr std::size_t kTemperedKappa = kTemperedKappaSteps + 1;
constexpr std::size_t kTemperedSlice = (kTemperedYSteps + 1) * kTemperedKappa;

// One slice of nodes (one node of w): i (y = 0.01 i) x 201 + k (kappa' = 0.01 k), each quantity less its empty-sample
// value.
void build_slice(double w, WeightMoments* nodes) {
    constexpr std::size_t kY = kTemperedYSteps + 1;
    constexpr std::size_t kU = kTemperedUSteps + 1;
    constexpr double kStepY = kTemperedTopY / kTemperedYSteps;
    constexpr double kStepKappa = kTemperedTopKappa / kTemperedKappaSteps;
    constexpr double kStepU = kTemperedTopU / kTemperedUSteps;
    std::vector<double> u(kU);
    std::vector<double> simpson(kU);
    for (std::size_t j = 0; j < kU; ++j) {
        u[j] = static_cast<double>(j) * kStepU;
        simpson[j] = (j == 0 || j + 1 == kU) ? 1.0 : (j % 2 != 0 ? 4.0 : 2.0);
    }
    // e^(kappa' u) at every node of kappa': each row of y becomes all its entries by one matrix-vector product.
    std::vector<double> tilt(kTemperedKappa * kU);
    for (std::size_t k = 0; k < kTemperedKappa; ++k)
        for (std::size_t j = 0; j < kU; ++j) tilt[k * kU + j] = std::exp(static_cast<double>(k) * kStepKappa * u[j]);
    std::vector<WeightMoments> empty(kTemperedKappa);
    for (std::size_t k = 0; k < kTemperedKappa; ++k)
        empty[k] = empty_sample_weight(static_cast<double>(k) * kStepKappa);
    std::vector<double> base(kU);
    const double root_w = std::sqrt(w);
    for (std::size_t k = 0; k < kTemperedKappa; ++k) nodes[k] = {0.0, 0.0, 0.0};  // y = 0: the closed forms
    for (std::size_t i = 1; i < kY; ++i) {
        const double y = static_cast<double>(i) * kStepY;
        const double zeta = y / root_w;
        const double center = root_w * y;  // w zeta, near the weight's peak for large zeta
        const double shift = w * y * y;    // about the log weight's highest value, for scale
        for (std::size_t j = 0; j < kU; ++j)
            base[j] = simpson[j] * std::exp(-u[j] * u[j] + w * ln_i0(2.0 * u[j] * zeta) - shift);
        for (std::size_t k = 0; k < kTemperedKappa; ++k) {
            double s0 = 0.0;
            double s1 = 0.0;
            double s2 = 0.0;
            for (std::size_t j = 0; j < kU; ++j) {
                const double weight = base[j] * tilt[k * kU + j];
                const double d = u[j] - center;  // moments about the center, for precision
                s0 += weight;
                s1 += weight * d;
                s2 += weight * d * d;
            }
            const double offset = s1 / s0;
            nodes[i * kTemperedKappa + k] = {shift + std::log(s0 * kStepU / 3.0) - empty[k].ln_j,
                                             center + offset - empty[k].mean, s2 / s0 - offset * offset - empty[k].var};
        }
    }
}

// The slices of w's nodes over [0.1986, 0.2755], slice-major (slice x 80 601 + node). Built once, for every caller.
const std::vector<WeightMoments>& tempered_table() {
    static const std::vector<WeightMoments> table = [] {
        std::vector<WeightMoments> nodes(kTemperedWNodes * kTemperedSlice);
        for (int slice = 0; slice < kTemperedWNodes; ++slice) {
            const double w = kTemperedLowestW + (kTemperedHighestW - kTemperedLowestW) * slice / (kTemperedWNodes - 1);
            build_slice(w, nodes.data() + static_cast<std::size_t>(slice) * kTemperedSlice);
        }
        return nodes;
    }();
    return table;
}

// The slice at w = 1 (the untempered form), built on its first use only.
const std::vector<WeightMoments>& unit_weight_table() {
    static const std::vector<WeightMoments> table = [] {
        std::vector<WeightMoments> nodes(kTemperedSlice);
        build_slice(1.0, nodes.data());
        return nodes;
    }();
    return table;
}

// The weight's ln J_w, mean and variance at (y, kappa') in [0, 4] x [0, 2] for w in [0.1986, 0.2755] or w = 1:
// bilinear in (y, kappa') within each slice, linear in w between the two slices around it.
WeightMoments tempered_weight_moments(double y, double kappa, double w) {
    const double py = y * (kTemperedYSteps / kTemperedTopY);
    const double pk = kappa * (kTemperedKappaSteps / kTemperedTopKappa);
    const auto iy = static_cast<std::size_t>(std::min(static_cast<int>(py), kTemperedYSteps - 1));
    const auto ik = static_cast<std::size_t>(std::min(static_cast<int>(pk), kTemperedKappaSteps - 1));
    const double fy = py - static_cast<double>(iy);
    const double fk = pk - static_cast<double>(ik);
    const auto read = [&](const WeightMoments* nodes) {
        const WeightMoments& a = nodes[iy * kTemperedKappa + ik];
        const WeightMoments& b = nodes[iy * kTemperedKappa + ik + 1];
        const WeightMoments& c = nodes[(iy + 1) * kTemperedKappa + ik];
        const WeightMoments& d = nodes[(iy + 1) * kTemperedKappa + ik + 1];
        const auto blend = [fy, fk](double va, double vb, double vc, double vd) {
            return (1.0 - fy) * ((1.0 - fk) * va + fk * vb) + fy * ((1.0 - fk) * vc + fk * vd);
        };
        return WeightMoments{blend(a.ln_j, b.ln_j, c.ln_j, d.ln_j), blend(a.mean, b.mean, c.mean, d.mean),
                             blend(a.var, b.var, c.var, d.var)};
    };
    WeightMoments rest;
    if (w == 1.0) {
        rest = read(unit_weight_table().data());
    } else {
        const WeightMoments* nodes = tempered_table().data();
        const double pw = (w - kTemperedLowestW) / (kTemperedHighestW - kTemperedLowestW) * (kTemperedWNodes - 1);
        const int iw = std::clamp(static_cast<int>(pw), 0, kTemperedWNodes - 2);
        const double fw = pw - iw;
        const WeightMoments low = read(nodes + static_cast<std::size_t>(iw) * kTemperedSlice);
        const WeightMoments high = read(nodes + static_cast<std::size_t>(iw + 1) * kTemperedSlice);
        rest = {low.ln_j + fw * (high.ln_j - low.ln_j), low.mean + fw * (high.mean - low.mean),
                low.var + fw * (high.var - low.var)};
    }
    const WeightMoments empty = empty_sample_weight(kappa);
    return {empty.ln_j + rest.ln_j, empty.mean + rest.mean, empty.var + rest.var};
}

bool tempered_table_holds(double w) { return w == 1.0 || (w >= kTemperedLowestW && w <= kTemperedHighestW); }

// The sample's integrand in b, m = |s| > 0, after checking the arguments. The prior counts as exact where P = 0, or
// where it is narrower than 1e-9 of max(beta, 1) (kExactPrior): the score is then w Lambda(mu), off by about q
// (Lambda'^2 + Lambda'') / 2 nats (Lambda's derivatives in b at beta), and the posterior the prior, {mu, P}.
Integrand sample_integrand(double e_fs, double m, double mu_fs, double p_fs2, double v_prime_fs2, double w, int order) {
    if (!finite_and_not_negative(e_fs))
        throw std::invalid_argument("sample_evidence: e must be finite and not negative (FS)");
    if (!(v_prime_fs2 > 0.0) || !std::isfinite(v_prime_fs2))
        throw std::invalid_argument("sample_evidence: V' must be positive and finite (FS^2)");
    if (!finite_and_not_negative(mu_fs))
        throw std::invalid_argument("sample_evidence: mu must be finite and not negative (FS)");
    if (!finite_and_not_negative(p_fs2))
        throw std::invalid_argument("sample_evidence: P must be finite and not negative (FS^2)");
    if (order < 2 || order > kMaxOrder)
        throw std::invalid_argument("sample_evidence: the quadrature's order must be 2 to 20");
    const double root_v = std::sqrt(v_prime_fs2);
    const double beta = m * mu_fs / root_v;
    const double q = m * m * p_fs2 / v_prime_fs2;
    const double narrowest = kExactPrior * std::max(beta, 1.0);
    return {beta, q, e_fs / root_v, !(q > narrowest * narrowest), w};
}

// The score, the posterior and the mode of a sample with m = |s| > 0 (sample_evidence after its checks); tabulated =
// false computes every truncated node by Phi^-1 instead of reading the table.
SampleEvidence evaluate(double e_fs, double m, double mu_fs, double p_fs2, double v_prime_fs2, double w, int order,
                        std::optional<double> start_a_fs, bool tabulated) {
    const Integrand integrand = sample_integrand(e_fs, m, mu_fs, p_fs2, v_prime_fs2, w, order);
    if (integrand.exact) {
        const double b = integrand.beta;  // w Lambda(mu): the tempered likelihood at the prior's center
        return {w * (-b * b + ln_i0(2.0 * b * integrand.z)), {mu_fs, p_fs2}, mu_fs};
    }
    const double unit = std::sqrt(v_prime_fs2) / m;  // FS per unit of b
    const double mode = find_mode(integrand, start_a_fs ? std::optional<double>(*start_a_fs / unit) : std::nullopt);
    if (tempered_table_holds(w)) {
        const double s_w2 = w + 0.5 / integrand.q;
        const double s_w = std::sqrt(s_w2);
        const double kappa = integrand.beta / integrand.q;
        const double y = std::sqrt(w) * integrand.z / s_w;
        if (y <= kTemperedTopY && kappa / s_w <= kTemperedTopKappa) {
            const WeightMoments weight = tempered_weight_moments(y, kappa / s_w, w);
            // ln N(0; beta, q) - ln Phi(beta / sqrt(q)) + ln J_w(zeta, kappa') - ln s_w
            const double score = -0.5 * kappa * integrand.beta -
                                 0.5 * std::log(2.0 * std::numbers::pi * integrand.q) -
                                 std::log1p(-0.5 * std::erfc(integrand.beta / std::sqrt(2.0 * integrand.q))) +
                                 weight.ln_j - 0.5 * std::log(s_w2);
            return {score, {weight.mean / s_w * unit, weight.var / s_w2 * unit * unit}, mode * unit};
        }
    }
    const Fit fit = fit_posterior(integrand, order, mode, tabulated);
    const Moments b = fit_moments(fit, order);
    return {fit.llr, {b.mean * unit, b.var * unit * unit}, mode * unit};
}

SampleEvidence checked_sample_evidence(double e_fs, double s, double mu_fs, double p_fs2, double v_prime_fs2, double w,
                                       int order, std::optional<double> start_a_fs, bool tabulated) {
    if (!std::isfinite(s)) throw std::invalid_argument("sample_evidence: s must be finite (1)");
    if (!(w > 0.0) || !std::isfinite(w))
        throw std::invalid_argument("sample_evidence: w must be positive and finite (1)");
    if (start_a_fs && !std::isfinite(*start_a_fs))
        throw std::invalid_argument("sample_evidence: start_a_fs must be finite (FS)");
    if (s == 0.0) {
        // Checked as for a scored sample (with a template of 1), then nothing to add.
        (void)sample_integrand(e_fs, 1.0, mu_fs, p_fs2, v_prime_fs2, w, order);
        return {0.0, {mu_fs, p_fs2}, start_a_fs ? *start_a_fs : mu_fs};
    }
    // Lambda is even in b, so a negative s scores as |s|.
    return evaluate(e_fs, std::fabs(s), mu_fs, p_fs2, v_prime_fs2, w, order, start_a_fs, tabulated);
}

}  // namespace

double ln_i0(double x) {
    x = std::fabs(x);
    if (x <= kBesselSplit) {
        const double r = x / kBesselSplit;
        return std::log(polynomial(kSmall, r * r));
    }
    if (x == kInf) return kInf;
    return x + std::log(polynomial(kLarge, kBesselSplit / x) / std::sqrt(x));
}

SampleEvidence sample_evidence(double e_fs, double s, double mu_fs, double p_fs2, double v_prime_fs2, double w,
                               int order, std::optional<double> start_a_fs) {
    return checked_sample_evidence(e_fs, s, mu_fs, p_fs2, v_prime_fs2, w, order, start_a_fs, true);
}

SampleEvidence story_evidence(double e_fs, double s, double mu_fs, double p_fs2, double v_fs2, double sigma2_fs2, double w,
                              int order, std::optional<double> start_a_fs) {
    if (!(sigma2_fs2 > 0.0) || !std::isfinite(sigma2_fs2))
        throw std::invalid_argument("story_evidence: sigma_y^2 must be positive and finite (FS^2)");
    SampleEvidence evidence = sample_evidence(e_fs, s, mu_fs, p_fs2, v_fs2, w, order, start_a_fs);
    if (s == 0.0) return evidence;
    // e^2 / sigma_y^2 - e^2 / V = e^2 (V - sigma_y^2) / (sigma_y^2 V), written so that V = sigma_y^2 adds exactly 0.
    const double excess = v_fs2 - sigma2_fs2;
    evidence.score_nats += w * (e_fs * e_fs * excess / (sigma2_fs2 * v_fs2) - std::log1p(excess / sigma2_fs2));
    return evidence;
}

void build_evidence_tables(int order) {
    if (order < 2 || order > kMaxOrder)
        throw std::invalid_argument("build_evidence_tables: the quadrature's order must be 2 to 20");
    (void)tempered_table();
    (void)truncated_node_table(order);
}

namespace detail {

SampleEvidence sample_evidence_direct_nodes(double e_fs, double s, double mu_fs, double p_fs2, double v_prime_fs2,
                                            double w, int order, std::optional<double> start_a_fs) {
    return checked_sample_evidence(e_fs, s, mu_fs, p_fs2, v_prime_fs2, w, order, start_a_fs, false);
}

}  // namespace detail

}  // namespace kz4ap::viterbi
