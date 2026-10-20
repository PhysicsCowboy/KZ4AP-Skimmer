#include "kz4ap/viterbi/observation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
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

// --- The level's posterior, in b = m a / sqrt(v) ---

// A block and its level's prior N(beta, q) truncated to b >= 0: the integrand on b >= 0 is e^g(b), prior x
// likelihood without the prior's normalization, g(b) = -(b - beta)^2 / (2 q) - b^2 + ln I0(2 b z).
struct Block {
    double beta;  // the prior's mean, b
    double q;     // its variance, b^2
    double z;     // |S| / sqrt(v)
    bool exact;   // a level known exactly: p = 0, or sqrt(q) <= 1e-9 max(beta, 1)
    double g(double b) const {
        const double d = b - beta;
        return -0.5 * d * d / q - b * b + ln_i0(2.0 * b * z);
    }
};

constexpr int kNewtonSteps = 60;  // a bound for the safeguarded iteration, never reached on the tests
// A Newton step below this many spacings of doubles at b, relatively, ends the iteration: one can be no smaller.
constexpr double kSpacings = 4.0 * std::numeric_limits<double>::epsilon();
// sqrt(q): a prior narrower than this share of its center, max(beta, 1), is a level known exactly. A heuristic margin:
// the quadrature's error from rounding its nodes' offsets grows as epsilon b / sigma^, and is still 7e-9 nats at this
// width at b = 10 (5.7e-5 at 1e-12, 3.2e-3 at 1e-14, 1.09 nats from about 1e-16, where the nodes coincide).
constexpr double kExactLevel = 1e-9;
// sigma^: from here on the Gaussian at the mode has under Phi(-8.5) = 9.5e-18 of its mass below 0, under half the
// spacing of doubles at 1, so the truncated nodes are the standard ones; every rule's nodes (at most 7.62 for order
// 20) then lie above 0.
constexpr double kTruncationLimit = 8.5;

// The quadrature of a block's posterior: the nodes b_k and their shares of the integral, w_k e^(g(b_k) - g(b^) +
// s_k^2 / 2) over the largest of them, s_k the node's distance from the mode in sigma^, and Lambda_P.
struct Fit {
    std::array<double, kMaxOrder> b{};
    std::array<double, kMaxOrder> share{};  // the largest is 1
    double total = 0.0;                     // the sum of the shares
    double llr = 0.0;                       // Lambda_P, nats
};

