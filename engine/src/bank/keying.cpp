#include "kz4ap/bank/keying.hpp"

#include "kz4ap/bank/noise.hpp"
#include "kz4ap/bank/numpy_sum.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace kz4ap::bank {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// numpy.maximum / numpy.minimum: NaN if either is NaN.
double np_maximum(double a, double b) { return (std::isnan(a) || std::isnan(b)) ? kNaN : std::max(a, b); }
double np_minimum(double a, double b) { return (std::isnan(a) || std::isnan(b)) ? kNaN : std::min(a, b); }

// np.sum(row) along a contiguous row.
double row_sum(const std::vector<double>& row) { return pairwise_sum(row.data(), row.size()); }

double log_prior_odds(const BankConfig& cfg) { return std::log(cfg.prior_key_down / (1.0 - cfg.prior_key_down)); }

}  // namespace

std::vector<int> hysteresis(std::span<const double> down, std::span<const double> up, int initial) {
    // The prototype's vectorized form (the last event at or before each sample: up -1, down +1, up winning)
    // as a per-sample loop: the same state at every sample.
    std::vector<int> key(down.size());
    int state = initial != 0 ? 1 : 0;
    for (std::size_t i = 0; i < down.size(); ++i) {
        if (up[i] != 0.0)
            state = 0;
        else if (down[i] != 0.0)
            state = 1;
        key[i] = state;
    }
    return key;
}

std::vector<std::vector<std::pair<std::int64_t, bool>>> edges(const Matrix& key, const std::vector<int>& before,
                                                            std::int64_t n0) {
    std::vector<std::vector<std::pair<std::int64_t, bool>>> out(static_cast<std::size_t>(key.rows));
    for (int r = 0; r < key.rows; ++r) {
        bool prev = before[static_cast<std::size_t>(r)] != 0;
        for (int c = 0; c < key.cols; ++c) {
            const bool now = key.at(r, c) != 0.0;
            if (now != prev) out[static_cast<std::size_t>(r)].emplace_back(n0 + c, now);
            prev = now;
        }
    }
    return out;
}

BankKeyer::BankKeyer(const BankConfig& cfg, double rate_hz, const std::vector<double>& lengths_s)
    : alpha(1.0 - std::exp(-1.0 / (cfg.amplitude_tau_s * rate_hz))),
      log_prior(log_prior_odds(cfg)),
      h(cfg.hysteresis_nats),
      x_off(std::sqrt(-2.0 * std::log(cfg.release_probability))),
      // W_min in samples of keyed time (s x samples/s; not rounded: W counts samples and is compared with it).
      rekey_weight(cfg.rekey_after_s * rate_hz) {
    const std::size_t k = lengths_s.size();
    // Squelch: a_min,k = squelch_a (L_k / squelch_ref_s)^squelch_exponent (a-hat^2's spread in noise alone
    // grows as sqrt(L_k), so a_min as L_k^(1/4)).
    a_min.resize(k);
    for (std::size_t i = 0; i < k; ++i)
        a_min[i] = cfg.squelch_a * std::pow(lengths_s[i] / cfg.squelch_ref_s, cfg.squelch_exponent);
    // Unknown amplitude: the calibrated x_on,k (E9a), or nominally sqrt(-2 ln(R_fa L_k)) with R_fa L_k clamped
    // at 0.5 (a numerical guard, heuristic).
    if (!cfg.x_on_values.empty()) {
        if (cfg.x_on_values.size() != k) throw std::invalid_argument("x_on_values needs one threshold per branch");
        x_on = cfg.x_on_values;
    } else {
        x_on.resize(k);
        for (std::size_t i = 0; i < k; ++i)
            x_on[i] = std::sqrt(-2.0 * std::log(np_minimum(0.5, cfg.false_marks_per_s * lengths_s[i])));
    }
    amp2.assign(k, 0.0);
    weight.assign(k, 0.0);
    prev_amp2.assign(k, kNaN);
    unknown.assign(k, true);  // the stream's start is an over's start
    keyed.assign(k, {});
    // seed_memory_rekeys x W_min of keyed time, samples (Python int(round(.)): ties to even); heuristic.
    keyed_cap = std::max(1, static_cast<int>(std::nearbyint(cfg.seed_memory_rekeys * rekey_weight)));
    key.assign(k, 0);
}

KeyStep BankKeyer::step(const Matrix& P, const std::vector<double>& sigma2) {
    const int K = P.rows, n = P.cols;
    KeyStep out;
    out.a.resize(static_cast<std::size_t>(K));
    out.key.rows = out.p.rows = K;
    out.key.cols = out.p.cols = n;
    out.key.v.assign(P.v.size(), 0.0);
    out.p.v.assign(P.v.size(), 0.0);
    out.before = key;
    Matrix p_raw = out.p;  // p before the squelch, for the amplitude update
    std::vector<double> down(static_cast<std::size_t>(n)), up(static_cast<std::size_t>(n));
    for (int k = 0; k < K; ++k) {
        const auto kk = static_cast<std::size_t>(k);
        const double a = std::sqrt(amp2[kk] / sigma2[kk]);
        out.a[kk] = a;
        const bool signal = a >= a_min[kk];
        for (int i = 0; i < n; ++i) {
            const auto ii = static_cast<std::size_t>(i);
            const double x = std::sqrt(P.at(k, i) / sigma2[kk]);
            const double g = envelope_llr(x, a) + log_prior;
            p_raw.at(k, i) = logistic(g);
            if (unknown[kk]) {
                down[ii] = x > x_on[kk] ? 1.0 : 0.0;
                up[ii] = x < x_off ? 1.0 : 0.0;
            } else {
                down[ii] = (signal && g > h) ? 1.0 : 0.0;
                up[ii] = (!signal || g < -h) ? 1.0 : 0.0;
            }
        }
        const auto row = hysteresis(down, up, out.before[kk]);
        for (int i = 0; i < n; ++i) {
            out.key.at(k, i) = row[static_cast<std::size_t>(i)];
            out.p.at(k, i) = p_raw.at(k, i) * (signal ? 1.0 : 0.0);
        }
        if (n > 0) key[kk] = row[static_cast<std::size_t>(n - 1)];
    }
    update_amplitude(P, p_raw, sigma2, out.key);
    return out;
}

