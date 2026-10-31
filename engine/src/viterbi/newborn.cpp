// The newborn integral (newborn.hpp; design notes, part 8 as amended 2026-10-26, 2026-10-29 and 2026-10-30, D22 to
// D24; plan 1b, Tasks 9 and 10).
#include "kz4ap/viterbi/newborn.hpp"

#include "kz4ap/viterbi/observation.hpp"

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

// softplus(x) = ln(1 + e^x), without overflow.
double softplus(double x) { return x > 0.0 ? x + std::log1p(std::exp(-x)) : std::log1p(std::exp(x)); }

// The 4-point Lagrange weights for the nodes at -1, 0, 1, 2 at the fraction t in [0, 1).
struct Lagrange {
    double c0, c1, c2, c3;
};
Lagrange lagrange(double t) {
    return {-t * (t - 1.0) * (t - 2.0) / 6.0, (t + 1.0) * (t - 1.0) * (t - 2.0) / 2.0,
            -(t + 1.0) * t * (t - 2.0) / 2.0, (t + 1.0) * t * (t - 1.0) / 6.0};
}

// The prediction step's scratch buffer, p_i = e^(F_i - m) with one pad at each end (header, 4): per thread, so that the
// per-newborn state and its copies stay as they were.
std::vector<double>& scratch(int points) {
    thread_local std::vector<double> buffer;
    const auto size = 2 * (static_cast<std::size_t>(points) + 2);  // p_i with its pads, then ln r_i (header, 3)
    if (buffer.size() < size) buffer.resize(size);
    return buffer;
}

}  // namespace

// --- NewbornTables ---

NewbornTables::NewbornTables(const OutputNoise& noise, double sigma2_per_n0_hz, int points, double span_nepers)
    : points_(points), span_(span_nepers), w_(noise.w_strong), sigma2_per_n0_hz_(sigma2_per_n0_hz), noise_(noise) {
    if (!positive_finite(sigma2_per_n0_hz) || !positive_finite(noise.sigma2_per_n0_hz))
        throw std::invalid_argument("NewbornTables: sigma_y^2 / N0 must be positive and finite (Hz)");
    if (!positive_finite(noise.w_strong))
        throw std::invalid_argument("NewbornTables: the output noise's w_strong must be positive and finite");
    if (points < 2) throw std::invalid_argument("NewbornTables: the grid needs at least 2 points");
    if (!positive_finite(span_nepers))
        throw std::invalid_argument("NewbornTables: the prior's span must be positive and finite (nepers)");
    step_ = span_nepers / (points - 1);
    ln_intervals_ = std::log(static_cast<double>(points - 1));

    // G at steps of h/8 over [-20, +165] nepers of ln x at the default span; below, down to the lowest ln x where the
    // table is read (x at least 1e-3) less the stencil's 2 steps and the span; above, to 40 nepers plus the span, where
    // G is constant to double precision (e^-40 = 4e-18), so that a clamped read is exact.
    g_step_ = step_ / 8.0;
    g_lo_ = std::min(kNewbornGLowestLnX, std::log(kNewbornSmallX) - span_nepers - 2.0 * g_step_);
    const double g_hi = std::max(kNewbornGHighestLnX, 40.0 + span_nepers + 4.0 * g_step_);
    const auto g_nodes = static_cast<std::size_t>(std::ceil((g_hi - g_lo_) / g_step_)) + 1;
    g_.resize(g_nodes);
    for (std::size_t j = 0; j < g_nodes; ++j) g_[j] = g_of(g_lo_ + static_cast<double>(j) * g_step_);

    // softplus at steps of h over [-40 - 2 span, 40 + 2 span], two nodes beyond each end for the stencil: a newborn reads
    // it only where ln gamma lies in [-40 - 2 span, 40] (header, 3), so ln gamma + 2 i h stays inside.
    sp_lo_ = -kNewbornSoftplusReach - 2.0 * span_nepers - 2.0 * step_;
    const auto sp_nodes =
        static_cast<std::size_t>(std::ceil((2.0 * kNewbornSoftplusReach + 4.0 * span_nepers + 4.0 * step_) / step_)) + 1;
    sp_.resize(sp_nodes);
    for (std::size_t j = 0; j < sp_nodes; ++j) sp_[j] = softplus(sp_lo_ + static_cast<double>(j) * step_);

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
    const double top = static_cast<double>(g_.size()) - 2.0;
    const double position = std::clamp((y - g_lo_) / g_step_, 0.0, top);
    const auto node = static_cast<std::size_t>(position);
    const double t = position - static_cast<double>(node);
    return x - 0.5 * y + g_[node] + t * (g_[node + 1] - g_[node]);
}

