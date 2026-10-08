#include "kz4ap/bank/periodicity.hpp"

#include "kz4ap/bank/numpy_sum.hpp"

#include "fft.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace kz4ap::bank {

namespace {

// Python's int(round(x)) for the sample counts: ties to even (the default rounding mode).
int round_int(double x) { return static_cast<int>(std::nearbyint(x)); }

// The prototype's _normalized_acf: the biased autocorrelation of x by FFT (zero-padded to the smallest
// power of two >= 2n, so the circular correlation equals the linear one), lags 0 ... n - 1, divided by the
// lag-0 sum c0 = x . x.
std::vector<double> normalized_acf(const std::vector<double>& x, double c0) {
    const std::size_t n = x.size();
    std::size_t size = 1;
    while (size < 2 * n) size <<= 1;  // 1 << ceil(log2(2n))
    std::vector<double> padded(size, 0.0);
    std::copy(x.begin(), x.end(), padded.begin());
    std::vector<std::complex<double>> spec(size / 2 + 1);
    kz4ap::detail::rfft_forward(padded.data(), spec.data(), size);
    // spec * conj(spec), as numpy's complex product: re^2 - im (-im) (= re^2 + im^2 exactly) and
    // re (-im) + im re (= 0 exactly).
    for (auto& s : spec) {
        const double re = s.real(), im = s.imag();
        s = {re * re - im * (-im), re * (-im) + im * re};
    }
    std::vector<double> acf(size);
    kz4ap::detail::irfft(spec.data(), acf.data(), size);
    acf.resize(n);
    for (double& a : acf) a /= c0;
    return acf;
}

}  // namespace

std::vector<double> t_grid(const BankConfig& cfg) {
    const double t_min = 1.2 / cfg.max_wpm;
    const double t_max = 1.2 / cfg.min_wpm;
    const int count = static_cast<int>(std::ceil(std::log(t_max / t_min) / std::log(1.01))) + 1;
    std::vector<double> grid;
    grid.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) grid.push_back(t_min * std::pow(1.01, static_cast<double>(i)));
    return grid;
}

std::pair<std::optional<double>, double> comb_estimate(std::span<const double> p, double rate_hz,
                                                       const std::vector<double>& grid, int teeth, double width) {
    const std::size_t n = p.size();
    if (n < 16) return {std::nullopt, 0.0};
    // x = p - mean(p), the mean numpy's (pairwise sum / n).
    const double mean = pairwise_sum(p.data(), n) / static_cast<double>(n);
    std::vector<double> x(n);
    for (std::size_t i = 0; i < n; ++i) x[i] = p[i] - mean;
    // c0 = x . x. numpy computes it with BLAS ddot, whose summation order depends on the BLAS build; the
    // pairwise sum of the squares stands in for it (the same value to rounding, about 1e-16 relative).
    std::vector<double> sq(n);
    for (std::size_t i = 0; i < n; ++i) sq[i] = x[i] * x[i];
    const double c0 = pairwise_sum(sq);
    if (c0 <= 1e-12 * static_cast<double>(n)) return {std::nullopt, 0.0};

    const std::vector<double> acf = normalized_acf(x, c0);
    // cs = [0, cumsum(acf)], summed in order.
    std::vector<double> cs(n + 1, 0.0);
    for (std::size_t i = 0; i < n; ++i) cs[i + 1] = cs[i] + acf[i];

    const double last = static_cast<double>(n - 1);
    const double reach = (static_cast<double>(teeth) + 0.5) + width;  // (teeth + 1/2 + width), in Pi
    std::vector<double> contrast(static_cast<std::size_t>(std::max(teeth, 0)));
    std::vector<double> score(grid.size());
    for (std::size_t g = 0; g < grid.size(); ++g) {
        const double period = 2.0 * grid[g] * rate_hz;  // Pi, samples
        const double half = width * period;
        // The mean of acf over [floor(c - half), ceil(c + half)], clipped to the lags 0 ... n - 1.
        auto band = [&](double center) {
            const double lo_f = std::clamp(std::floor(center - half), 0.0, last);
            const double hi_f = std::clamp(std::ceil(center + half), 0.0, last);
            const auto lo = static_cast<std::size_t>(lo_f);
            const auto hi = static_cast<std::size_t>(hi_f);
            return (cs[hi + 1] - cs[lo]) / static_cast<double>(hi - lo + 1);
        };
        for (int k = 1; k <= teeth; ++k) {
            const double kd = static_cast<double>(k);
            contrast[static_cast<std::size_t>(k - 1)] =
                band(kd * period) - 0.5 * (band((kd - 0.5) * period) + band((kd + 0.5) * period));
        }
        // contrast.mean(axis=1): numpy sums each row of teeth in order (pairwise_sum, under 8 values).
        score[g] = reach * period <= last / 2.0 ? pairwise_sum(contrast) / static_cast<double>(teeth)
                                                : -std::numeric_limits<double>::infinity();
    }
    if (score.empty()) return {std::nullopt, 0.0};
    // np.argmax: the first NaN if there is one, else the first maximum.
    std::size_t best = 0;
    for (std::size_t g = 0; g < score.size(); ++g) {
        if (std::isnan(score[g])) {
            best = g;
            break;
        }
        if (score[g] > score[best]) best = g;
    }
    if (!std::isfinite(score[best])) return {std::nullopt, 0.0};
    return {grid[best], score[best]};
}

