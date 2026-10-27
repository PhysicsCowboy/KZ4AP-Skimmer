// The newborn integral (newborn.hpp; design notes, part 8 as amended 2026-10-26, D21 and D22; plan 1b, Task 9).
// This file reads the generated weight table (through sample_weight), so kz4ap-viterbi-tables must not need it
// (Ruling 76).
#include "kz4ap/viterbi/newborn.hpp"

#include "kz4ap/viterbi/observation.hpp"
#include "kz4ap/viterbi/weight.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace kz4ap::viterbi {

namespace {

bool positive_finite(double x) { return x > 0.0 && std::isfinite(x); }

// Above this x, G comes from the large-x series rather than ln_i0, whose ln_i0(x) - x loses about x x 1.1e-16 nats to
// rounding (1.1e-10 nats at 10^6; header, 3).
constexpr double kSeriesFromX = 1e6;

// G(y) = ln I0(e^y) - e^y + y / 2 (nats).
double g_of(double y) {
    const double x = std::exp(y);
    if (x <= kSeriesFromX) return ln_i0(x) - x + 0.5 * y;
    // ln I0(x) = x - (1/2) ln(2 pi x) + ln(1 + 1/(8x) + 9/(128 x^2) + ...) (Abramowitz and Stegun 9.7.1); the next
    // term, 225/(3072 x^3), is below 1e-19 here.
    const double r = 1.0 / (8.0 * x);
    return -0.5 * std::log(2.0 * std::numbers::pi) + std::log1p(r + 4.5 * r * r);
}

// The 4-point Lagrange weights for the nodes at -1, 0, 1, 2 at the fraction t in [0, 1).
struct Lagrange {
    double c0, c1, c2, c3;
};
Lagrange lagrange(double t) {
    return {-t * (t - 1.0) * (t - 2.0) / 6.0, (t + 1.0) * (t - 1.0) * (t - 2.0) / 2.0,
            -(t + 1.0) * t * (t - 2.0) / 2.0, (t + 1.0) * t * (t - 1.0) / 6.0};
}

}  // namespace

// --- NewbornTables ---

NewbornTables::NewbornTables(const OutputNoise& noise, double sigma2_per_n0_hz, std::optional<double> fixed_weight,
                             int points, double span_nepers)
    : points_(points), span_(span_nepers), fixed_weight_(fixed_weight), sigma2_per_n0_hz_(sigma2_per_n0_hz),
      noise_(noise) {
    if (!positive_finite(sigma2_per_n0_hz) || !positive_finite(noise.sigma2_per_n0_hz))
        throw std::invalid_argument("NewbornTables: sigma_y^2 / N0 must be positive and finite (Hz)");
    if (fixed_weight && !positive_finite(*fixed_weight))
        throw std::invalid_argument("NewbornTables: a fixed weight must be positive and finite");
    if (points < 2) throw std::invalid_argument("NewbornTables: the grid needs at least 2 points");
    if (!positive_finite(span_nepers))
        throw std::invalid_argument("NewbornTables: the prior's span must be positive and finite (nepers)");
    step_ = span_nepers / (points - 1);
    ln_intervals_ = std::log(static_cast<double>(points - 1));

    // G at steps of h/4 over [-20, +165] nepers of ln x at the default span; below, down to the lowest live point's ln x
    // where the table is read (x at the top at least 1e-3, the span below it, the stencil's 2 steps); above, to 40 nepers
    // plus the span, where G is constant to double precision (e^-40 = 4e-18), so that a clamped read is exact.
    g_step_ = step_ / 4.0;
    g_lo_ = std::min(kNewbornGLowestLnX, std::log(kNewbornSmallX) - span_nepers - 2.0 * g_step_);
    const double g_hi = std::max(kNewbornGHighestLnX, 40.0 + span_nepers + 4.0 * g_step_);
    const auto g_nodes = static_cast<std::size_t>(std::ceil((g_hi - g_lo_) / g_step_)) + 1;
    g_.resize(g_nodes);
    for (std::size_t j = 0; j < g_nodes; ++j) g_[j] = g_of(g_lo_ + static_cast<double>(j) * g_step_);

    // w at steps of h in ln q (Ruling 81), reaching twice the grid's span plus two steps beyond the curve's clamps at
    // q = 1e-4 and 1e4, so that a base offset clamped to either end reads only the constant w there.
    w_step_ = step_;  // h (Ruling 81; D22 stated 2h): point i reads node w_node + 2 i
    const double lq_lo = std::log(kWeightLowestQ);
    const double lq_hi = std::log(kWeightHighestQ);
    w_lo_ = lq_lo - w_step_ * (2 * points + 2);
    const auto w_nodes =
        static_cast<std::size_t>(std::ceil((lq_hi + w_step_ * (2 * points + 2) - w_lo_) / w_step_)) + 1;
    w_.resize(w_nodes);
    for (std::size_t j = 0; j < w_nodes; ++j)
        w_[j] = fixed_weight ? *fixed_weight : sample_weight(std::exp(w_lo_ + static_cast<double>(j) * w_step_));

    e1_.resize(static_cast<std::size_t>(points));
    e2_.resize(static_cast<std::size_t>(points));
    for (int i = 0; i < points; ++i) {
        e1_[static_cast<std::size_t>(i)] = std::exp(i * step_);
        e2_[static_cast<std::size_t>(i)] = std::exp(2.0 * i * step_);
    }
}