double NewbornTables::softplus_from_table(double x) const {
    if (!std::isfinite(x)) throw std::invalid_argument("NewbornTables::softplus_from_table: x must be finite");
    const double position = (x - sp_lo_) / step_;
    if (position < 1.0) return std::exp(x);
    if (position > static_cast<double>(sp_.size()) - 3.0) return x;
    const auto node = static_cast<std::size_t>(position);
    const Lagrange c = lagrange(position - static_cast<double>(node));
    return c.c0 * sp_[node - 1] + c.c1 * sp_[node] + c.c2 * sp_[node + 1] + c.c3 * sp_[node + 2];
}

// --- NewbornIntegral ---

NewbornIntegral::NewbornIntegral(const NewbornTables& tables, double u_lo, double u_hi, int points,
                                 double live_range_nats, NewbornFade fade)
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
    if (fade.interval_samples < 0)
        throw std::invalid_argument("NewbornIntegral: the prediction step's interval must not be negative (samples)");
    if (!(fade.var_per_sample_nepers2 >= 0.0) || !std::isfinite(fade.var_per_sample_nepers2))
        throw std::invalid_argument("NewbornIntegral: the prediction step's variance must be finite and not negative "
                                    "(nepers^2 per sample)");
    h_ = (u_hi - u_lo) / (points - 1);
    a_lo_ = std::exp(u_lo);
    if (fade.interval_samples > 0) {
        // header, 4: eps = interval x var / (2 h^2), in ceil(eps / (1/4)) substeps (at least one, so that a variance of 0
        // still runs the round trip F_i = m + ln e^(F_i - m)).
        interval_ = fade.interval_samples;
        const double eps = interval_ * fade.var_per_sample_nepers2 / (2.0 * h_ * h_);
        substeps_ = std::max(1, static_cast<int>(std::ceil(eps / kNewbornLargestSubstep)));
        eps_ = eps / substeps_;
    }
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

