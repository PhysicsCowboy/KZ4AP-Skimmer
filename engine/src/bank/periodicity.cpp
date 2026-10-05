#include "kz4ap/bank/periodicity.hpp"

#include "kz4ap/bank/filters.hpp"
#include "kz4ap/bank/numpy_sum.hpp"
#include "kz4ap/bank/timing.hpp"

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

Periodicity::Periodicity(const BankConfig& cfg, double rate_hz)
    : Periodicity(cfg, rate_hz, bank_timing(cfg).periodicity_windows_s, bank_timing(cfg).periodicity_window_dits) {}

Periodicity::Periodicity(const BankConfig& cfg, double rate_hz, const std::vector<std::vector<double>>& windows_s,
                         const std::vector<double>& window_dits)
    : cfg_(cfg), dits_(window_dits) {
    factor_ = std::max(1, round_int(rate_hz / cfg.periodicity_rate_hz));
    rate_ = rate_hz / static_cast<double>(factor_);
    update_every_ = std::max(1, round_int(cfg.periodicity_update_s * rate_hz));
    grid_ = t_grid(cfg);
    if (cfg.periodicity_method != "comb")
        throw std::invalid_argument("periodicity method \"" + cfg.periodicity_method +
                                    "\" is not ported (only \"comb\")");
    if (windows_s.empty()) throw std::invalid_argument("the periodicity windows are empty");
    for (const auto& row : windows_s) {
        if (row.size() != 1 && row.size() != grid_.size())
            throw std::invalid_argument("a periodicity window needs one length or one per candidate dit");
        std::vector<int> n;
        for (const double w : row) n.push_back(std::max(16, round_int(w * rate_)));
        any_per_candidate_ = any_per_candidate_ || n.size() != 1;
        for (const int x : n) longest_ = std::max(longest_, static_cast<std::size_t>(x));
        windows_.push_back(std::move(n));
    }
    // Shortest first, by the first entry (a per-candidate row's first entry is its shortest candidate's window).
    std::stable_sort(windows_.begin(), windows_.end(),
                     [](const std::vector<int>& a, const std::vector<int>& b) { return a.front() < b.front(); });
    if (!dits_.empty()) {
        // Windows that follow T-hat (B4a-C): shared rows only, as many as windows_s (the windows without a T-hat).
        if (any_per_candidate_ || dits_.size() != windows_.size())
            throw std::invalid_argument("windows that follow a dit need one shared window in seconds per N_w");
        std::sort(dits_.begin(), dits_.end());
        // The longest fitted dit an eligible fit can have: |ln(L_k / (length_dits T))| <= eligibility_tolerance, with
        // L_k the realized length N_k / rate_hz, so T <= L_max / length_dits x exp(eligibility_tolerance).
        const std::vector<int> n = branch_samples(branch_lengths_s(cfg), rate_hz);
        const int n_max = n.empty() ? 1 : *std::max_element(n.begin(), n.end());
        dit_cap_s_ = static_cast<double>(n_max) / rate_hz / cfg.length_dits * std::exp(cfg.eligibility_tolerance);
        const int n_cap = std::max(16, round_int(dits_.back() * dit_cap_s_ * rate_));
        longest_ = std::max(longest_, static_cast<std::size_t>(n_cap));
    }
    threshold_ = cfg.comb_confidence_min;
    per_window_.assign(windows_.size(), {std::nullopt, 0.0});
    scores_.resize(windows_.size());
    slid_.resize(windows_.size());
    const auto bands = static_cast<std::size_t>(2 * std::max(cfg.comb_teeth, 0) + 1);
    for (std::size_t r = 0; r < windows_.size(); ++r)
        if (windows_[r].size() != 1) slid_[r].assign(grid_.size(), Slid{std::vector<double>(bands, 0.0), false, 0});
    if (any_per_candidate_) {
        // The comb's bands of lags per candidate, as comb_estimate computes them: center j Pi / 2 (j even: a tooth
        // at k Pi, k = j / 2; j odd: a negative tooth at (k -/+ 1/2) Pi), from floor(c - width Pi) to
        // ceil(c + width Pi), clipped below at lag 0 (comb_estimate's clipping; it acts only for width > 1/2). Inside
        // the reach (the only candidates computed) hi <= ceil((teeth + 1/2 + width) Pi) <= ceil((n - 1) / 2) <= n - 1
        // needs no clipping.
        bands_.resize(grid_.size());
        for (std::size_t g = 0; g < grid_.size(); ++g) {
            const double period = 2.0 * grid_[g] * rate_;
            const double half = cfg.comb_width * period;
            for (std::size_t j = 1; j <= bands; ++j) {
                const double center = (static_cast<double>(j) * 0.5) * period;
                bands_[g].lo.push_back(std::max(0, static_cast<int>(std::floor(center - half))));
                bands_[g].hi.push_back(static_cast<int>(std::ceil(center + half)));
            }
        }
        entering_.assign(grid_.size(), std::vector<double>(bands, 0.0));
        tail_ = s_end_ = entering_;
        have_entering_.assign(grid_.size(), 0);
        have_end_.assign(grid_.size(), 0);
    }
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
        // Keep the longest window; with per-candidate windows also the samples from the last recomputation's
        // longest window on, which the slid sums subtract at the next one (unless that one recomputes them all).
        const auto keep = static_cast<std::int64_t>(longest_);
        const std::int64_t end = buffer_start_ + static_cast<std::int64_t>(buffer_.size());
        std::int64_t drop_to = end - keep;
        if (any_per_candidate_ && last_end_ >= 0 && end - last_end_ < keep)
            drop_to = std::min(drop_to, last_end_ - keep);
        if (drop_to > buffer_start_) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(drop_to - buffer_start_));
            buffer_start_ = drop_to;
        }
    }
    carry_.assign(x.begin() + static_cast<std::ptrdiff_t>(whole), x.end());
    pending_ += static_cast<long long>(p.size());
}