double NewbornTables::ln_i0_from_table(double x) const {
    if (!positive_finite(x)) throw std::invalid_argument("NewbornTables::ln_i0_from_table: x must be positive and finite");
    if (x < kNewbornSmallX) return 0.25 * x * x;
    const double y = std::log(x);
    const double position = (y - g_lo_) / g_step_;
    auto node = static_cast<long long>(std::floor(position));
    double t = position - static_cast<double>(node);
    const auto top = static_cast<long long>(g_.size()) - 3;
    if (node < 1) node = 1, t = 0.0;
    if (node > top) node = top, t = 0.0;
    const Lagrange c = lagrange(t);
    const auto j = static_cast<std::size_t>(node);
    return x - 0.5 * y + c.c0 * g_[j - 1] + c.c1 * g_[j] + c.c2 * g_[j + 1] + c.c3 * g_[j + 2];
}

double NewbornTables::weight_from_table(double ln_q) const {
    const double position = (ln_q - w_lo_) / w_step_;
    if (!(position > 0.0)) return w_.front();
    auto node = static_cast<std::size_t>(position);
    if (node >= w_.size() - 1) return w_.back();
    const double t = position - static_cast<double>(node);
    return w_[node] + t * (w_[node + 1] - w_[node]);
}

// --- NewbornIntegral ---

NewbornIntegral::NewbornIntegral(const NewbornTables& tables, double u_lo, double u_hi, int points,
                                 double live_range_nats)
    : tables_(&tables), u_lo_(u_lo), live_range_(live_range_nats), lo_(0), hi_(points - 1) {
    if (!std::isfinite(u_lo) || !std::isfinite(u_hi) || !(u_hi > u_lo))
        throw std::invalid_argument("NewbornIntegral: the prior's bounds must be finite and increasing (nepers)");
    if (points != tables.points())
        throw std::invalid_argument("NewbornIntegral: the grid's points must be the tables' (" +
                                    std::to_string(tables.points()) + ")");
    if (std::fabs((u_hi - u_lo) - tables.span_nepers()) > 1e-9 * tables.span_nepers())
        throw std::invalid_argument("NewbornIntegral: the prior's width u_hi - u_lo must be the tables' span (" +
                                    std::to_string(tables.span_nepers()) + " nepers)");
    if (!(live_range_nats > 0.0)) throw std::invalid_argument("NewbornIntegral: the live range must be positive (nats)");
    h_ = (u_hi - u_lo) / (points - 1);
    a_lo_ = std::exp(u_lo);
    f_.assign(static_cast<std::size_t>(points), 0.0);
    depth_.assign(static_cast<std::size_t>(points), 0.0f);
    // The prior's own trapezoid moments (every F_i = 0).
    double s0 = 0.0;
    double s1 = 0.0;
    double s2 = 0.0;
    for (int i = 0; i < points; ++i) {
        const double c = (i == 0 || i == points - 1) ? 0.5 : 1.0;
        const double d = i * h_;
        s0 += c;
        s1 += c * d;
        s2 += c * d * d;
    }
    mean_u_ = u_lo + s1 / s0;
    sigma_u_ = std::sqrt(std::max(0.0, s2 / s0 - (s1 / s0) * (s1 / s0)));
    narrowest_sigma_u_ = std::numeric_limits<double>::infinity();
}