void NewbornIntegral::add(double e_fs, double s, double v, double sigma2_fs2, double floor) {
    if (!(e_fs >= 0.0) || !std::isfinite(e_fs))
        throw std::invalid_argument("NewbornIntegral::add: e must be finite and not negative (FS)");
    if (!std::isfinite(s)) throw std::invalid_argument("NewbornIntegral::add: s must be finite");
    if (!(v >= 0.0) || !std::isfinite(v) || !(floor >= 0.0) || !std::isfinite(floor))
        throw std::invalid_argument("NewbornIntegral::add: v and the floor must be finite and not negative (1)");
    if (!positive_finite(sigma2_fs2))
        throw std::invalid_argument("NewbornIntegral::add: sigma_y^2 must be positive and finite (FS^2)");
    last_stepped_ = false;
    if (s == 0.0) {  // no evidence: V = sigma_y^2 and l = 0 at every amplitude
        last_score_ = 0.0;
        return;
    }
    ++scored_;
    const NewbornTables& t = *tables_;
    const int points = t.points_;
    const double w = t.w_;
    const double s_abs = std::fabs(s);
    const double inv_sigma2 = 1.0 / sigma2_fs2;
    const double a2_lo = a_lo_ * a_lo_;
    const double big_e = e_fs * e_fs * inv_sigma2;           // e^2 / sigma_y^2
    const double big_s = s * s * a2_lo * inv_sigma2;         // s^2 a_lo^2 / sigma_y^2: times e^(2ih) / r_i
    const double big_k = 2.0 * e_fs * s_abs * a_lo_ * inv_sigma2;  // x_i = big_k e^(ih) / r_i
    const double gamma = (floor * floor * s * s + v) * a2_lo * inv_sigma2;
    double* const f = f_.data();
    const double* const e1 = t.e1_.data();
    const double* const e2 = t.e2_.data();
    const int lo = lo_;
    const int hi = hi_;

    // ln r_i (header, 3): 0 where gamma e^(2 i h) is below e^-40 at every point, the argument itself where it is above
    // e^+40 at every point, else the softplus table at node sp_node + 2 i with one fraction.
    const double ln_gamma = gamma > 0.0 ? std::log(gamma) : -std::numeric_limits<double>::infinity();
    const double top_arg = ln_gamma + 2.0 * (points - 1) * h_;
    enum class Lr { Zero, Table, Linear } lr_kind = Lr::Table;
    if (top_arg < -kNewbornSoftplusReach) lr_kind = Lr::Zero;
    else if (ln_gamma > kNewbornSoftplusReach) lr_kind = Lr::Linear;
    // ln r_i into the scratch buffer's second half, one loop per kind.
    double* const p = scratch(points).data() + 1;      // the prediction step's p_i (below)
    double* const lr = p + (points + 2);               // ln r_i
    if (lr_kind == Lr::Table) {
        const double position = (ln_gamma - t.sp_lo_) / h_;  // >= 2: ln gamma >= -40 - 2 span
        const auto sp_node = static_cast<std::size_t>(position);
        const Lagrange c = lagrange(position - static_cast<double>(sp_node));
        const double* const sp = t.sp_.data() + sp_node;
        for (int i = lo; i <= hi; ++i) {
            const std::size_t j = 2 * static_cast<std::size_t>(i);
            lr[i] = c.c0 * sp[j - 1] + c.c1 * sp[j] + c.c2 * sp[j + 1] + c.c3 * sp[j + 2];
        }
    } else if (lr_kind == Lr::Linear) {
        for (int i = lo; i <= hi; ++i) lr[i] = ln_gamma + 2.0 * i * h_;
    } else {
        for (int i = lo; i <= hi; ++i) lr[i] = 0.0;
    }

    if (big_k * e1[hi] < kNewbornSmallX) {
        // x_i <= big_k e^(ih) < 1e-3 at every live point (r_i >= 1): ln I0(x) = x^2 / 4.
        for (int i = lo; i <= hi; ++i) {
            const double inv_r = 1.0 / (1.0 + gamma * e2[i]);
            const double x = big_k * e1[i] * inv_r;
            f[i] += w * (-lr[i] - (big_e + big_s * e2[i]) * inv_r + 0.25 * x * x);
        }
    } else {
        const double ln_k = std::log(big_k);
        const double* const g = t.g_.data();
        const double inv_g_step = 1.0 / t.g_step_;
        const double g_top = static_cast<double>(t.g_.size()) - 2.0;
        const double g_shift = (ln_k - t.g_lo_) * inv_g_step;  // point i's position: g_shift + (i h - ln r_i) / step
        for (int i = lo; i <= hi; ++i) {
            const double inv_r = 1.0 / (1.0 + gamma * e2[i]);
            const double x = big_k * e1[i] * inv_r;
            const double ln_x = ln_k + i * h_ - lr[i];
            // G at this point's own position by linear interpolation, clamped into the table (a point below it has
            // x < 1e-3 and takes x^2 / 4).
            const double position = std::clamp(g_shift + (i * h_ - lr[i]) * inv_g_step, 0.0, g_top);
            const auto node = static_cast<std::size_t>(position);
            const double gi = g[node] + (position - static_cast<double>(node)) * (g[node + 1] - g[node]);
            const double ln_i0_x = x < kNewbornSmallX ? 0.25 * x * x : x - 0.5 * ln_x + gi;
            f[i] += w * (-lr[i] - (big_e + big_s * e2[i]) * inv_r + ln_i0_x);
        }
    }

    // The largest point, the trapezoid's log-sum-exp and the moments about the largest point.
    double m = f[lo];
    int at = lo;
    for (int i = lo + 1; i <= hi; ++i) {
        if (f[i] > m) m = f[i], at = i;
    }
    // Every point weighted 1 here, d in steps of h from the largest; the grid's two end points, weighted 1/2 by the
    // trapezoid, are corrected after the loop. p_i = e^(F_i - m) goes to the scratch buffer for the prediction step.
    double s0 = 0.0;
    double s1 = 0.0;
    double s2 = 0.0;
    for (int i = lo; i <= hi; ++i) {
        const double pi = std::exp(f[i] - m);
        p[i] = pi;
        const double d = static_cast<double>(i - at);
        s0 += pi;
        s1 += pi * d;
        s2 += pi * d * d;
    }
    for (const int end : {0, points - 1}) {
        if (end < lo || end > hi) continue;  // a dropped end carries no weight
        const double pe = 0.5 * p[end];
        const double d = static_cast<double>(end - at);
        s0 -= pe;
        s1 -= pe * d;
        s2 -= pe * d * d;
    }
    // The revival counter: a point back within 10 nats of the largest after falling deeper (header, 6); branch-free.
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
    common_ += w * big_e;  // w e^2 / sigma_y^2, shared by every point, kept out of F_i (header, 3)
    const double ln_z = m + std::log(s0) - t.ln_intervals_ + common_;
    last_score_ = ln_z - ln_z_;
    ln_z_ = ln_z;
    const double mean_d = s1 / s0;  // in steps of h
    mean_u_ = u_lo_ + (at + mean_d) * h_;
    sigma_u_ = h_ * std::sqrt(std::max(0.0, s2 / s0 - mean_d * mean_d));
    narrowest_sigma_u_ = std::min(narrowest_sigma_u_, sigma_u_);

    // The prediction step (header, 4), on every interval-th scored sample, after the moments: F_i = m + ln p_i'.
    if (interval_ > 0 && scored_ % interval_ == 0) {
        predict(p);
        for (int i = lo; i <= hi; ++i) f[i] = m + std::log(p[i]);
        ++steps_;
        last_stepped_ = true;
    }

    // The live range: its ends more than live_range_ below the largest are dropped for good.
    const double cutoff = m - live_range_;
    while (lo_ < hi_ && f[lo_] < cutoff) ++lo_;
    while (hi_ > lo_ && f[hi_] < cutoff) --hi_;
}

void NewbornIntegral::predict(double* p) {
    const int n = static_cast<int>(f_.size());
    const int lo = lo_;
    const int hi = hi_;
    const double c1 = 1.0 - 2.0 * eps_;
    for (int step = 0; step < substeps_; ++step) {
        // The pads: the reflecting ghost mirrored about the end node at the grid's ends (p_-1 = p_1, p_N = p_(N-2)), 0
        // beyond the live range elsewhere.
        p[lo - 1] = lo == 0 ? (hi >= 1 ? p[1] : 0.0) : 0.0;
        p[hi + 1] = hi == n - 1 ? (lo <= n - 2 ? p[n - 2] : 0.0) : 0.0;
        // In place, carrying the old left neighbor.
        double prev = p[lo - 1];
        for (int i = lo; i <= hi; ++i) {
            const double here = p[i];
            p[i] = c1 * here + eps_ * (prev + p[i + 1]);
            prev = here;
        }
    }
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
