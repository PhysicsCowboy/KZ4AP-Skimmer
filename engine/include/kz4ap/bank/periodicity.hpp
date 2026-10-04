// The bank decoder's periodicity estimator: a port of training/kz4ap_proto/periodicity.py, the comb on the
// dit-plus-space period Pi = 2T only (docs/signal-processing.md section 8c, "Periodicity"). The coarse speed
// T_P comes from branch 1's squelched keying probability p, averaged down to periodicity_rate_hz, over several
// windows; the confident estimate of the shortest window is T_P. The prototype's other two methods (the
// sign-weighted edge comb and the spectrum fit) were not adopted and are not ported. Since Plan B's B4a each
// candidate dit T is judged over its own window N_w x T (timing.hpp); a window shared by every candidate (stage 1's
// fixed windows in seconds) remains available for the golden tests and is computed as the prototype computes it.
#pragma once

#include "kz4ap/bank/bank_config.hpp"

#include <cstdint>
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
// dimensionless), the window that gave T_P (s: the chosen candidate's window; none when none) and whether the
// estimate was recomputed in this call.
struct PeriodicityUpdate {
    std::optional<double> t_p_s;
    double confidence = 0.0;
    std::optional<double> window_s;
    bool updated = false;
};

// Branch 1's posterior p (at the channel rate rate_hz, samples/s), averaged down by
// factor = max(1, round(rate_hz / periodicity_rate_hz)) samples into a buffer as long as the longest window;
// every periodicity_update_s of pushed samples each window (shortest first) is estimated, and the confident
// estimate (score >= comb_confidence_min) of the shortest window is T_P.
//
// A window is a row of lengths: one length for every candidate (a shared window: comb_estimate over the most
// recent samples, as the prototype), or one length per candidate T of t_grid(cfg) (since Plan B's B4a, N_w x T):
// each candidate's score is then the comb's score of that candidate alone on its own most recent samples, the
// same quantity comb_estimate(recent n_T samples, rate, {T}, ...) gives, and the row's estimate is the first
// maximum over the candidates whose window the buffer fills (a candidate whose window is not full yet, or whose
// p does not vary, takes no part). Such a row is computed without a per-candidate FFT: each band of lags of the
// comb is a sum of lagged products, sum over tau in [lo, hi] of sum over i of p_i p_(i+tau) =
// sum over i of p_i (S(i+hi+1) - S(i+lo)) with S the running sum of p, kept per candidate and band and slid
// from one recomputation to the next (the products entering at the window's end added, those leaving at its
// start subtracted) and recomputed directly once the window has slid by its own length (or when it is not
// finite); the mean is removed algebraically (docs/signal-processing.md section 8c, "Periodicity").
class Periodicity {
public:
    // The configuration's windows (bank_timing(cfg).periodicity_windows_s).
    Periodicity(const BankConfig& cfg, double rate_hz);
    // Explicit windows, s: rows of one value or of one value per candidate of t_grid(cfg). Throws
    // std::invalid_argument for a periodicity_method other than "comb", no rows, or a row of another size.
    Periodicity(const BankConfig& cfg, double rate_hz, const std::vector<std::vector<double>>& windows_s);

    // Appends p's samples (the channel rate); a remainder shorter than factor waits for the next push.
    void push(std::span<const double> p);
    // Recomputes when force is set or at least update_every() samples were pushed since the last
    // recomputation; otherwise returns the last result with updated = false.
    PeriodicityUpdate update(bool force = false);

    // The last recomputation's (T s or none, score) per window, shortest first; (none, 0) for a window
    // the buffer does not fill yet (for a per-candidate window: for none of its candidates).
    const std::vector<std::pair<std::optional<double>, double>>& per_window() const { return per_window_; }
    // Samples at the channel rate between recomputations: round(periodicity_update_s x rate_hz).
    int update_every() const { return update_every_; }
    // The averaging factor, samples, and the averaged rate, samples/s.
    int factor() const { return factor_; }
    double rate_hz() const { return rate_; }
    // The candidate dits, s (t_grid(cfg)).
    const std::vector<double>& grid() const { return grid_; }
    // The windows, samples at the averaged rate, max(16, round(w x rate_hz())): one row per window, ascending by
    // the first entry; a row holds one value (shared) or one per candidate of grid().
    const std::vector<std::vector<int>>& windows() const { return windows_; }
    // The window of candidate g in row r, samples at the averaged rate.
    int window_samples(std::size_t r, std::size_t g) const {
        return windows_[r].size() == 1 ? windows_[r][0] : windows_[r][g];
    }
    // The last recomputation's score of every candidate of a per-candidate row, dimensionless (minus infinity: no
    // part in the row's estimate); empty for a shared row.
    const std::vector<std::vector<double>>& candidate_scores() const { return scores_; }
    // The averaged p, oldest first (dimensionless): at least the longest window once that much was pushed, and
    // with per-candidate windows also the samples since the last recomputation.
    const std::vector<double>& buffer() const { return buffer_; }

private:
    // The comb's band lags of candidate g (j = 1 ... 2 teeth + 1: center j Pi / 2): [lo, hi], samples.
    struct Bands {
        std::vector<int> lo, hi;
    };
    // The slid sums of one candidate of a per-candidate row.
    struct Slid {
        std::vector<double> full;          // per band j: sum over i in [s, e - 1 - hi_j] of p_i (S(i+hi+1) - S(i+lo))
        bool valid = false;
        std::int64_t computed_at = 0;      // absolute end e of the last direct computation
    };
    double band_product(std::size_t g, std::size_t j, std::int64_t i) const;
    double band_products(std::size_t g, std::size_t j, std::int64_t from, std::int64_t to) const;
    // Computes row r's candidates at the end e (absolute), appends the row's estimate to per_window_ and returns
    // the chosen candidate's index.
    std::size_t per_candidate_row(std::size_t r, std::int64_t e);

    BankConfig cfg_;
    int factor_;
    double rate_;
    std::vector<std::vector<int>> windows_;
    std::size_t longest_ = 0;  // the longest window, samples
    bool any_per_candidate_ = false;
    int update_every_;
    std::vector<double> grid_;
    std::vector<Bands> bands_;        // per candidate
    double threshold_;
    std::vector<double> buffer_;
    std::int64_t buffer_start_ = 0;   // absolute index (averaged samples) of buffer_[0]
    std::int64_t last_end_ = -1;      // absolute end of the buffer at the last recomputation (-1: none)
    std::vector<double> carry_;
    long long pending_ = 0;
    PeriodicityUpdate last_;
    std::vector<std::pair<std::optional<double>, double>> per_window_;
    std::vector<std::vector<double>> scores_;  // per row: per candidate (per-candidate rows) or empty
    std::vector<std::vector<Slid>> slid_;      // per row: per candidate (per-candidate rows) or empty
    // Per recomputation: the buffer with a NaN or an infinity held as 0 (finite_), S (running sum of finite_) and Q
    // (of its squares), S[q] = sum of finite_[0 ... q-1], and the running count of NaNs and infinities (bad_).
    std::vector<double> finite_, s_, q_;
    std::vector<std::int32_t> bad_;  // the buffer holds fewer than 2^31 samples
    // Per recomputation, per candidate: the entering sums since the last recomputation (computed once, shared by
    // the rows), and the tail and S-sum terms at the end e (shared by the rows).
    std::vector<std::vector<double>> entering_, tail_, s_end_;
    std::vector<char> have_entering_, have_end_;
};

}  // namespace kz4ap::bank