void NewbornIntegral::add(double e_fs, double s, double v_prime_fs2, double sigma2_fs2) {
    if (!(e_fs >= 0.0) || !std::isfinite(e_fs))
        throw std::invalid_argument("NewbornIntegral::add: e must be finite and not negative (FS)");
    if (!std::isfinite(s)) throw std::invalid_argument("NewbornIntegral::add: s must be finite");
    if (!positive_finite(v_prime_fs2) || !positive_finite(sigma2_fs2))
        throw std::invalid_argument("NewbornIntegral::add: V' and sigma_y^2 must be positive and finite (FS^2)");
    if (s == 0.0) {  // no evidence: Lambda = 0 at every amplitude
        last_score_ = 0.0;
        return;
    }
    const NewbornTables& t = *tables_;
    const int points = t.points_;
    const double s_abs = std::fabs(s);
    const double kappa = 2.0 * s_abs * e_fs / v_prime_fs2;  // x = kappa a (1/FS)
    const double lambda = s * s / v_prime_fs2;              // b^2 = lambda a^2 (1/FS^2)
    double* const f = f_.data();
    const double* const e1 = t.e1_.data();
    const double* const e2 = t.e2_.data();
    const int lo = lo_;
    const int hi = hi_;

    // The w table's base node and fraction: ln q_i = ln q_0 + 2h i, node w_node + 2 i (header, 3).
    const double ln_q0 = 2.0 * u_lo_ + std::log(s * s / sigma2_fs2);
    const double w_position = (ln_q0 - t.w_lo_) / t.w_step_;
    const auto w_top = static_cast<long long>(t.w_.size()) - 1 - 2LL * points;
    long long w_node;
    double w_t;
    if (!(w_position >= 0.0)) {
        w_node = 0, w_t = 0.0;  // every point below the curve's clamp at q = 1e-4
    } else if (w_position > static_cast<double>(w_top)) {
        w_node = w_top, w_t = 0.0;  // every point above the clamp at q = 1e4
    } else {
        w_node = static_cast<long long>(w_position);
        w_t = w_position - static_cast<double>(w_node);
    }
    const double* const w = t.w_.data() + w_node;

    const double a2_lo = a_lo_ * a_lo_;
    const double x_top = kappa * a_lo_ * e1[hi];
    if (x_top < kNewbornSmallX) {
        // ln I0(x) = x^2 / 4 at every live point: the term is (kappa^2 / 4 - lambda) a^2.
        const double c = (0.25 * kappa * kappa - lambda) * a2_lo;
        for (int i = lo; i <= hi; ++i) f[i] += (w[2 * i] + w_t * (w[2 * i + 1] - w[2 * i])) * c * e2[i];
    } else {
        // ln x_i = ln x_0 + i h; the G table's node for point i is g_node + 4 i, with one fraction for all points.
        const double ln_x0 = std::log(kappa) + u_lo_;
        const double g_position = (ln_x0 - t.g_lo_) / t.g_step_;
        const auto g_top = static_cast<long long>(t.g_.size()) - 3 - 4LL * (points - 1);
        long long g_node = g_top;  // every point where G is constant (header, 3)
        double g_t = 0.0;
        if (g_position < static_cast<double>(g_top)) {
            g_node = static_cast<long long>(std::floor(g_position));
            g_t = g_position - static_cast<double>(g_node);
        }
        const Lagrange c = lagrange(g_t);
        const double* const g = t.g_.data();
        const double ka = kappa * a_lo_;
        const double la = lambda * a2_lo;
        const double half_h = 0.5 * h_;
        const double half_ln_x0 = 0.5 * ln_x0;
        for (int i = lo; i <= hi; ++i) {
            const auto j = static_cast<std::size_t>(g_node + 4LL * i);  // >= 2 at every live point here
            const double gi = c.c0 * g[j - 1] + c.c1 * g[j] + c.c2 * g[j + 1] + c.c3 * g[j + 2];
            const double term = -la * e2[i] + ka * e1[i] - half_ln_x0 - half_h * i + gi;
            f[i] += (w[2 * i] + w_t * (w[2 * i + 1] - w[2 * i])) * term;
        }
    }

    // The largest point, the trapezoid's log-sum-exp and the moments about the largest point.
    double m = f[lo];
    int at = lo;
    for (int i = lo + 1; i <= hi; ++i) {
        if (f[i] > m) m = f[i], at = i;
    }
    // Every point weighted 1 here, d in steps of h from the largest; the grid's two end points, weighted 1/2 by the
    // trapezoid, are corrected after the loop.
    double s0 = 0.0;
    double s1 = 0.0;
    double s2 = 0.0;
    for (int i = lo; i <= hi; ++i) {
        const double p = std::exp(f[i] - m);
        const double d = static_cast<double>(i - at);
        s0 += p;
        s1 += p * d;
        s2 += p * d * d;
    }
    for (const int end : {0, points - 1}) {
        if (end < lo || end > hi) continue;  // a dropped end carries no weight
        const double p = 0.5 * std::exp(f[end] - m);
        const double d = static_cast<double>(end - at);
        s0 -= p;
        s1 -= p * d;
        s2 -= p * d * d;
    }
    // The revival counter: a point back within 10 nats of the largest after falling deeper (header, 5); branch-free.
    float* const depth = depth_.data();
    const double near = m - kNewbornRevivalNats;
    float deepest = 0.0f;
    for (int i = lo; i <= hi; ++i) {
        const float before = depth[i];
        const float back = f[i] > near ? -before : 0.0f;
        deepest = deepest > back ? deepest : back;
        const auto rel = static_cast<float>(f[i] - m);
        depth[i] = rel < before ? rel : before;
    }
    deepest_revival_ = std::max(deepest_revival_, static_cast<double>(deepest));
    const double ln_z = m + std::log(s0) - t.ln_intervals_;
    last_score_ = ln_z - ln_z_;
    ln_z_ = ln_z;
    const double mean_d = s1 / s0;  // in steps of h
    mean_u_ = u_lo_ + (at + mean_d) * h_;
    sigma_u_ = h_ * std::sqrt(std::max(0.0, s2 / s0 - mean_d * mean_d));
    narrowest_sigma_u_ = std::min(narrowest_sigma_u_, sigma_u_);

    // The live range: its ends more than live_range_ below the largest are dropped for good.
    const double cutoff = m - live_range_;
    while (lo_ < hi_ && f[lo_] < cutoff) ++lo_;
    while (hi_ > lo_ && f[hi_] < cutoff) --hi_;
}

Amplitude NewbornIntegral::to_gaussian(std::int64_t k) const {
    const double s2 = sigma_u_ * sigma_u_;
    const double mu = std::exp(mean_u_ + 0.5 * s2);
    return {mu, mu * mu * std::expm1(s2), k};
}

bool NewbornIntegral::should_switch(double switch_sigma_u, bool last_scored_sample_of_first_mark) const {
    return last_scored_sample_of_first_mark || sigma_u_ < switch_sigma_u;
}

}  // namespace kz4ap::viterbi