Periodicity::Periodicity(const BankConfig& cfg, double rate_hz) : cfg_(cfg) {
    factor_ = std::max(1, round_int(rate_hz / cfg.periodicity_rate_hz));
    rate_ = rate_hz / static_cast<double>(factor_);
    update_every_ = std::max(1, round_int(cfg.periodicity_update_s * rate_hz));
    grid_ = t_grid(cfg);
    if (cfg.periodicity_method != "comb")
        throw std::invalid_argument("periodicity method \"" + cfg.periodicity_method +
                                    "\" is not ported (only \"comb\")");
    if (cfg.periodicity_windows_s.empty()) throw std::invalid_argument("periodicity_windows_s is empty");
    for (const double w : cfg.periodicity_windows_s) windows_.push_back(std::max(16, round_int(w * rate_)));
    std::sort(windows_.begin(), windows_.end());  // shortest first
    threshold_ = cfg.comb_confidence_min;
    per_window_.assign(windows_.size(), {std::nullopt, 0.0});
}

void Periodicity::push(std::span<const double> p) {
    std::vector<double> x = std::move(carry_);
    x.insert(x.end(), p.begin(), p.end());
    const auto f = static_cast<std::size_t>(factor_);
    const std::size_t whole = x.size() / f * f;
    if (whole > 0) {
        // The mean of each run of factor samples (numpy's mean over the reshaped rows).
        for (std::size_t i = 0; i < whole; i += f)
            buffer_.push_back(pairwise_sum(x.data() + i, f) / static_cast<double>(f));
        // Keep the longest window.
        const auto keep = static_cast<std::size_t>(windows_.back());
        if (buffer_.size() > keep)
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_.size() - keep));
    }
    carry_.assign(x.begin() + static_cast<std::ptrdiff_t>(whole), x.end());
    pending_ += static_cast<long long>(p.size());
}

PeriodicityUpdate Periodicity::update(bool force) {
    if (!force && pending_ < update_every_) {
        PeriodicityUpdate r = last_;
        r.updated = false;
        return r;
    }
    pending_ = 0;
    std::optional<PeriodicityUpdate> found;
    double best = 0.0;
    per_window_.clear();
    for (const int w : windows_) {
        // Every candidate over the window's most recent samples.
        const auto wn = static_cast<std::size_t>(w);
        if (buffer_.size() < wn) {
            per_window_.emplace_back(std::nullopt, 0.0);
            continue;
        }
        const std::span<const double> recent(buffer_.data() + (buffer_.size() - wn), wn);
        const auto est = comb_estimate(recent, rate_, grid_, cfg_.comb_teeth, cfg_.comb_width);
        per_window_.push_back(est);
        if (est.second > best) best = est.second;  // Python's max(best, score)
        if (!found && est.first && est.second >= threshold_)
            found = PeriodicityUpdate{est.first, est.second, static_cast<double>(w) / rate_, false};
    }
    last_ = found ? *found : PeriodicityUpdate{std::nullopt, best, std::nullopt, false};
    PeriodicityUpdate r = last_;
    r.updated = true;
    return r;
}

}  // namespace kz4ap::bank
