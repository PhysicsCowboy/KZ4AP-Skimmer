#include "kz4ap/bank/noise.hpp"

#include "fft.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace kz4ap::bank {

namespace {

// Python's int(round(x)) for the sample counts: ties to even (the default rounding mode). Used only for
// lengths derived from the configuration (a warm-up, a segment, a reach: well within int).
int round_int(double x) { return static_cast<int>(std::nearbyint(x)); }

// The column of P holding absolute sample i, where column 0 holds absolute sample base (64-bit indices). The
// difference is a position inside the window, so it fits in int; a sample outside the window is an error.
int window_column(std::int64_t i, std::int64_t base, const Matrix& P) {
    const std::int64_t c = i - base;
    if (c < 0 || c >= P.cols) throw std::out_of_range("noise: sample index outside the window of P");
    return static_cast<int>(c);
}

}  // namespace

double guard_mean(double kappa) { return 1.0 - kappa * std::exp(-kappa) / (1.0 - std::exp(-kappa)); }

double quantile_linear(std::vector<double> x, double q) {
    const std::size_t n = x.size();
    if (n == 0) throw std::invalid_argument("quantile of an empty sample");
    for (double v : x)
        if (std::isnan(v)) return std::numeric_limits<double>::quiet_NaN();
    std::sort(x.begin(), x.end());
    // numpy "linear": virtual index (n - 1) q; at or past the last index, the last value.
    const double vi = static_cast<double>(n - 1) * q;
    if (vi >= static_cast<double>(n - 1)) return x[n - 1];
    const double lo = std::floor(vi);
    const auto i = static_cast<std::size_t>(lo);
    const double gamma = vi - lo;
    const double a = x[i], b = x[i + 1];
    // numpy _lerp: a + (b - a) t, or b - (b - a)(1 - t) where t >= 0.5.
    const double diff = b - a;
    return gamma >= 0.5 ? b - diff * (1.0 - gamma) : a + diff * gamma;
}

// --- ThreeTapNoise ------------------------------------------------------------------------------------

ThreeTapNoise::ThreeTapNoise(const BankConfig& cfg, double rate_hz, std::vector<int> branch_n,
                             std::int64_t first_sample)
    : n_(std::move(branch_n)),
      kappa_(cfg.noise_guard),
      kappa_n_(cfg.neighbor_guard),
      m_(guard_mean(cfg.noise_guard)),
      alpha_(1.0 - std::exp(-1.0 / (cfg.noise_tau_s * rate_hz))),
      warmup_(std::max(1, round_int(cfg.noise_warmup_s * rate_hz))),
      origin_(first_sample),
      var_(n_.size(), std::numeric_limits<double>::quiet_NaN()),
      weight_(n_.size(), 0.0) {}

void ThreeTapNoise::update(const Matrix& P, std::int64_t n0, std::int64_t n1, std::int64_t base) {
    if (n1 <= n0) return;
    const std::size_t K = n_.size();
    if (!started_) {
        // the warm-up reads every sample from the stream's start, origin_ ... n1 - 1
        const int from = window_column(origin_, base, P);
        const int to = window_column(n1 - 1, base, P) + 1;
        const double scale = 2.0 * -std::log(0.8);
        for (std::size_t k = 0; k < K; ++k) {
            std::vector<double> row(static_cast<std::size_t>(to - from));
            for (int i = from; i < to; ++i) row[static_cast<std::size_t>(i - from)] = P.at(static_cast<int>(k), i);
            const double q = quantile_linear(std::move(row), 0.2) / scale;
            var_[k] = std::max(q, kMinVar);  // np.maximum: NaN propagates (std::max returns q, as NaN < x is false)
        }
        if (n1 - origin_ >= warmup_) {
            started_ = true;
            std::fill(weight_.begin(), weight_.end(), 0.1 * warmup_);  // milestone 2: the warm-up's weight
        }
        return;
    }
    std::vector<int> count(K, 0);
    std::vector<double> sum_mid(K, 0.0);
    bool any = false;
    for (std::size_t k = 0; k < K; ++k) {
        const std::int64_t lag = n_[k];
        const int r = static_cast<int>(k);
        const double two_var = 2.0 * var_[k];
        for (std::int64_t i = std::max(n0, origin_ + 2 * lag); i < n1; ++i) {  // valid: i - origin >= 2 N_k
            const double now = P.at(r, window_column(i, base, P));
            const double mid = P.at(r, window_column(i - lag, base, P));
            const double old = P.at(r, window_column(i - 2 * lag, base, P));
            if (mid < kappa_ * two_var && now < kappa_n_ * two_var && old < kappa_n_ * two_var) {
                ++count[k];
                sum_mid[k] += mid;
            }
        }
        any = any || count[k] > 0;
    }
    if (!any) return;
    for (std::size_t k = 0; k < K; ++k) {
        const double c = static_cast<double>(count[k]);
        const double mean_mid = sum_mid[k] / std::max(c, 1.0);
        weight_[k] += c;
        if (count[k] == 0) continue;
        const double step = std::max(1.0 - std::pow(1.0 - alpha_, c), c / std::max(weight_[k], 1.0));
        const double target = mean_mid / (2.0 * m_);
        var_[k] = std::max(var_[k] + step * (target - var_[k]), kMinVar);
    }
}