double Periodicity::band_product(std::size_t g, std::size_t j, std::int64_t i) const {
    // p_i times the sum of p over the band's lags after i: the sum over tau in [lo, hi] of p_(i + tau).
    const std::int64_t at = i - buffer_start_;
    const auto lo = static_cast<std::int64_t>(bands_[g].lo[j]);
    const auto hi = static_cast<std::int64_t>(bands_[g].hi[j]);
    return finite_[static_cast<std::size_t>(at)] *
           (s_[static_cast<std::size_t>(at + hi + 1)] - s_[static_cast<std::size_t>(at + lo)]);
}

double Periodicity::band_products(std::size_t g, std::size_t j, std::int64_t from, std::int64_t to) const {
    double sum = 0.0;
    for (std::int64_t i = from; i <= to; ++i) sum += band_product(g, j, i);
    return sum;
}

std::size_t Periodicity::per_candidate_row(std::size_t r, std::int64_t e) {
    // Candidate g's window is [s, e), n samples, s = e - n. With x = p - m (m the window's mean), a band of lags
    // [lo, hi] sums R = sum over tau of sum over i = s ... e-1-tau of x_i x_(i+tau)
    //   = F + tail - m (A + B) + m^2 (L n - (lo + hi) L / 2),  L = hi - lo + 1, where
    // F = sum over i = s ... e-1-hi of p_i (S(i+hi+1) - S(i+lo))  (every lag of the band inside the window; slid),
    // tail = sum over i = e-hi ... e-1-lo of p_i (S(e) - S(i+lo))   (the lags that reach the window's end),
    // A = sum over tau of (S(e-tau) - S(s)) and B = sum over tau of (S(e) - S(s+tau)) (the sums of p that m
    // multiplies), and c0 = sum over the window of x^2 = (Q(e) - Q(s)) - n m^2. The band's value is R / c0 / L,
    // the mean of the normalized autocorrelation over the band, as comb_estimate's.
    const std::size_t G = grid_.size();
    const int K = cfg_.comb_teeth;
    const auto J = static_cast<std::size_t>(2 * std::max(K, 0) + 1);
    const double reach = (static_cast<double>(K) + 0.5) + cfg_.comb_width;
    const auto m = static_cast<std::int64_t>(buffer_.size());
    auto S = [&](std::int64_t a) { return s_[static_cast<std::size_t>(a - buffer_start_)]; };
    auto Q = [&](std::int64_t a) { return q_[static_cast<std::size_t>(a - buffer_start_)]; };
    auto bad = [&](std::int64_t a) { return bad_[static_cast<std::size_t>(a - buffer_start_)]; };
    auto& score = scores_[r];
    score.assign(G, -std::numeric_limits<double>::infinity());
    std::vector<double> contrast(static_cast<std::size_t>(std::max(K, 0)));
    std::vector<double> band(J);
    for (std::size_t g = 0; g < G; ++g) {
        Slid& sl = slid_[r][g];
        const std::int64_t n = windows_[r][g];
        const double period = 2.0 * grid_[g] * rate_;
        if (!(reach * period <= static_cast<double>(n - 1) / 2.0) || m < n) {
            sl.valid = false;  // out of reach (comb_estimate's rule) or its window not full yet: no part
            continue;
        }
        const std::int64_t s = e - n;
        if (bad(e) != bad(s)) {
            // A NaN or an infinity in the window: no finite score, as comb_estimate's (its mean and so every x is
            // then NaN). The slid sums hold the sample as 0 and stay finite.
            score[g] = std::numeric_limits<double>::quiet_NaN();
        }
        const auto& lo = bands_[g].lo;
        const auto& hi = bands_[g].hi;
        bool finite = sl.valid;
        for (std::size_t j = 0; j < J && finite; ++j) finite = std::isfinite(sl.full[j]);
        if (finite && last_end_ >= 0 && e - sl.computed_at < n) {
            // Slide from [last_end_ - n, last_end_) to [s, e): the products entering at the end (shared by the
            // rows) added, those leaving at the start subtracted.
            if (!have_entering_[g]) {
                for (std::size_t j = 0; j < J; ++j)
                    entering_[g][j] = band_products(g, j, last_end_ - hi[j], e - 1 - hi[j]);
                have_entering_[g] = 1;
            }
            for (std::size_t j = 0; j < J; ++j)
                sl.full[j] += entering_[g][j] - band_products(g, j, last_end_ - n, s - 1);
        } else {
            // Directly: the first time, once the window has slid by its own length, or after a non-finite value.
            for (std::size_t j = 0; j < J; ++j) sl.full[j] = band_products(g, j, s, e - 1 - hi[j]);
            sl.valid = true;
            sl.computed_at = e;
        }
        if (std::isnan(score[g])) continue;
        const double se = S(e), ss = S(s);
        const double mean = (se - ss) / static_cast<double>(n);
        const double c0 = (Q(e) - Q(s)) - (se - ss) * mean;
        if (c0 <= 1e-12 * static_cast<double>(n)) continue;  // p does not vary (comb_estimate's rule): no part
        if (!have_end_[g]) {
            for (std::size_t j = 0; j < J; ++j) {
                double tail = 0.0, s_end = 0.0;
                for (std::int64_t i = e - hi[j]; i <= e - 1 - lo[j]; ++i)
                    tail += finite_[static_cast<std::size_t>(i - buffer_start_)] * (se - S(i + lo[j]));
                for (std::int64_t tau = lo[j]; tau <= hi[j]; ++tau) s_end += S(e - tau);
                tail_[g][j] = tail;
                s_end_[g][j] = s_end;
            }
            have_end_[g] = 1;
        }
        for (std::size_t j = 0; j < J; ++j) {
            const double L = static_cast<double>(hi[j] - lo[j] + 1);
            double s_start = 0.0;
            for (std::int64_t tau = lo[j]; tau <= hi[j]; ++tau) s_start += S(s + tau);
            const double a = s_end_[g][j] - L * ss;
            const double b = L * se - s_start;
            const double lags = L * static_cast<double>(n) - 0.5 * static_cast<double>(lo[j] + hi[j]) * L;
            const double sum = sl.full[j] + tail_[g][j] - mean * (a + b) + mean * mean * lags;
            band[j] = sum / c0 / L;
        }
        // band[j] is centered at (j + 1) Pi / 2: tooth k (at k Pi) is index 2k - 1, its neighbors 2k - 2 and 2k.
        for (int k = 1; k <= K; ++k) {
            const auto t = static_cast<std::size_t>(2 * k - 1);
            contrast[static_cast<std::size_t>(k - 1)] = band[t] - 0.5 * (band[t - 1] + band[t + 1]);
        }
        score[g] = pairwise_sum(contrast) / static_cast<double>(K);
    }
    // The row's estimate: np.argmax's pick (the first NaN if there is one, else the first maximum), none unless
    // finite, as comb_estimate's.
    std::size_t best = 0;
    for (std::size_t g = 0; g < G; ++g) {
        if (std::isnan(score[g])) {
            best = g;
            break;
        }
        if (score[g] > score[best]) best = g;
    }
    if (G == 0 || !std::isfinite(score[best]))
        per_window_.emplace_back(std::nullopt, 0.0);
    else
        per_window_.emplace_back(grid_[best], score[best]);
    return best;
}