// The mode b^ of the integrand e^g. start: where Newton's method starts, in units of b (clamped into its bracket); the
// bracket's top when empty.
double find_mode(const Block& block, std::optional<double> start) {
    const double beta = block.beta;
    const double q = block.q;
    const double z = block.z;
    // The mode b^ of g: Newton's method on g', by default from the precision-weighted mean of the prior (beta,
    // precision 1 / q) and the likelihood's peak (z, precision 2). There g' <= 0 since d ln I0 / dx < 1, while g'(0) =
    // beta / q >= 0, so b^ lies in the bracket [0, that mean]. It stops at a step below 1e-10 sigma^ or below a few
    // spacings of doubles at b, or one that leaves b where it is; a step past the bracket's top goes to the top, one
    // that would leave the bracket below, or one where g is not concave, bisects it instead, and one past b = 0 stops
    // there. A zero slope is the mode only where g is
    // concave: elsewhere it can only be b = 0 with beta = 0 (g'(0) = beta / q), a minimum once z^2 > 1 + 1/(2 q), and
    // the mode lies above it.
    double low = 0.0;
    double high = q <= 1.0 ? (beta + 2.0 * q * z) / (1.0 + 2.0 * q) : (beta / q + 2.0 * z) / (1.0 / q + 2.0);
    double b = start ? std::clamp(*start, low, high) : high;
    for (int step = 0; step < kNewtonSteps; ++step) {
        const Slopes s = ln_i0_slopes(2.0 * b * z);
        const double slope = -(b - beta) / q - 2.0 * b + 2.0 * z * s.first;  // g'(b)
        const double curvature = 1.0 / q + 2.0 - 4.0 * z * z * s.second;    // -g''(b)
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

// The adaptive quadrature at the mode b.
Fit fit_posterior(const Block& block, int order, double b) {
    const double beta = block.beta;
    const double q = block.q;
    const double z = block.z;
    // -g''(b^) = 1 / q + excess. It is positive at a maximum; the fallback, the curvature of prior x e^-b^2 (so that
    // sigma^ < 1 / sqrt(2)), guards a degenerate one and a scale beyond the doubles' range.
    double excess = 2.0 - 4.0 * z * z * ln_i0_slopes(2.0 * b * z).second;
    if (!(q * excess > -1.0) || !std::isfinite(std::sqrt(q / (1.0 + q * excess)))) excess = 2.0;
    const Rule& rule = rule_of_order(order);
    const double g_mode = block.g(b);
    Fit fit;
    // The nodes' terms in the log, ln w_k + g(b_k) - g(b^) + s_k^2 / 2, summed by log-sum-exp, so that no share
    // underflows into an empty sum. A term that is not a number (a node beyond the doubles' range) counts for nothing;
    // if no term counts, the nodes are laid again at the fallback's scale, within a few units of b of the mode.
    std::array<double, kMaxOrder> log_term{};
    double sigma = 0.0;
    double below = 0.0;
    double largest = -kInf;
    for (int attempt = 0; attempt < 2 && !(largest > -kInf); ++attempt) {
        if (attempt > 0) excess = 2.0;
        sigma = std::sqrt(q / (1.0 + q * excess));
        const double tau = b / sigma;
        below = tau < kTruncationLimit ? 0.5 * std::erfc(tau / std::numbers::sqrt2) : 0.0;
        for (std::size_t k = 0; k < static_cast<std::size_t>(order); ++k) {
            // The node's quantile, in the Gaussian at the mode truncated at b = 0 when that truncation matters.
            double s = rule.node[k];
            if (below > 0.0)
                s = s < 0.0 ? inverse_normal_cdf(below + (1.0 - below) * rule.tail[k])
                            : -inverse_normal_cdf((1.0 - below) * rule.tail[k]);
            fit.b[k] = std::max(0.0, b + sigma * s);
            const double term = rule.log_weight[k] + block.g(fit.b[k]) - g_mode + 0.5 * s * s;
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

// --- The broad-prior regime: an exact rescaling and tables ---
//
// In b, N(b; beta, q) = N(0; beta, q) e^(kappa b - b^2 / (2 q)), kappa = beta / q, exactly. With the likelihood's
// e^-b^2 the prior's e^(-b^2 / (2 q)) makes e^(-s^2 b^2), s = sqrt(1 + 1 / (2 q)), and u = s b maps the integral onto
// the weight w(u) = e^(kappa' u - u^2) I0(2 u z') at kappa' = kappa / s and z' = z / s:
//     integral_0^inf N(b; beta, q) e^(-b^2) I0(2 b z) db = N(0; beta, q) J(z', kappa') / s,
//     J(z', kappa') = integral_0^inf w(u) du,
// and the posterior of b is the weight's distribution in u scaled by 1 / s: its mean and variance are the weight's
// over s and s^2. This is exact at every q (derived). Tables of ln J and the weight's mean and variance over
// z' in [0, 4] and kappa' in [0, 1] serve every prior that maps into them. The curvature-scaled nodes fail around
// z' = 1 at kappa' = 0, where -g''(0) = 2 s^2 (1 - z'^2) vanishes (derived); the bounds are a heuristic margin around
// that point, and outside them the nodes hold within 0.002 nats where measured (observation.hpp).

constexpr double kRegimeTopZ = 4.0;      // z', at most
constexpr double kRegimeTopKappa = 1.0;  // kappa', at most
constexpr int kRegimeZSteps = 400;       // the tables' nodes: z' = 0, 0.01, ..., 4
constexpr int kRegimeKappaSteps = 100;   // and kappa' = 0, 0.01, ..., 1
constexpr double kRegimeTopU = 12.0;     // the weight's integral runs to u = 12, e^-56 of its peak at the far corner
constexpr int kRegimeUSteps = 600;       // by Simpson's rule in steps of 0.02 of u

// The weight's ln J (nats), mean and variance in u.
struct WeightMoments {
    double ln_j;
    double mean;
    double var;
};

// At z' = 0 the weight, e^(kappa' u - u^2), is N(kappa' / 2, 1/2) truncated at u = 0, with closed forms: J =
// e^(kappa'^2 / 4) sqrt(pi) Phi(alpha), mean kappa' / 2 + lambda / sqrt(2) and variance (1 - alpha lambda -
// lambda^2) / 2, alpha = kappa' / sqrt(2), lambda = phi(alpha) / Phi(alpha).
WeightMoments empty_block_weight(double kappa) {
    const double alpha = kappa / std::numbers::sqrt2;
    const double cdf = 0.5 * std::erfc(-alpha / std::numbers::sqrt2);
    const double ratio = std::exp(-0.5 * alpha * alpha) / (std::sqrt(2.0 * std::numbers::pi) * cdf);
    return {0.25 * kappa * kappa + std::log(std::sqrt(std::numbers::pi) * cdf),
            0.5 * kappa + ratio / std::numbers::sqrt2, 0.5 * (1.0 - alpha * ratio - ratio * ratio)};
}

// The tables hold each quantity less its value on an empty block, so that they are 0 at z' = 0 and a block of nothing
// reads the closed forms exactly. Built once, on first use, for every decoder: 40 501 nodes of 3 doubles, 0.97 MB.
struct RegimeTable {
    std::vector<WeightMoments> nodes;  // z'-major: i (z' = 0.01 i) x 101 + k (kappa' = 0.01 k)
};

const RegimeTable& regime_table() {
    static const RegimeTable table = [] {
        constexpr std::size_t kZ = kRegimeZSteps + 1;
        constexpr std::size_t kKappa = kRegimeKappaSteps + 1;
        constexpr std::size_t kU = kRegimeUSteps + 1;
        constexpr double kStepZ = kRegimeTopZ / kRegimeZSteps;
        constexpr double kStepKappa = kRegimeTopKappa / kRegimeKappaSteps;
        constexpr double kStepU = kRegimeTopU / kRegimeUSteps;
        RegimeTable built;
        built.nodes.assign(kZ * kKappa, {0.0, 0.0, 0.0});
        std::vector<double> u(kU);
        std::vector<double> simpson(kU);
        for (std::size_t j = 0; j < kU; ++j) {
            u[j] = static_cast<double>(j) * kStepU;
            simpson[j] = (j == 0 || j + 1 == kU) ? 1.0 : (j % 2 != 0 ? 4.0 : 2.0);
        }
        // e^(kappa' u) at every node of kappa': each row of z' becomes all its entries by one matrix-vector product.
        std::vector<double> tilt(kKappa * kU);
        for (std::size_t k = 0; k < kKappa; ++k)
            for (std::size_t j = 0; j < kU; ++j)
                tilt[k * kU + j] = std::exp(static_cast<double>(k) * kStepKappa * u[j]);
        std::vector<double> base(kU);
        for (std::size_t i = 1; i < kZ; ++i) {  // the row z' = 0 is the closed forms: 0 in the tables
            const double z = static_cast<double>(i) * kStepZ;
            for (std::size_t j = 0; j < kU; ++j)
                base[j] = simpson[j] * std::exp(-u[j] * u[j] + ln_i0(2.0 * u[j] * z) - z * z);  // scaled by e^-z^2
            for (std::size_t k = 0; k < kKappa; ++k) {
                double s0 = 0.0;
                double s1 = 0.0;
                double s2 = 0.0;
                for (std::size_t j = 0; j < kU; ++j) {
                    const double w = base[j] * tilt[k * kU + j];
                    const double d = u[j] - z;  // moments about z, near the weight's center, for precision
                    s0 += w;
                    s1 += w * d;
                    s2 += w * d * d;
                }
                const double offset = s1 / s0;
                const WeightMoments empty = empty_block_weight(static_cast<double>(k) * kStepKappa);
                built.nodes[i * kKappa + k] = {z * z + std::log(s0 * kStepU / 3.0) - empty.ln_j,
                                               z + offset - empty.mean, s2 / s0 - offset * offset - empty.var};
            }
        }
        return built;
    }();
    return table;
}

// The weight's ln J, mean and variance at (z', kappa') in [0, 4] x [0, 1], by bilinear interpolation of the tables
// added to the closed forms of an empty block.
WeightMoments weight_moments(double z, double kappa) {
    constexpr std::size_t kKappa = kRegimeKappaSteps + 1;
    const RegimeTable& table = regime_table();
    const double pz = z * (kRegimeZSteps / kRegimeTopZ);
    const double pk = kappa * (kRegimeKappaSteps / kRegimeTopKappa);
    const auto iz = static_cast<std::size_t>(std::min(static_cast<int>(pz), kRegimeZSteps - 1));
    const auto ik = static_cast<std::size_t>(std::min(static_cast<int>(pk), kRegimeKappaSteps - 1));
    const double fz = pz - static_cast<double>(iz);
    const double fk = pk - static_cast<double>(ik);
    const WeightMoments& a = table.nodes[iz * kKappa + ik];
    const WeightMoments& b = table.nodes[iz * kKappa + ik + 1];
    const WeightMoments& c = table.nodes[(iz + 1) * kKappa + ik];
    const WeightMoments& d = table.nodes[(iz + 1) * kKappa + ik + 1];
    const auto blend = [fz, fk](double va, double vb, double vc, double vd) {
        return (1.0 - fz) * ((1.0 - fk) * va + fk * vb) + fz * ((1.0 - fk) * vc + fk * vd);
    };
    const WeightMoments empty = empty_block_weight(kappa);
    return {empty.ln_j + blend(a.ln_j, b.ln_j, c.ln_j, d.ln_j), empty.mean + blend(a.mean, b.mean, c.mean, d.mean),
            empty.var + blend(a.var, b.var, c.var, d.var)};
}

void check_block(double s_abs, int m, double v) {
    if (m < 1) throw std::invalid_argument("block evidence: m must be at least 1 channel sample");
    if (!(v > 0.0) || !std::isfinite(v))
        throw std::invalid_argument("block evidence: v must be positive and finite (FS^2)");
    if (!finite_and_not_negative(s_abs))
        throw std::invalid_argument("block evidence: s_abs must be finite and not negative (FS)");
}

// The block in b after checking the arguments. Its level counts as exact where p = 0, or where the prior is narrower
// than 1e-9 of max(beta, 1) (kExactLevel): Lambda_P is then block_llr at mu, off by about q (Lambda'^2 + Lambda'') / 2
// nats (Lambda's derivatives in b at beta), and the posterior the prior, {mu, p}. For mu > 0 the true posterior differs
// from that by about q relatively; for mu = 0 it is a half-normal (mean 0.80 sqrt(p), variance 0.36 p), off by order 1
// relatively but by under 1e-9 in units of b.
Block uncertain_block(double s_abs, int m, double mu, double p, double v, int order, int lowest_order) {
    check_block(s_abs, m, v);
    if (!finite_and_not_negative(mu))
        throw std::invalid_argument("uncertain level: mu must be finite and not negative (FS)");
    if (!finite_and_not_negative(p))
        throw std::invalid_argument("uncertain level: p must be finite and not negative (FS^2)");
    if (order < lowest_order || order > kMaxOrder)
        throw std::invalid_argument("uncertain level: the quadrature's order must be 1 (2 for the posterior) to 20");
    const double root_v = std::sqrt(v);
    const double beta = m * mu / root_v;
    const double q = static_cast<double>(m) * static_cast<double>(m) * p / v;
    const double narrowest = kExactLevel * std::max(beta, 1.0);
    return {beta, q, s_abs / root_v, !(q > narrowest * narrowest)};
}

// The one evaluation behind block_llr_uncertain, level_posterior and block_evidence, after checking the arguments
// (orders from lowest_order to 20): the evidence, the level's moments and the mode, in FS. The mode comes from
// Newton's method; the evidence and the moments from the broad-prior regime's tables where (z', kappa') lies in them,
// and from the adaptive quadrature elsewhere.
BlockEvidence evaluate(double s_abs, int m, double mu, double p, double v, int order, int lowest_order,
                       std::optional<double> start_a) {
    const Block block = uncertain_block(s_abs, m, mu, p, v, order, lowest_order);
    if (start_a && !std::isfinite(*start_a))
        throw std::invalid_argument("block evidence: start_a must be finite (FS)");
    if (block.exact) return {block_llr(s_abs, m, mu, v), {mu, p}, mu};
    const double unit = std::sqrt(v) / m;  // FS per unit of b
    const double mode = find_mode(block, start_a ? std::optional<double>(*start_a / unit) : std::nullopt);
    const double s = std::sqrt(1.0 + 0.5 / block.q);
    const double kappa = block.beta / block.q;
    if (block.z / s <= kRegimeTopZ && kappa / s <= kRegimeTopKappa) {
        const WeightMoments w = weight_moments(block.z / s, kappa / s);
        // ln N(0; beta, q) - ln Phi(beta / sqrt(q)) + ln J(z', kappa') - ln s
        const double llr = -0.5 * kappa * block.beta - 0.5 * std::log(2.0 * std::numbers::pi * block.q) -
                           std::log1p(-0.5 * std::erfc(block.beta / std::sqrt(2.0 * block.q))) + w.ln_j -
                           0.5 * std::log1p(0.5 / block.q);
        return {llr, {w.mean / s * unit, w.var / (s * s) * unit * unit}, mode * unit};
    }
    const Fit fit = fit_posterior(block, order, mode);
    double mean = 0.0;
    for (std::size_t k = 0; k < static_cast<std::size_t>(order); ++k) mean += fit.share[k] * fit.b[k];
    mean /= fit.total;
    double var = 0.0;
    for (std::size_t k = 0; k < static_cast<std::size_t>(order); ++k) {
        const double d = fit.b[k] - mean;
        var += fit.share[k] * d * d;
    }
    var /= fit.total;
    return {fit.llr, {mean * unit, var * unit * unit}, mode * unit};
}

// --- The flat prior ---

constexpr double kS500Hz = 500.0;          // S500: the carrier's power over the noise power in 500 Hz
constexpr double kTableTopZ = 40.0;
constexpr int kTableSteps = 4000;          // the table's nodes: z = 0, 0.01, ..., 40
constexpr double kTableNodesPerZ = 100.0;  // 1 / the table's step of 0.01
constexpr double kLevelStepDb = 0.01;      // dB of x (x in dB S500): the step of the integral over the level x
constexpr double kHighestLowerB = 6.0;     // b at level_min_db, at most, for every block
constexpr double kAboveZ = 10.0;           // the integral starts at b = z + 10: e^Lambda is e^-100 of its peak there
constexpr double kNegligible = 1e-17;      // a rest of the integral below this share of it is left out

}  // namespace

std::vector<double> sum_noise_hz(std::span<const double> taps, double input_rate_hz, int decimation, int max_samples) {
    if (taps.empty()) throw std::invalid_argument("sum_noise_hz: no taps");
    if (!(input_rate_hz > 0.0) || !std::isfinite(input_rate_hz))
        throw std::invalid_argument("sum_noise_hz: input_rate_hz must be positive and finite (Hz)");
    if (decimation < 1) throw std::invalid_argument("sum_noise_hz: decimation must be at least 1");
    if (max_samples < 0) throw std::invalid_argument("sum_noise_hz: max_samples must not be negative");
    // R_k = input_rate_hz x sum_j h[j] h[j + D k], Hz, at every lag at which the taps overlap.
    const auto lag_step = static_cast<std::size_t>(decimation);
    std::vector<double> r((taps.size() - 1) / lag_step + 1);
    for (std::size_t k = 0; k < r.size(); ++k) {
        double sum = 0.0;
        for (std::size_t j = 0; j + k * lag_step < taps.size(); ++j) sum += taps[j] * taps[j + k * lag_step];
        r[k] = input_rate_hz * sum;
    }
    std::vector<double> c(static_cast<std::size_t>(max_samples) + 1, 0.0);
    for (std::size_t m = 1; m < c.size(); ++m) {
        double sum = static_cast<double>(m) * r[0];
        for (std::size_t k = 1; k < m && k < r.size(); ++k) sum += 2.0 * static_cast<double>(m - k) * r[k];
        c[m] = sum;
    }
    return c;
}

double ln_i0(double x) {
    x = std::fabs(x);
    if (x <= kBesselSplit) {
        const double r = x / kBesselSplit;
        return std::log(polynomial(kSmall, r * r));
    }
    if (x == kInf) return kInf;
    return x + std::log(polynomial(kLarge, kBesselSplit / x) / std::sqrt(x));
}

double block_llr(double s_abs, int m, double a, double v) {
    check_block(s_abs, m, v);
    if (!finite_and_not_negative(a)) throw std::invalid_argument("block_llr: a must be finite and not negative (FS)");
    const double root_v = std::sqrt(v);
    const double b = m * a / root_v;
    return -b * b + ln_i0(2.0 * b * (s_abs / root_v));
}

double block_llr_uncertain(double s_abs, int m, double mu, double p, double v, int order) {
    return evaluate(s_abs, m, mu, p, v, order, 1, std::nullopt).llr;
}

Moments level_posterior(double s_abs, int m, double mu, double p, double v, int order) {
    return evaluate(s_abs, m, mu, p, v, order, 2, std::nullopt).level;
}

BlockEvidence block_evidence(double s_abs, int m, double mu, double p, double v, int order,
                             std::optional<double> start_a) {
    return evaluate(s_abs, m, mu, p, v, order, 2, start_a);
}

FlatPrior::FlatPrior(const ViterbiConfig& config, std::span<const double> c, int max_pieces, int samples_per_piece)
    : max_pieces_(max_pieces), samples_per_piece_(samples_per_piece), large_z_constant_(0.0) {
    if (max_pieces < 1) throw std::invalid_argument("flat prior: max_pieces must be at least 1");
    if (samples_per_piece < 1) throw std::invalid_argument("flat prior: samples_per_piece must be at least 1");
    const auto pieces_count = static_cast<std::size_t>(max_pieces);
    const auto piece_samples = static_cast<std::size_t>(samples_per_piece);
    if (c.size() <= piece_samples * pieces_count)
        throw std::invalid_argument("flat prior: c must reach m = samples_per_piece x max_pieces channel samples");
    if (!std::isfinite(config.level_min_db) || !std::isfinite(config.level_max_db) ||
        !(config.level_min_db < config.level_max_db))
        throw std::invalid_argument("flat prior: the level range must be finite and not empty (dB S500)");
    large_z_constant_ = std::log(20.0 / (std::numbers::ln10 * (config.level_max_db - config.level_min_db)));

    // u = ln b at the ends of the range, for each block length: ln b = x ln 10 / 20 + ln(500 Hz m^2 / c(m)) / 2.
    const double u_per_db = std::numbers::ln10 / 20.0;
    std::vector<double> lower(pieces_count);
    for (std::size_t n = 1; n <= pieces_count; ++n) {
        const std::size_t m = piece_samples * n;
        if (!(c[m] > 0.0) || !std::isfinite(c[m]))
            throw std::invalid_argument("flat prior: c(m) must be positive and finite (Hz)");
        const double half_ln_gain = 0.5 * std::log(kS500Hz * static_cast<double>(m) * static_cast<double>(m) / c[m]);
        lower[n - 1] = config.level_min_db * u_per_db + half_ln_gain;
        const double upper = config.level_max_db * u_per_db + half_ln_gain;
        if (lower[n - 1] > std::log(kHighestLowerB) || upper < std::log(kTableTopZ + kAboveZ))
            throw std::invalid_argument(
                "flat prior: for every block, b must be at most 6 at level_min_db and at least 50 at level_max_db");
    }

    // The grid in u, in steps of 0.01 dB of x: the nodes are whole multiples of the step (b = 1 is one), so that an
    // entry depends only on its block's m, and j counts them from the one at or below the lowest lower limit up to
    // b = 40 + 10. Each block length's lower limit lies in a cell [u_j, u_j+1), at a fraction of the step; they are
    // read from the top down (ties by length).
    const double step = u_per_db * kLevelStepDb;
    const auto first = static_cast<long long>(std::floor(*std::min_element(lower.begin(), lower.end()) / step));
    const auto top = static_cast<std::size_t>(
        static_cast<long long>(std::ceil(std::log(kTableTopZ + kAboveZ) / step)) - first);
    std::vector<double> grid_b(top + 1);
    for (std::size_t j = 0; j <= top; ++j)
        grid_b[j] = std::exp(static_cast<double>(first + static_cast<long long>(j)) * step);
    struct Limit {
        std::size_t cell;
        double fraction;
        std::size_t pieces;
    };
    std::vector<Limit> limits;
    limits.reserve(pieces_count);
    for (std::size_t n = 1; n <= pieces_count; ++n) {
        const double position = lower[n - 1] / step;
        const double cell = std::floor(position);
        limits.push_back({static_cast<std::size_t>(static_cast<long long>(cell) - first), position - cell, n});
    }
    std::sort(limits.begin(), limits.end(), [](const Limit& a, const Limit& b) {
        return a.cell != b.cell ? a.cell > b.cell : a.pieces > b.pieces;
    });

    // For each z, the integrals of e^(Lambda - z^2), (b - z) e^(Lambda - z^2) and (b - z)^2 e^(Lambda - z^2) over u
    // from b = z + 10 down, by the trapezoid rule; at each lower limit they give Lambda_flat - z^2 and the posterior.
    const std::size_t row = static_cast<std::size_t>(kTableSteps) + 1;
    nodes_.resize(pieces_count * row);
    for (std::size_t i = 0; i < row; ++i) {
        const double z = static_cast<double>(i) / kTableNodesPerZ;
        const auto store = [&](std::size_t pieces, double f0, double f1, double f2) {
            const double mean_offset = f1 / f0;
            nodes_[(pieces - 1) * row + i] = {large_z_constant_ + std::log(f0), z + mean_offset,
                                              f2 / f0 - mean_offset * mean_offset};
        };
        const auto start = std::min(
            top, static_cast<std::size_t>(static_cast<long long>(std::ceil(std::log(z + kAboveZ) / step)) - first));
        double f0 = 0.0;  // the integrals from the start down to u_j
        double f1 = 0.0;
        double f2 = 0.0;
        double e0 = 0.0;  // the integrands at u_j+1
        double e1 = 0.0;
        double e2 = 0.0;
        std::size_t next = 0;
        for (std::size_t j = start + 1; j-- > 0;) {
            const double b = grid_b[j];
            const double d = b - z;
            const double e = std::exp(-b * b + ln_i0(2.0 * b * z) - z * z);
            const double above0 = f0;
            const double above1 = f1;
            const double above2 = f2;
            if (j < start) {
                f0 += 0.5 * step * (e + e0);
                f1 += 0.5 * step * (d * e + e1);
                f2 += 0.5 * step * (d * d * e + e2);
            }
            for (; next < limits.size() && limits[next].cell == j; ++next) {
                const double f = limits[next].fraction;  // u_lo lies f steps above u_j
                store(limits[next].pieces, f0 - f * (f0 - above0), f1 - f * (f1 - above1), f2 - f * (f2 - above2));
            }
            // Below the peak e^Lambda falls with u: once what lies below u_j, at most e (u_j - u_0), cannot reach
            // 1e-17 of the integral, the remaining lower limits read the integral as it stands.
            if (j < start && e <= e0 && e * static_cast<double>(j) * step < kNegligible * f0) {
                for (; next < limits.size(); ++next) store(limits[next].pieces, f0, f1, f2);
                break;
            }
            e0 = e;
            e1 = d * e;
            e2 = d * d * e;
        }
    }
}

const FlatPrior::Node* FlatPrior::row(int pieces, double z) const {
    if (pieces < 1 || pieces > max_pieces_) throw std::out_of_range("flat prior: pieces must be 1 ... max_pieces");
    if (!finite_and_not_negative(z)) throw std::invalid_argument("flat prior: z must be finite and not negative");
    return nodes_.data() + static_cast<std::size_t>(pieces - 1) * (static_cast<std::size_t>(kTableSteps) + 1);
}

double FlatPrior::llr(int pieces, double z) const {
    const Node* nodes = row(pieces, z);
    if (z > kTableTopZ) return z * z - std::numbers::ln2 - 2.0 * std::log(z) + large_z_constant_;
    const double position = z * kTableNodesPerZ;
    const int cell = std::min(static_cast<int>(position), kTableSteps - 1);
    const double fraction = position - cell;
    const Node& a = nodes[cell];
    const Node& b = nodes[cell + 1];
    return z * z + a.llr_less_z2 + fraction * (b.llr_less_z2 - a.llr_less_z2);
}

Moments FlatPrior::posterior(int pieces, double z) const {
    const Node* nodes = row(pieces, z);
    // The posterior of b, proportional to e^-(b - z)^2 b^(-3/2) for large z, by Laplace's method one order further.
    if (z > kTableTopZ) return {z - 0.75 / z, 0.5 + 0.375 / (z * z)};
    const double position = z * kTableNodesPerZ;
    const int cell = std::min(static_cast<int>(position), kTableSteps - 1);
    const double fraction = position - cell;
    const Node& a = nodes[cell];
    const Node& b = nodes[cell + 1];
    return {a.mean + fraction * (b.mean - a.mean), a.var + fraction * (b.var - a.var)};
}

}  // namespace kz4ap::viterbi