// --- BranchNoise --------------------------------------------------------------------------------------

BranchNoise::BranchNoise(const BankConfig& cfg, double rate_hz, std::vector<int> branch_n,
                         std::int64_t first_sample)
    : est_(cfg, rate_hz, std::move(branch_n), first_sample) {}

void BranchNoise::update(std::span<const std::complex<double>>, const Matrix& P, std::int64_t n0, std::int64_t n1,
                         std::int64_t base) {
    est_.update(P, n0, n1, base);
}

// --- SpectrumNoise ------------------------------------------------------------------------------------

SpectrumNoise::SpectrumNoise(const BankConfig& cfg, double rate_hz, std::vector<int> branch_n,
                             const std::string& level, std::int64_t first_sample)
    : n_(std::move(branch_n)),
      mask_bias_(cfg.mask_bias),
      ref_(cfg, rate_hz, std::vector<int>(n_.begin(), n_.begin() + (n_.empty() ? 0 : 1)), first_sample),
      origin_(first_sample),
      next_start_(first_sample) {
    if (level != "three-tap" && level != "spectrum")
        throw std::invalid_argument("unknown noise level source '" + level + "'");
    spectrum_level_ = level == "spectrum";
    if (mask_bias_.size() != n_.size())
        throw std::invalid_argument("mask_bias has " + std::to_string(mask_bias_.size()) + " values for " +
                                    std::to_string(n_.size()) + " branches");
    m_ = std::max(16, round_int(cfg.segment_s * rate_hz));
    const auto M = static_cast<std::size_t>(m_);
    window_.resize(M);
    for (std::size_t i = 0; i < M; ++i)
        window_[i] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / m_);
    // numpy.fft.fftfreq(M, d = 1 / r): bin i at i, then -(M/2) ... -1, times 1 / (M d), Hz.
    const double d = 1.0 / rate_hz;
    const double val = 1.0 / (m_ * d);
    const int positive = (m_ - 1) / 2 + 1;
    std::vector<double> centers(M);
    for (int i = 0; i < m_; ++i) {
        const int j = i < positive ? i : i - m_;
        centers[static_cast<std::size_t>(i)] = j * val;
    }
    std::vector<double> offsets(kSubsamples);
    for (int j = 0; j < kSubsamples; ++j)
        offsets[static_cast<std::size_t>(j)] = ((j + 0.5) / kSubsamples - 0.5) * rate_hz / m_;
    bin_weights_.assign(n_.size(), std::vector<double>(M));
    for (std::size_t k = 0; k < n_.size(); ++k)
        for (std::size_t i = 0; i < M; ++i) {
            double s = 0.0;
            for (double o : offsets) s += power_response(centers[i] + o, n_[k], rate_hz);
            bin_weights_[k][i] = s / kSubsamples;
        }
    half_ = std::max(0, round_int(cfg.spectrum_smoothing_hz * m_ / rate_hz));
    kappa_n_ = cfg.neighbor_guard;
    reach_ = round_int(cfg.guard_margin_s * rate_hz);
    min_clean_ = cfg.min_clean_fraction;
    beta_ = 1.0 - std::exp(-cfg.segment_s / cfg.noise_tau_s);
}