void BankKeyer::update_amplitude(const Matrix& P, const Matrix& p, const std::vector<double>& sigma2,
                                 const Matrix& keymat) {
    // Online EM for the Rician component, one step per block (known branches): mean square 2 sigma^2 + s^2,
    // p-weighted; s^2 moves toward the block's weighted mean by min(1, max(1 - (1 - alpha)^(sum p), sum p / W)).
    const int K = P.rows, n = P.cols;
    std::vector<double> row(static_cast<std::size_t>(n)), prow(static_cast<std::size_t>(n));
    for (int k = 0; k < K; ++k) {
        const auto kk = static_cast<std::size_t>(k);
        for (int i = 0; i < n; ++i) {
            prow[static_cast<std::size_t>(i)] = p.at(k, i);
            row[static_cast<std::size_t>(i)] = p.at(k, i) * P.at(k, i);
        }
        const double wp = unknown[kk] ? 0.0 : row_sum(prow);
        const bool has = wp > 0.0;
        const double mean_p = row_sum(row) / np_maximum(wp, 1e-300);
        weight[kk] += wp;
        const double step = np_minimum(
            1.0, np_maximum(1.0 - std::pow(1.0 - alpha, wp), wp / np_maximum(weight[kk], 1e-300)));
        const double target = mean_p - 2.0 * sigma2[kk];
        if (has) amp2[kk] = np_maximum(0.0, amp2[kk] + step * (target - amp2[kk]));
    }
    // While unknown, only the keyed samples count: W is their number (keyed time), and s^2 is seeded as the 0.9
    // quantile of the most recent keyed_cap of them, minus 2 sigma^2.
    for (int k = 0; k < K; ++k) {
        const auto kk = static_cast<std::size_t>(k);
        if (!unknown[kk]) continue;
        int count = 0;
        auto& kept = keyed[kk];
        for (int i = 0; i < n; ++i)
            if (keymat.at(k, i) != 0.0) {
                kept.push_back(P.at(k, i));
                ++count;
            }
        if (count == 0) continue;
        const auto cap = static_cast<std::size_t>(keyed_cap);
        if (kept.size() > cap) kept.erase(kept.begin(), kept.end() - static_cast<std::ptrdiff_t>(cap));
        weight[kk] += static_cast<double>(count);
        // Python's max(0.0, x): 0 when x is NaN, as std::max(0.0, x) gives.
        amp2[kk] = std::max(0.0, quantile_linear(kept, 0.9) - 2.0 * sigma2[kk]);
    }
}

std::vector<bool> BankKeyer::ready_to_rekey() const {
    std::vector<bool> out(unknown.size());
    for (std::size_t k = 0; k < out.size(); ++k) out[k] = unknown[k] && weight[k] >= rekey_weight;
    return out;
}

void BankKeyer::start_over(int k) {
    const auto kk = static_cast<std::size_t>(k);
    if (!unknown[kk]) prev_amp2[kk] = amp2[kk];
    amp2[kk] = 0.0;
    weight[kk] = 0.0;
    keyed[kk].clear();
    unknown[kk] = true;
}

void BankKeyer::finish_over_start(int k, double amp2_k, bool key_now) {
    const auto kk = static_cast<std::size_t>(k);
    amp2[kk] = amp2_k;
    keyed[kk].clear();
    unknown[kk] = false;
    key[kk] = key_now ? 1 : 0;
}

std::vector<int> rekey(std::span<const double> P, double sigma2, double amp2, const BankConfig& cfg, double a_min) {
    // Python's max(amp2, 0.0) keeps a NaN amp2, as std::max(amp2, 0.0) does.
    const double a = std::sqrt(std::max(amp2, 0.0) / sigma2);
    const double log_prior = log_prior_odds(cfg);
    const bool signal = a >= a_min;
    std::vector<double> down(P.size()), up(P.size());
    for (std::size_t i = 0; i < P.size(); ++i) {
        const double x = std::sqrt(P[i] / sigma2);
        const double g = envelope_llr(x, a) + log_prior;
        down[i] = (signal && g > cfg.hysteresis_nats) ? 1.0 : 0.0;
        up[i] = (!signal || g < -cfg.hysteresis_nats) ? 1.0 : 0.0;
    }
    return hysteresis(down, up, 0);
}

}  // namespace kz4ap::bank
