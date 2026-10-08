// The bank decoder's periodicity estimator: a port of training/kz4ap_proto/periodicity.py, the comb on the
// dit-plus-space period Pi = 2T only (docs/signal-processing.md section 8c, "Periodicity"). The coarse speed
// T_P comes from branch 1's squelched keying probability p, averaged down to periodicity_rate_hz, over several
// windows; the confident estimate of the shortest window is T_P. The prototype's other two methods (the
// sign-weighted edge comb and the spectrum fit) were not adopted and are not ported. Every candidate is judged over
// the same windows in seconds (stage 1's rule), computed as the prototype computes them: 5 and 10 s since the
// owner's amendment of 2026-10-07 (stage 1's were 2, 5 and 10 s).
#pragma once

#include "kz4ap/bank/bank_config.hpp"

#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace kz4ap::bank {

// Candidate dits, s: 1.2 s / max_wpm x 1.01^i, i = 0 ... ceil(ln(t_max / t_min) / ln 1.01), t_max = 1.2 s /
// min_wpm (12 ms to 242.2 ms, 303 points, with the defaults). The 1% step is the prototype's constant here,
// not cfg.t_grid_step.
std::vector<double> t_grid(const BankConfig& cfg);

// (T s, score), or (none, 0) when p does not vary (fewer than 16 values, or sum of (p - mean)^2 <= 1e-12 x
// count) or no candidate is within reach. The comb on mean-removed p (rate_hz samples/s) at the period
// Pi = 2T of each candidate T of grid: teeth at k Pi (k = 1 ... teeth), negative teeth at (k -/+ 1/2) Pi, each
// the mean of the biased, normalized autocorrelation over the samples from floor(c - width Pi) to
// ceil(c + width Pi) (clipped to the lags 0 ... n - 1); score = mean over k of tooth - (left + right) / 2,
// dimensionless. A candidate is in reach when (teeth + 1/2 + width) Pi <= (n - 1) / 2 samples. The first
// maximum wins.
std::pair<std::optional<double>, double> comb_estimate(std::span<const double> p, double rate_hz,
                                                       const std::vector<double>& grid, int teeth, double width);

// What Periodicity::update returns: T_P (s, none when no window is confident), the confidence (the chosen
// window's score; when none is confident, max(0, every window's score), so 0 if none is positive;
// dimensionless), the window that gave T_P (s, none when none) and whether the estimate was recomputed in
// this call.
struct PeriodicityUpdate {
    std::optional<double> t_p_s;
    double confidence = 0.0;
    std::optional<double> window_s;
    bool updated = false;
};

// Branch 1's posterior p (at the channel rate rate_hz, samples/s), averaged down by
// factor = max(1, round(rate_hz / periodicity_rate_hz)) samples into a buffer as long as the longest window;
// every periodicity_update_s of pushed samples each window of cfg.periodicity_windows_s (shortest first) is
// estimated by comb_estimate over its most recent samples, and the confident estimate (score >=
// comb_confidence_min) of the shortest window is T_P. Throws std::invalid_argument for a periodicity_method other
// than "comb" or no windows.
class Periodicity {
public:
    Periodicity(const BankConfig& cfg, double rate_hz);

    // Appends p's samples (the channel rate); a remainder shorter than factor waits for the next push.
    void push(std::span<const double> p);
    // Recomputes when force is set or at least update_every() samples were pushed since the last
    // recomputation; otherwise returns the last result with updated = false.
    PeriodicityUpdate update(bool force = false);

    // The last recomputation's (T s or none, score) per window, shortest first; (none, 0) for a window
    // the buffer does not fill yet.
    const std::vector<std::pair<std::optional<double>, double>>& per_window() const { return per_window_; }
    // Samples at the channel rate between recomputations: round(periodicity_update_s x rate_hz).
    int update_every() const { return update_every_; }
    // The averaging factor, samples, and the averaged rate, samples/s.
    int factor() const { return factor_; }
    double rate_hz() const { return rate_; }
    // The windows, samples at the averaged rate, ascending: max(16, round(w x rate_hz())).
    const std::vector<int>& windows() const { return windows_; }
    // The averaged p, oldest first, at most the longest window (dimensionless).
    const std::vector<double>& buffer() const { return buffer_; }

private:
    BankConfig cfg_;
    int factor_;
    double rate_;
    std::vector<int> windows_;
    int update_every_;
    std::vector<double> grid_;
    double threshold_;
    std::vector<double> buffer_;
    std::vector<double> carry_;
    long long pending_ = 0;
    PeriodicityUpdate last_;
    std::vector<std::pair<std::optional<double>, double>> per_window_;
};

}  // namespace kz4ap::bank
