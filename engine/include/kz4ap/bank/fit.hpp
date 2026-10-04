// The bank decoder's duration fit: a port of training/kz4ap_proto/fit.py (docs/signal-processing.md section
// 8c, "Duration fit"). Per branch, a mixture over the element classes (dit, dah; element space, character
// gap, word gap) with log-normal scatter in ln(duration) plus the branch's timing-resolution variance, and a
// log-uniform outlier class; exponential memory held exactly by recursive log-likelihood tables over a grid
// of (T, q, w, T_g); the grid maximum (with the optional T_P prior), then a Gauss-Newton refinement in
// ln(duration) on the retained history, accepted only if it does not lower the weighted log-likelihood.
//
// theta = (T, w, qT, T_g), all s. Class medians: dit T + w, dah qT + w, element space T - w, character gap
// 3 T_g - w, word gap 7 T_g - w. Log-likelihoods are in nats; durations in s; variances in s^2.
#pragma once

#include "kz4ap/bank/bank_config.hpp"

#include <array>
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kz4ap::bank {

// One mark or space: the prototype's (is_mark, duration s, sigma_t^2 s^2) tuple.
struct Obs {
    bool is_mark;
    double d_s;       // duration, s
    double var_t_s2;  // timing-resolution variance sigma_t^2, s^2
};

// The prototype's Fit (fit.py): the fitted timing and its quality.
struct Fit {
    double t_s;      // T, the dit, s
    double q;        // dah/dit ratio, dimensionless
    double w_s;      // key weighting w, s
    double tg_s;     // gap timebase T_g, s
    double quality;  // Q: weighted mean log-likelihood per element, nats
    double weight;   // the memory's weight, elements

    // (T, w, qT, T_g), s; qT is recomputed as q x T, as the prototype's Fit.theta() does.
    std::array<double, 4> theta() const { return {t_s, w_s, q * t_s, tg_s}; }
};

// The class priors derived from VE3NEA's character and word-length tables: P(dit), P(dah) among marks and
// P(element space), P(character gap), P(word gap) among spaces.
struct ClassPriors {
    std::array<double, 2> marks;
    std::array<double, 3> spaces;
};
ClassPriors class_priors();

// sigma_t^2, s^2: two edges of (L / a)^2 each plus sampling, 1 / (12 r^2) per edge; a floored at 1.
// length_s: the branch's length L, s; a: s / sigma_v, dimensionless; rate_hz: samples/s.
double resolution_var_s2(double length_s, double a, double rate_hz);

// numpy's logaddexp, ln(e^x + e^y): x + ln 2 where x == y (both -inf gives -inf), else
// max(x, y) + log1p(exp(-|x - y|)), nats.
double logaddexp(double x, double y);

// ln(e^x_0 + ... + e^x_(n-1) + e^out), nats: the fit's one-pass log-sum-exp (Plan B task B2(a); fit.cpp,
// lse_block): m + ln(1 + sum of e^(x - m) over the other terms), m the largest, the others added after the
// largest in order (the outlier out last), terms more than 40 nats below m left out (no bit changes); exp and
// log are the fit's (SLEEF's, src/bank/vecmath.hpp; B2(b)). A NaN term gives NaN, otherwise a +inf term gives
// +inf. n <= 255.
double log_sum_exp(const double* x, std::size_t n, double out);

// Per observation: ll, ln(prior x density in ln d) of each class (-inf for the other kind of interval, or a
// class whose median is not positive; the others' priors are not renormalized); total, the log-likelihood
// with the outlier class; var_lin, each class's variance in duration to first order, s^2. Durations are
// clamped to the outlier range. Throws std::invalid_argument if any duration is not > 0 s.
struct ClassLogliks {
    std::vector<std::array<double, 5>> ll;
    std::vector<double> total;
    std::vector<std::array<double, 5>> var_lin;
};
ClassLogliks class_logliks(const std::array<double, 4>& theta, const std::vector<Obs>& obs, const BankConfig& cfg);

// Mean log-likelihood per element of the observations under the fit, nats; -inf if there is no fit or no
// observation. Throws std::invalid_argument if any duration is not > 0 s.
double observations_loglik(const Fit& fit, const std::vector<Obs>& obs, const BankConfig& cfg);
double observations_loglik(const std::optional<Fit>& fit, const std::vector<Obs>& obs, const BankConfig& cfg);

// True if the mark is likelier a dah than a dit under the fit (d s, var_t s^2).
bool classify_mark(const Fit& fit, double d, double var_t, const BankConfig& cfg);
// The likeliest space class under the fit: "element", "character" or "word".
std::string classify_space(const Fit& fit, double d, double var_t, const BankConfig& cfg);

// One branch's fit. Every observation multiplies both tables by lambda = exp(-1 / N_mem) and adds its
// log-likelihood at every grid point to one of them (untruncated memory). The last ceil(4 N_mem)
// observations are retained for the refinement, its acceptance test and the quality.
class DurationFit {
public:
    explicit DurationFit(const BankConfig& cfg);

    // An independent copy (the read-only grid constants are shared, as by every fit of an equal configuration).
    DurationFit copy() const { return *this; }

    // One more mark or space: duration_s, s, and its timing-resolution variance var_t, s^2. A duration that is
    // not > 0 s is ignored; others are clamped to the outlier range before scoring.
    void add(bool is_mark, double duration_s, double var_t);

    // Grid indices (T, w, q, T_g) of the maximum of the tables plus the T_P prior
    // (-prior_weight (ln T - ln T_P)^2 / (2 sigma_P^2), applied when prior_t_s is set and prior_weight > 0);
    // none before any observation.
    std::optional<std::array<int, 4>> grid_index(std::optional<double> prior_t_s = std::nullopt,
                                                 double prior_weight = 0.0) const;
    // theta = (T, w, qT, T_g), s, at grid_index; none before any observation.
    std::optional<std::array<double, 4>> grid_theta(std::optional<double> prior_t_s = std::nullopt,
                                                    double prior_weight = 0.0) const;
    // Sum over the retained history of lambda^age x the log-likelihood, plus the T_P prior term, nats.
    double weighted_loglik(const std::array<double, 4>& theta, std::optional<double> prior_t_s = std::nullopt,
                           double prior_weight = 0.0) const;
    // The grid maximum refined by Gauss-Newton in ln d, the refined point kept only if its weighted
    // log-likelihood (with the prior term) is at least the grid point's; none before any observation.
    std::optional<Fit> best(std::optional<double> prior_t_s = std::nullopt, double prior_weight = 0.0) const;
    // The Gauss-Newton refinement from theta (refine_iterations steps) on the retained history; requires at
    // least one retained observation.
    std::array<double, 4> refine(const std::array<double, 4>& theta, std::optional<double> prior_t_s,
                                 double prior_weight) const;

    // The memory's weight, elements: lambda x weight + 1 per observation.
    double weight() const { return weight_; }
    // lambda = exp(-1 / N_mem), dimensionless.
    double lambda() const;
    // The retained history, newest first, at most history_capacity() observations.
    const std::deque<Obs>& history() const { return history_; }
    std::size_t history_capacity() const;

    // The grid's axes: T (s, log-spaced), q, w / T and T_g / T (dimensionless).
    const std::vector<double>& t_grid_s() const;
    const std::vector<double>& q_grid() const;
    const std::vector<double>& w_grid() const;
    const std::vector<double>& tg_grid() const;
    // The tables, nats, row-major: marks over (T, q, w), spaces over (T, w, T_g).
    const std::vector<double>& mark_table() const { return mark_table_; }
    const std::vector<double>& space_table() const { return space_table_; }
    // One observation's log-likelihood at every grid point of its table (the outlier class included), nats,
    // laid out as that table. The duration must be > 0 s.
    std::vector<double> grid_loglik(bool is_mark, double duration_s, double var_t) const;

    struct Model;  // the read-only grid and constants, shared by every fit of an equal configuration

    // The fit's grid constants, for tests: fits built from configurations equal in every field the constants
    // read share one object.
    const Model* model() const { return m_.get(); }

private:
    // The process-wide Model of this configuration (built on first use, freed when no fit uses it).
    static std::shared_ptr<const Model> shared_model(const BankConfig& cfg);

    std::shared_ptr<const Model> m_;  // immutable, shared by every fit of an equal configuration
    std::vector<double> mark_table_;
    std::vector<double> space_table_;
    double weight_ = 0.0;
    std::deque<Obs> history_;
};

}  // namespace kz4ap::bank