PeriodicityUpdate Periodicity::update(bool force, std::optional<double> dit_s) {
    if (!force && pending_ < update_every_) {
        PeriodicityUpdate r = last_;
        r.updated = false;
        return r;
    }
    pending_ = 0;
    const std::int64_t e = buffer_start_ + static_cast<std::int64_t>(buffer_.size());
    if (any_per_candidate_) {
        // S and Q (the running sums of p and p^2) over the buffer, from its first sample, with a NaN or an infinity
        // held as 0 (so it reaches no window but its own) and counted (bad_).
        finite_.resize(buffer_.size());
        s_.assign(buffer_.size() + 1, 0.0);
        q_.assign(buffer_.size() + 1, 0.0);
        bad_.assign(buffer_.size() + 1, 0);
        for (std::size_t i = 0; i < buffer_.size(); ++i) {
            const bool ok = std::isfinite(buffer_[i]);
            finite_[i] = ok ? buffer_[i] : 0.0;
            s_[i + 1] = s_[i] + finite_[i];
            q_[i + 1] = q_[i] + finite_[i] * finite_[i];
            bad_[i + 1] = bad_[i] + (ok ? 0 : 1);
        }
        std::fill(have_entering_.begin(), have_entering_.end(), 0);
        std::fill(have_end_.begin(), have_end_.end(), 0);
    }
    std::optional<PeriodicityUpdate> found;
    double best = 0.0;
    per_window_.clear();
    used_.assign(windows_.size(), 0);
    for (std::size_t r = 0; r < windows_.size(); ++r) {
        double window_s = 0.0;
        if (windows_[r].size() == 1) {
            // A shared row: every candidate over the same most recent samples. With windows that follow a dit
            // (B4a-C) and a T-hat, N_w,r x T-hat (T-hat capped at dit_cap_s_, which the buffer holds).
            auto wn = static_cast<std::size_t>(windows_[r][0]);
            if (!dits_.empty() && dit_s && std::isfinite(*dit_s) && *dit_s > 0.0)
                wn = static_cast<std::size_t>(std::max(16, round_int(dits_[r] * std::min(*dit_s, dit_cap_s_) * rate_)));
            if (buffer_.size() < wn) {
                per_window_.emplace_back(std::nullopt, 0.0);
                continue;
            }
            const std::span<const double> recent(buffer_.data() + (buffer_.size() - wn), wn);
            per_window_.push_back(comb_estimate(recent, rate_, grid_, cfg_.comb_teeth, cfg_.comb_width));
            window_s = static_cast<double>(wn) / rate_;
            used_[r] = static_cast<int>(wn);
        } else {
            const std::size_t g = per_candidate_row(r, e);
            window_s = static_cast<double>(windows_[r][g]) / rate_;
            if (per_window_.back().first) used_[r] = windows_[r][g];
        }
        const auto& est = per_window_.back();
        if (est.second > best) best = est.second;  // Python's max(best, score)
        if (!found && est.first && est.second >= threshold_)
            found = PeriodicityUpdate{est.first, est.second, window_s, false};
    }
    last_end_ = e;
    last_ = found ? *found : PeriodicityUpdate{std::nullopt, best, std::nullopt, false};
    PeriodicityUpdate r = last_;
    r.updated = true;
    return r;
}

}  // namespace kz4ap::bank