void SpectrumNoise::update(std::span<const std::complex<double>> u, const Matrix& P, std::int64_t n0,
                           std::int64_t n1, std::int64_t base) {
    ref_.update(P, n0, n1, base);  // branch 1 only: ref_ has one branch and reads row 0
    const int look = n_[0] - 1 + reach_;  // the flag needs |v_1|^2 this far past a segment
    while (next_start_ + m_ + look <= n1) {
        const std::int64_t s = next_start_;
        next_start_ += m_;
        if (!ref_.started()) continue;
        const auto c = clean(P, s, base);
        int kept = 0;
        for (char x : c) kept += x;
        const double fraction = static_cast<double>(kept) / m_;
        ++segments_offered_;
        kept_fraction_sum_ += fraction;
        if (fraction >= min_clean_)
            accept(u.subspan(static_cast<std::size_t>(s - base), static_cast<std::size_t>(m_)), c);
    }
}

std::vector<char> SpectrumNoise::clean(const Matrix& P, std::int64_t s, std::int64_t base) const {
    // Positions relative to the window's first column (absolute sample base); the window holds every sample
    // the flag needs (s + M + N_1 - 1 + reach <= n1, the update's end).
    // Absolute (64-bit) first: the flag window starts no earlier than the stream's first sample.
    const int n1 = n_[0];
    const std::int64_t lo_abs = std::max(origin_, s - reach_);
    const std::int64_t hi_abs = std::min(base + P.cols, s + m_ + n1 - 1 + reach_);
    // Then window columns: positions inside P, small (checked).
    const int rel = window_column(s, base, P);
    const int lo = window_column(lo_abs, base, P);
    const int hi = hi_abs > lo_abs ? window_column(hi_abs - 1, base, P) + 1 : lo;
    const int len = std::max(0, hi - lo);
    const double limit = kappa_n_ * 2.0 * ref_.var()[0];
    std::vector<int> c(static_cast<std::size_t>(len) + 1, 0);  // cumulative count of flagged samples
    for (int j = 0; j < len; ++j)
        c[static_cast<std::size_t>(j) + 1] = c[static_cast<std::size_t>(j)] + (P.at(0, lo + j) >= limit ? 1 : 0);
    std::vector<char> out(static_cast<std::size_t>(m_));
    for (int i = rel; i < rel + m_; ++i) {
        // u[i] feeds v_1[i .. i + N_1 - 1]; none of them (+/- the reach) may be flagged
        const int a = std::clamp(i - reach_ - lo, 0, len);
        const int b = std::clamp(i + n1 - 1 + reach_ - lo + 1, 0, len);
        out[static_cast<std::size_t>(i - rel)] = (c[static_cast<std::size_t>(b)] - c[static_cast<std::size_t>(a)]) == 0;
    }
    return out;
}

void SpectrumNoise::accept(std::span<const std::complex<double>> seg, const std::vector<char>& clean) {
    const auto M = static_cast<std::size_t>(m_);
    std::vector<double> w(M);
    double norm = 0.0;
    for (std::size_t i = 0; i < M; ++i) {
        w[i] = window_[i] * (clean[i] ? 1.0 : 0.0);
        norm += w[i] * w[i];
    }
    if (norm <= 0.0) return;
    std::vector<std::complex<double>> x(M), X(M);
    for (std::size_t i = 0; i < M; ++i) x[i] = seg[i] * w[i];
    detail::fft_forward(x.data(), X.data(), M);
    std::vector<double> periodogram(M);  // mean over bins = power per sample, FS^2
    double total = 0.0;
    for (std::size_t i = 0; i < M; ++i) {
        const double a = std::abs(X[i]);
        periodogram[i] = a * a / norm;
        total += periodogram[i];
    }
    ++segments_;
    masked_power_sum_ += total / m_;
    if (!periodogram_sum_) {
        periodogram_sum_ = periodogram;
    } else {
        for (std::size_t i = 0; i < M; ++i) (*periodogram_sum_)[i] += periodogram[i];
    }
    if (!shape_) {
        shape_ = periodogram;
    } else {
        const double g = std::max(beta_, 1.0 / segments_);
        for (std::size_t i = 0; i < M; ++i) (*shape_)[i] = (*shape_)[i] + g * (periodogram[i] - (*shape_)[i]);
    }
    ratio_.reset();
}

std::vector<double> SpectrumNoise::smoothed(const std::vector<double>& s) const {
    if (half_ == 0) return s;
    // circular extension by half_ bins each side, then the (2 half_ + 1)-bin moving mean ("valid")
    const int M = m_, h = half_;
    std::vector<double> ext;
    ext.reserve(static_cast<std::size_t>(M + 2 * h));
    ext.insert(ext.end(), s.end() - h, s.end());
    ext.insert(ext.end(), s.begin(), s.end());
    ext.insert(ext.end(), s.begin(), s.begin() + h);
    const double tap = 1.0 / (2 * h + 1);
    std::vector<double> out(static_cast<std::size_t>(M));
    for (int i = 0; i < M; ++i) {
        double acc = 0.0;
        for (int j = 0; j <= 2 * h; ++j) acc += ext[static_cast<std::size_t>(i + j)] * tap;
        out[static_cast<std::size_t>(i)] = acc;
    }
    return out;
}

std::vector<double> SpectrumNoise::masked_branch_power() const {
    if (!periodogram_sum_) throw std::logic_error("no segment has entered the spectrum");
    std::vector<double> mean(*periodogram_sum_);
    for (double& x : mean) x /= segments_;
    const auto sm = smoothed(mean);
    std::vector<double> out(n_.size());
    for (std::size_t k = 0; k < n_.size(); ++k) {
        double dot = 0.0;
        for (std::size_t i = 0; i < sm.size(); ++i) dot += bin_weights_[k][i] * sm[i];
        out[k] = 0.5 * dot / m_;
    }
    return out;
}

std::vector<double> SpectrumNoise::sigma2() const {
    const double level = ref_.var()[0];
    const std::size_t K = n_.size();
    std::vector<double> out(K);
    if (!shape_) {
        for (std::size_t k = 0; k < K; ++k) out[k] = level * n_[0] / n_[k];
        return out;
    }
    if (!ratio_) {
        // through the mask, branch k reads mask_bias[k] of its true noise power (measured per branch, white
        // noise), so both the ratio and the absolute level are divided by it
        const auto sm = smoothed(*shape_);
        std::vector<double> w(K);
        for (std::size_t k = 0; k < K; ++k) {
            double dot = 0.0;
            for (std::size_t i = 0; i < sm.size(); ++i) dot += bin_weights_[k][i] * sm[i];
            w[k] = dot / mask_bias_[k];
        }
        std::vector<double> ratio(K);
        absolute_.assign(K, 0.0);
        for (std::size_t k = 0; k < K; ++k) {
            ratio[k] = w[k] / w[0];
            // variant (b): complex power of v_k is (1/M) sum_m I_m W_k[m]; per real component half of it
            absolute_[k] = 0.5 * w[k] / m_;
        }
        ratio_ = std::move(ratio);
    }
    if (spectrum_level_) return absolute_;
    for (std::size_t k = 0; k < K; ++k) out[k] = level * (*ratio_)[k];
    return out;
}

// --- make_noise ---------------------------------------------------------------------------------------

std::unique_ptr<NoiseEstimator> make_noise(const BankConfig& cfg, double rate_hz, std::vector<int> branch_n,
                                           std::int64_t first_sample) {
    if (cfg.noise_method == "spectrum")
        return std::make_unique<SpectrumNoise>(cfg, rate_hz, std::move(branch_n), "three-tap", first_sample);
    if (cfg.noise_method == "spectrum-level")
        return std::make_unique<SpectrumNoise>(cfg, rate_hz, std::move(branch_n), "spectrum", first_sample);
    if (cfg.noise_method == "branch")
        return std::make_unique<BranchNoise>(cfg, rate_hz, std::move(branch_n), first_sample);
    throw std::invalid_argument("unknown noise method '" + cfg.noise_method + "'");
}

}  // namespace kz4ap::bank
