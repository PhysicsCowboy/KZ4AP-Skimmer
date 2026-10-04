#include "kz4ap/bank/fit.hpp"

#include "kz4ap/bank/numpy_sum.hpp"
#include "kz4ap/bank/python_sum.hpp"
#include "kz4ap/morse.hpp"
#include "ve3nea.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace kz4ap::bank {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
// npymath's NPY_LOGE2, the double nearest ln 2.
constexpr double kLogE2 = 0.693147180559945309417232121458176568;
// 0.5 ln(2 pi), as the prototype's LOG_SQRT_2PI.
const double kLogSqrt2Pi = 0.5 * std::log(2.0 * std::numbers::pi);

// Class medians are linear in theta = (T, w, qT, T_g): dit T + w, dah qT + w, element space T - w, character
// gap 3 T_g - w, word gap 7 T_g - w.
constexpr double kDesign[5][4] = {{1.0, 1.0, 0.0, 0.0},
                                  {0.0, 1.0, 1.0, 0.0},
                                  {1.0, -1.0, 0.0, 0.0},
                                  {0.0, -1.0, 0.0, 3.0},
                                  {0.0, -1.0, 0.0, 7.0}};
constexpr bool kIsMarkClass[5] = {true, true, false, false, false};
constexpr const char* kSpaceKinds[3] = {"element", "character", "word"};

// VE3NEA's CW character frequencies and word-length probabilities (ve3nea.hpp).
using detail::kVe3neaCharWeights;
using detail::kVe3neaWordLengthProbs;

// ln((1 - epsilon) x prior) per class, nats (dit, dah, element, character, word).
std::array<double, 5> log_priors(const BankConfig& cfg) {
    const ClassPriors p = class_priors();
    const double keep = 1.0 - cfg.outlier_prior;
    return {std::log(keep * p.marks[0]), std::log(keep * p.marks[1]), std::log(keep * p.spaces[0]),
            std::log(keep * p.spaces[1]), std::log(keep * p.spaces[2])};
}

// The outlier class's density in ln d: epsilon / ln(hi / lo), as a log, nats.
double log_outlier(const BankConfig& cfg) {
    const double lo = cfg.outlier_range_s.at(0);
    const double hi = cfg.outlier_range_s.at(1);
    return std::log(cfg.outlier_prior) - std::log(std::log(hi / lo));
}

std::array<double, 5> class_sigma2(const BankConfig& cfg) {
    std::array<double, 5> s{};
    for (int c = 0; c < 5; ++c) {
        const double sigma = kIsMarkClass[c] ? cfg.sigma_ln_mark : cfg.sigma_ln_space;
        s[static_cast<std::size_t>(c)] = sigma * sigma;
    }
    return s;
}

// DESIGN @ theta, s (the medians; the terms added in column order).
std::array<double, 5> medians(const std::array<double, 4>& theta) {
    std::array<double, 5> mu{};
    for (int c = 0; c < 5; ++c) {
        double s = 0.0;
        for (int j = 0; j < 4; ++j) s += kDesign[c][j] * theta[static_cast<std::size_t>(j)];
        mu[static_cast<std::size_t>(c)] = s;
    }
    return mu;
}

// logaddexp's value, ln(e^x + e^y), nats: numpy's formula by parts (see logaddexp below), returning max(x, y)
// without evaluating exp and log1p where |x - y| is so large that the formula's result is max(x, y) exactly.
//
// Proof that the skip changes no bit. Let m = max(x, y) be a normal double with exponent k (2^k <= |m| <
// 2^(k+1)) and k >= -1000, and d = |x - y| (computed, as the formula uses it). The formula returns
// fl(m + delta), delta = fl(log1p(fl(exp(-d)))) >= 0. The doubles next to m are at least 2^(k-53) away (the gap
// below |m| = 2^k), so fl(m + delta) = m whenever delta < 2^(k-54). If d >= (58 - k) ln 2 (the threshold is
// computed to within relative 2^-52, which changes e^-d by less than relative 2^-40), the exact e^-d is at most
// 2^(k-58) (1 + 2^-40); exp and log1p err by less than one unit in the last place (with an absolute error of at
// most 2^-1074 where the result is subnormal), and log1p(u) <= u, so delta <= 2^(k-58) (1 + 2^-39) + 2^-1073,
// which is below 2^(k-54) for every k >= -1014. Margin: a factor 16, against libm errors of a few units.
// Other cases are left to the formula: m zero, subnormal or below 2^-1000 in magnitude (exponent field < 23),
// and m NaN (d NaN: the comparison is false). m = +inf has exponent field 2047, a negative threshold, and
// the formula's value inf + log1p(exp(-|inf - y|)) = inf = m; x == y is handled first, as before.
inline double lae(double x, double y) {
    if (x == y) return x + kLogE2;
    const double m = std::max(x, y);
    const double d = std::abs(x - y);
    const auto field = static_cast<int>((std::bit_cast<std::uint64_t>(m) >> 52) & 0x7ff);
    if (field >= 23 && d >= static_cast<double>(1081 - field) * kLogE2) return m;  // (58 - k) ln 2, k = field - 1023
    return m + std::log1p(std::exp(-d));
}

// Terms more than this far below the largest are left out of a log-sum-exp, nats (derived, see lse).
constexpr double kNegligibleNats = 40.0;

// ln(e^x_0 + ... + e^x_(n-1) + e^out), nats, in one pass (Plan B task B2(a), near-exact): m + ln(1 + the sum
// of e^(x - m) over the other terms), m the largest term (the first largest; out where a class term only ties
// it). The sum starts at the largest term's e^0 = 1 and the others are added after it in class order, the
// outlier last (unless it is the largest); terms more than kNegligibleNats below m are left out, and the log
// is skipped where the sum is exactly 1 (ln 1 = 0 and m + 0 = m: m is never -0).
//
// The leave-out changes no bit. Every partial sum is >= 1 (it starts at 1 and adds e^r >= 0), and the doubles
// above a value >= 1 are at least 2^-52 apart, so adding t < 2^-53 rounds back to the partial sum. A left-out
// term has r <= -40 nats, e^r <= 4.25e-18, and exp errs by less than one unit in the last place, so its
// computed value is below 4.3e-18 < 2^-57 < 2^-53: adding it would leave the partial sum unchanged, and the
// remaining terms are added in the same order to the same partial sums with or without it (tested bit for
// bit: BankFitB2a.LogSumExpLeaveOutChangesNoBit).
//
// Non-finite terms: a NaN term (x or out) is returned (the logaddexp chain also gave NaN); otherwise a +inf
// term gives +inf (as the chain did); -inf terms add nothing (e^-inf = 0) and are never the largest while out
// is finite.
inline double lse(const double* x, std::size_t n, double out) {
    if (std::isnan(out)) return out;
    std::size_t top = n;  // n: the outlier
    double m = out;
    for (std::size_t c = 0; c < n; ++c) {
        if (std::isnan(x[c])) return x[c];
        if (x[c] > m) {
            m = x[c];
            top = c;
        }
    }
    if (m == kInf) return m;
    double sum = 1.0;
    for (std::size_t c = 0; c < n; ++c) {
        if (c == top) continue;
        const double r = x[c] - m;
        if (r > -kNegligibleNats) sum += std::exp(r);
    }
    if (top != n) {
        const double r = out - m;
        if (r > -kNegligibleNats) sum += std::exp(r);
    }
    return sum == 1.0 ? m : m + std::log(sum);
}

// What the prototype's _terms / _class_terms compute for each observation: ll (n, 5), total (n), s2 (n, 5),
// and per class the median (1 where not positive) and its log.
struct Terms {
    std::vector<std::array<double, 5>> ll;
    std::vector<double> total;
    std::vector<std::array<double, 5>> s2;
    std::array<double, 5> safe{};
    std::array<double, 5> log_mu{};
};

// The classes of an observation's kind: [0, 2) for a mark, [2, 5) for a space.
constexpr std::size_t first_class(bool is_mark) { return is_mark ? 0 : 2; }
constexpr std::size_t end_class(bool is_mark) { return is_mark ? 2 : 5; }

// log_d: ln of the clamped durations; s2 = sigma_ln^2 + var_t / mu^2 (every class); ll = lp - 0.5 z^2 / s2 -
// 0.5 ln s2 - ln sqrt(2 pi), z = ln d - ln mu, for the classes of the observation's kind, -inf for the other
// kind or a median not positive; total = ln of the sum of e^ll over the kind's classes and the outlier's
// e^log_out, in one pass (lse; Plan B task B2(a), near-exact: the earlier logaddexp chain over all
// five classes then the outlier differs in the last bits).
//
// Evaluating only the kind's classes changes no bit of ll or of the sum: the other kind's -inf terms add
// e^-inf = 0 (and before B2(a)'s near-exact step entered the logaddexp chain only as logaddexp(x, -inf) = x).
Terms terms(const std::array<double, 4>& theta, const std::vector<bool>& is_mark, const std::vector<double>& log_d,
            const std::vector<double>& var_t, const std::array<double, 5>& lp, const std::array<double, 5>& sigma2,
            double log_out) {
    Terms t;
    const std::array<double, 5> mu = medians(theta);
    std::array<bool, 5> valid{};
    for (std::size_t c = 0; c < 5; ++c) {
        valid[c] = mu[c] > 0.0;
        t.safe[c] = valid[c] ? mu[c] : 1.0;
        t.log_mu[c] = std::log(t.safe[c]);
    }
    const std::size_t n = log_d.size();
    t.ll.resize(n);
    t.total.resize(n);
    t.s2.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t c = 0; c < 5; ++c) t.s2[i][c] = sigma2[c] + var_t[i] / (t.safe[c] * t.safe[c]);
        t.ll[i].fill(-kInf);
        const std::size_t c0 = first_class(is_mark[i]);
        for (std::size_t c = c0; c < end_class(is_mark[i]); ++c) {
            const double s2 = t.s2[i][c];
            const double z = log_d[i] - t.log_mu[c];
            double ll = lp[c] - 0.5 * z * z / s2 - 0.5 * std::log(s2) - kLogSqrt2Pi;
            if (!valid[c]) ll = -kInf;
            t.ll[i][c] = ll;
        }
        t.total[i] = lse(t.ll[i].data() + c0, end_class(is_mark[i]) - c0, log_out);
    }
    return t;
}

// Solves the 4 x 4 system a x = b by LU with partial pivoting (LAPACK gesv's method), row-major a.
std::array<double, 4> solve4(std::array<std::array<double, 4>, 4> a, std::array<double, 4> b) {
    for (int k = 0; k < 4; ++k) {
        int p = k;
        for (int i = k + 1; i < 4; ++i)
            if (std::abs(a[i][k]) > std::abs(a[p][k])) p = i;
        std::swap(a[k], a[p]);
        std::swap(b[k], b[p]);
        for (int i = k + 1; i < 4; ++i) {
            const double f = a[i][k] / a[k][k];
            a[i][k] = f;
            for (int j = k + 1; j < 4; ++j) a[i][j] -= f * a[k][j];
            b[i] -= f * b[k];
        }
    }
    std::array<double, 4> x{};
    for (int i = 3; i >= 0; --i) {
        double s = b[i];
        for (int j = i + 1; j < 4; ++j) s -= a[i][j] * x[j];
        x[i] = s / a[i][i];
    }
    return x;
}

}  // namespace

// The read-only grid and constants (the prototype's _grid, _log_priors, _log_outlier and lambda); one per
// configuration, shared (DurationFit::shared_model).
struct DurationFit::Model {
    // One class over the grid: ln median, 1 / median^2, median > 0 (laid out as its table).
    struct Class {
        std::vector<double> log_mu;
        std::vector<double> inv_mu2;
        std::vector<char> valid;
    };
    std::vector<double> t, q, w, g;
    std::size_t nt = 0, nq = 0, nw = 0, ng = 0;
    std::array<Class, 2> marks;   // over (T, q, w)
    std::array<Class, 3> spaces;  // over (T, w, T_g)
    std::array<double, 5> lp{};
    std::array<double, 5> sigma2{};
    double log_out = 0.0;
    double lam = 0.0;
    std::size_t capacity = 0;
    double lo = 0.0, hi = 0.0;
    double sigma_ln_mark = 0.0, sigma_ln_space = 0.0;
    double prior_sigma_ln = 0.0;
    int refine_iterations = 0;

    static void prep(Class& cls, std::size_t i, double mu) {
        const bool valid = mu > 0.0;
        const double safe = valid ? mu : 1.0;
        cls.log_mu[i] = std::log(safe);
        cls.inv_mu2[i] = 1.0 / (safe * safe);
        cls.valid[i] = valid ? 1 : 0;
    }

    explicit Model(const BankConfig& cfg) {
        const double t_min = 1.2 / cfg.max_wpm;
        const double t_max = 1.2 / cfg.min_wpm;
        const int count =
            static_cast<int>(std::ceil(std::log(t_max / t_min) / std::log1p(cfg.t_grid_step))) + 1;
        const double base = 1.0 + cfg.t_grid_step;
        for (int i = 0; i < count; ++i) t.push_back(t_min * std::pow(base, static_cast<double>(i)));
        q = cfg.q_grid;
        w = cfg.w_grid;
        g = cfg.tg_grid;
        nt = t.size();
        nq = q.size();
        nw = w.size();
        ng = g.size();
        const std::size_t nm = nt * nq * nw;
        const std::size_t ns = nt * nw * ng;
        for (auto& c : marks) c = {std::vector<double>(nm), std::vector<double>(nm), std::vector<char>(nm)};
        for (auto& c : spaces) c = {std::vector<double>(ns), std::vector<double>(ns), std::vector<char>(ns)};
        for (std::size_t i = 0; i < nt; ++i) {
            const double T = t[i];
            for (std::size_t k = 0; k < nq; ++k)
                for (std::size_t j = 0; j < nw; ++j) {
                    const std::size_t at = (i * nq + k) * nw + j;
                    prep(marks[0], at, T * (1.0 + w[j]));
                    prep(marks[1], at, T * (q[k] + w[j]));
                }
            for (std::size_t j = 0; j < nw; ++j)
                for (std::size_t l = 0; l < ng; ++l) {
                    const std::size_t at = (i * nw + j) * ng + l;
                    prep(spaces[0], at, T * (1.0 - w[j]));
                    prep(spaces[1], at, T * (3.0 * g[l] - w[j]));
                    prep(spaces[2], at, T * (7.0 * g[l] - w[j]));
                }
        }
        lp = log_priors(cfg);
        sigma2 = class_sigma2(cfg);
        log_out = log_outlier(cfg);
        lam = std::exp(-1.0 / cfg.fit_memory);
        capacity = static_cast<std::size_t>(std::ceil(4.0 * cfg.fit_memory));
        lo = cfg.outlier_range_s.at(0);
        hi = cfg.outlier_range_s.at(1);
        sigma_ln_mark = cfg.sigma_ln_mark;
        sigma_ln_space = cfg.sigma_ln_space;
        prior_sigma_ln = cfg.prior_sigma_ln;
        refine_iterations = cfg.refine_iterations;
    }

    // The T_P prior's log term, -prior_weight (ln T - ln T_P)^2 / (2 sigma_P^2), nats (0 without T_P).
    double prior_term(double t_s, std::optional<double> prior_t_s, double prior_weight) const {
        if (!prior_t_s || !(prior_weight > 0.0)) return 0.0;
        const double z = (std::log(t_s) - std::log(*prior_t_s)) / prior_sigma_ln;
        return -prior_weight * 0.5 * z * z;
    }
};

ClassPriors class_priors() {
    // Per character its dits and dahs, its elements minus one element spaces, a character gap after every
    // character but a word's last ((L - 1) / L per character) and a word gap per word (1 / L), L the mean word
    // length.
    int total = 0;
    int dit_sum = 0;
    int dah_sum = 0;
    for (const auto& cw : kVe3neaCharWeights) {
        const std::string_view code = morse::encode(cw.symbol);
        total += cw.weight;
        dit_sum += cw.weight * static_cast<int>(std::count(code.begin(), code.end(), '.'));
        dah_sum += cw.weight * static_cast<int>(std::count(code.begin(), code.end(), '-'));
    }
    const double dits = static_cast<double>(dit_sum) / total;
    const double dahs = static_cast<double>(dah_sum) / total;
    constexpr std::size_t nl = std::size(kVe3neaWordLengthProbs);
    // Python's sum() of floats (fit.py: sum(VE3NEA_WORD_LENGTH_PROBS)), which is compensated in Python 3.12.
    const double psum = python_sum(kVe3neaWordLengthProbs, nl);
    std::vector<double> lp(nl);
    for (std::size_t i = 0; i < nl; ++i) lp[i] = static_cast<double>(i) * (kVe3neaWordLengthProbs[i] / psum);
    const double mean_len = pairwise_sum(lp);
    const double spaces[3] = {dits + dahs - 1.0, (mean_len - 1.0) / mean_len, 1.0 / mean_len};
    const double ssum = pairwise_sum(spaces, 3);
    ClassPriors p;
    p.marks = {dits / (dits + dahs), dahs / (dits + dahs)};
    p.spaces = {spaces[0] / ssum, spaces[1] / ssum, spaces[2] / ssum};
    return p;
}

double resolution_var_s2(double length_s, double a, double rate_hz) {
    const double r = length_s / std::max(a, 1.0);
    return 2.0 * (r * r) + 2.0 / (12.0 * (rate_hz * rate_hz));
}

double logaddexp(double x, double y) {
    // The prototype's grid uses numpy's own formula by parts, max(x, y) + log1p(exp(-|x - y|)), and falls back
    // to np.logaddexp where that is NaN (both -inf); np.logaddexp gives x + ln 2 where x == y, which the
    // prototype checks equals max + log1p(1) on its machine (BankFit.LogaddexpOfEqualArguments checks it here).
    // The value where |x - y| is so large that it is max(x, y) exactly is returned without exp and log1p (lae).
    return lae(x, y);
}

double log_sum_exp(const double* x, std::size_t n, double out) { return lse(x, n, out); }

ClassLogliks class_logliks(const std::array<double, 4>& theta, const std::vector<Obs>& obs, const BankConfig& cfg) {
    const double lo = cfg.outlier_range_s.at(0);
    const double hi = cfg.outlier_range_s.at(1);
    std::vector<bool> is_mark(obs.size());
    std::vector<double> log_d(obs.size());
    std::vector<double> var_t(obs.size());
    for (std::size_t i = 0; i < obs.size(); ++i) {
        if (!(obs[i].d_s > 0.0)) throw std::invalid_argument("durations must be positive, s");
        is_mark[i] = obs[i].is_mark;
        log_d[i] = std::log(std::min(std::max(obs[i].d_s, lo), hi));
        var_t[i] = obs[i].var_t_s2;
    }
    Terms t = terms(theta, is_mark, log_d, var_t, log_priors(cfg), class_sigma2(cfg), log_outlier(cfg));
    ClassLogliks out;
    out.var_lin.resize(obs.size());
    for (std::size_t i = 0; i < obs.size(); ++i)
        for (std::size_t c = 0; c < 5; ++c) out.var_lin[i][c] = t.s2[i][c] * (t.safe[c] * t.safe[c]);
    out.ll = std::move(t.ll);
    out.total = std::move(t.total);
    return out;
}

double observations_loglik(const Fit& fit, const std::vector<Obs>& obs, const BankConfig& cfg) {
    if (obs.empty()) return -kInf;
    const ClassLogliks c = class_logliks(fit.theta(), obs, cfg);
    return pairwise_sum(c.total) / static_cast<double>(c.total.size());
}

double observations_loglik(const std::optional<Fit>& fit, const std::vector<Obs>& obs, const BankConfig& cfg) {
    if (!fit) return -kInf;
    return observations_loglik(*fit, obs, cfg);
}

bool classify_mark(const Fit& fit, double d, double var_t, const BankConfig& cfg) {
    const ClassLogliks c = class_logliks(fit.theta(), {Obs{true, d, var_t}}, cfg);
    return c.ll[0][1] > c.ll[0][0];
}

std::string classify_space(const Fit& fit, double d, double var_t, const BankConfig& cfg) {
    const ClassLogliks c = class_logliks(fit.theta(), {Obs{false, d, var_t}}, cfg);
    std::size_t best = 0;  // np.argmax: the first maximum
    for (std::size_t k = 1; k < 3; ++k)
        if (c.ll[0][2 + k] > c.ll[0][2 + best]) best = k;
    return kSpaceKinds[best];
}

namespace {

// The exact values of the fields Model reads (min_wpm, max_wpm, t_grid_step, q_grid, w_grid, tg_grid,
// outlier_prior, outlier_range_s, sigma_ln_mark, sigma_ln_space, fit_memory, prior_sigma_ln,
// refine_iterations) as bit patterns, each list preceded by its length: equal keys build bit-identical models.
std::vector<std::uint64_t> model_key(const BankConfig& cfg) {
    std::vector<std::uint64_t> key;
    auto put = [&key](double x) { key.push_back(std::bit_cast<std::uint64_t>(x)); };
    auto put_list = [&key, &put](const std::vector<double>& v) {
        key.push_back(v.size());
        for (double x : v) put(x);
    };
    put(cfg.min_wpm);
    put(cfg.max_wpm);
    put(cfg.t_grid_step);
    put_list(cfg.q_grid);
    put_list(cfg.w_grid);
    put_list(cfg.tg_grid);
    put(cfg.outlier_prior);
    put_list(cfg.outlier_range_s);
    put(cfg.sigma_ln_mark);
    put(cfg.sigma_ln_space);
    put(cfg.fit_memory);
    put(cfg.prior_sigma_ln);
    key.push_back(static_cast<std::uint64_t>(static_cast<std::int64_t>(cfg.refine_iterations)));
    return key;
}

}  // namespace

// One immutable Model per configuration, shared process-wide by every fit built from a configuration with the
// same key; the cache holds weak references, so a Model is freed when no fit uses it (expired entries are
// dropped whenever a Model is added). Models are only read after construction, so fits on several threads may
// share one; the cache itself is guarded by a mutex.
std::shared_ptr<const DurationFit::Model> DurationFit::shared_model(const BankConfig& cfg) {
    static std::mutex mutex;
    static std::map<std::vector<std::uint64_t>, std::weak_ptr<const Model>> cache;
    std::vector<std::uint64_t> key = model_key(cfg);
    const std::lock_guard<std::mutex> lock(mutex);
    const auto it = cache.find(key);
    if (it != cache.end())
        if (auto m = it->second.lock()) return m;
    auto m = std::make_shared<const Model>(cfg);
    std::erase_if(cache, [](const auto& entry) { return entry.second.expired(); });
    cache.insert_or_assign(std::move(key), m);
    return m;
}

DurationFit::DurationFit(const BankConfig& cfg) : m_(shared_model(cfg)) {
    mark_table_.assign(m_->nt * m_->nq * m_->nw, 0.0);
    space_table_.assign(m_->nt * m_->nw * m_->ng, 0.0);
}

double DurationFit::lambda() const { return m_->lam; }
std::size_t DurationFit::history_capacity() const { return m_->capacity; }
const std::vector<double>& DurationFit::t_grid_s() const { return m_->t; }
const std::vector<double>& DurationFit::q_grid() const { return m_->q; }
const std::vector<double>& DurationFit::w_grid() const { return m_->w; }
const std::vector<double>& DurationFit::tg_grid() const { return m_->g; }

std::vector<double> DurationFit::grid_loglik(bool is_mark, double duration_s, double var_t) const {
    // Each class's term, operation by operation in the prototype's order: s2 = var_t / mu^2 + sigma^2;
    // q = 0.5 z z / s2 with z = ln d - ln mu; lp - q - 0.5 ln s2 - ln sqrt(2 pi); -inf where the median is not
    // positive. Classes and the outlier combined in one pass (lse; Plan B task B2(a), near-exact).
    //
    // A class whose term cannot come within kNegligibleNats of the outlier's is left out before its ln s2 is
    // computed: s2 >= sigma^2, so the term is at most a - 0.5 ln sigma^2 - ln sqrt(2 pi), a = lp - q, and where
    // that bound is more than kNegligibleNats + 1 nats below the outlier's log-density (1 nat for rounding) the
    // term would be left out of the sum anyway: the result is the same bit for bit as evaluating it.
    const Model& m = *m_;
    const double log_d = std::log(std::min(std::max(duration_s, m.lo), m.hi));
    const double sigma = is_mark ? m.sigma_ln_mark : m.sigma_ln_space;
    const double sigma2 = sigma * sigma;
    const double cut = m.log_out - (kNegligibleNats + 1.0) + 0.5 * std::log(sigma2) + kLogSqrt2Pi;
    const std::size_t nclass = is_mark ? 2 : 3;
    const std::size_t size = is_mark ? mark_table_.size() : space_table_.size();
    std::vector<double> total(size);
    for (std::size_t at = 0; at < size; ++at) {
        double x[3];
        for (std::size_t c = 0; c < nclass; ++c) {
            const Model::Class& cls = is_mark ? m.marks[c] : m.spaces[c];
            const double lp = m.lp[is_mark ? c : 2 + c];
            double s2 = var_t * cls.inv_mu2[at];
            s2 += sigma2;
            const double z = log_d - cls.log_mu[at];
            double q = 0.5 * z;
            q *= z;
            q /= s2;
            q = lp - q;
            if (!cls.valid[at] || q < cut) {
                x[c] = -kInf;
                continue;
            }
            q -= std::log(s2) * 0.5;
            q -= kLogSqrt2Pi;
            x[c] = q;
        }
        total[at] = lse(x, nclass, m.log_out);
    }
    return total;
}

void DurationFit::add(bool is_mark, double duration_s, double var_t) {
    if (!(duration_s > 0.0)) return;
    const std::vector<double> total = grid_loglik(is_mark, duration_s, var_t);
    const double lam = m_->lam;
    for (double& v : mark_table_) v *= lam;
    for (double& v : space_table_) v *= lam;
    std::vector<double>& table = is_mark ? mark_table_ : space_table_;
    for (std::size_t i = 0; i < table.size(); ++i) table[i] += total[i];
    weight_ = lam * weight_ + 1.0;
    history_.push_front(Obs{is_mark, duration_s, var_t});
    if (history_.size() > m_->capacity) history_.pop_back();
}

std::optional<std::array<int, 4>> DurationFit::grid_index(std::optional<double> prior_t_s, double prior_weight) const {
    if (history_.empty()) return std::nullopt;
    const Model& m = *m_;
    const bool use_prior = prior_t_s.has_value() && prior_weight > 0.0;
    std::size_t bi = 0, bj = 0;
    double best = 0.0;
    bool first = true;
    for (std::size_t i = 0; i < m.nt; ++i) {
        double pen = 0.0;
        if (use_prior) {
            const double z = (std::log(m.t[i]) - std::log(*prior_t_s)) / m.prior_sigma_ln;
            pen = prior_weight * 0.5 * z * z;
        }
        for (std::size_t j = 0; j < m.nw; ++j) {
            double mmax = mark_table_[(i * m.nq) * m.nw + j];
            for (std::size_t k = 1; k < m.nq; ++k) mmax = std::max(mmax, mark_table_[(i * m.nq + k) * m.nw + j]);
            double smax = space_table_[(i * m.nw + j) * m.ng];
            for (std::size_t l = 1; l < m.ng; ++l) smax = std::max(smax, space_table_[(i * m.nw + j) * m.ng + l]);
            double score = mmax + smax;
            if (use_prior) score = score - pen;
            if (first || score > best) {
                best = score;
                bi = i;
                bj = j;
                first = false;
            }
        }
    }
    std::size_t qi = 0;
    for (std::size_t k = 1; k < m.nq; ++k)
        if (mark_table_[(bi * m.nq + k) * m.nw + bj] > mark_table_[(bi * m.nq + qi) * m.nw + bj]) qi = k;
    std::size_t gi = 0;
    for (std::size_t l = 1; l < m.ng; ++l)
        if (space_table_[(bi * m.nw + bj) * m.ng + l] > space_table_[(bi * m.nw + bj) * m.ng + gi]) gi = l;
    return std::array<int, 4>{static_cast<int>(bi), static_cast<int>(bj), static_cast<int>(qi),
                              static_cast<int>(gi)};
}

std::optional<std::array<double, 4>> DurationFit::grid_theta(std::optional<double> prior_t_s,
                                                             double prior_weight) const {
    const auto idx = grid_index(prior_t_s, prior_weight);
    if (!idx) return std::nullopt;
    const Model& m = *m_;
    const double t = m.t[static_cast<std::size_t>((*idx)[0])];
    return std::array<double, 4>{t, m.w[static_cast<std::size_t>((*idx)[1])] * t,
                                 m.q[static_cast<std::size_t>((*idx)[2])] * t,
                                 m.g[static_cast<std::size_t>((*idx)[3])] * t};
}

namespace {

// The retained history as arrays, newest first, with what every scoring of it shares: the clamped
// durations' logarithms and the ages' weights lambda^age (and their sum).
struct Retained {
    std::vector<bool> is_mark;
    std::vector<double> log_d;
    std::vector<double> var_t;
    std::vector<double> age;
    double age_sum = 0.0;
};

Retained retained(const std::deque<Obs>& history, double lam, double lo, double hi) {
    Retained r;
    for (std::size_t i = 0; i < history.size(); ++i) {
        const Obs& o = history[i];
        r.is_mark.push_back(o.is_mark);
        r.log_d.push_back(std::log(std::min(std::max(o.d_s, lo), hi)));
        r.var_t.push_back(o.var_t_s2);
        r.age.push_back(std::pow(lam, static_cast<double>(i)));
    }
    r.age_sum = pairwise_sum(r.age);
    return r;
}

// sum over the history of lambda^age x total (numpy's pairwise order), nats.
double aged_sum(const Retained& r, const std::vector<double>& total) {
    std::vector<double> prod(total.size());
    for (std::size_t i = 0; i < total.size(); ++i) prod[i] = r.age[i] * total[i];
    return pairwise_sum(prod);
}

}  // namespace

double DurationFit::weighted_loglik(const std::array<double, 4>& theta, std::optional<double> prior_t_s,
                                    double prior_weight) const {
    const Model& m = *m_;
    const Retained r = retained(history_, m.lam, m.lo, m.hi);
    const Terms t = terms(theta, r.is_mark, r.log_d, r.var_t, m.lp, m.sigma2, m.log_out);
    return aged_sum(r, t.total) + m.prior_term(theta[0], prior_t_s, prior_weight);
}

namespace {

// refine's result with the terms it evaluated at its start and at its result (one evaluation each; the same
// object when there are no steps).
struct Refinement {
    std::array<double, 4> theta{};
    Terms start;
    Terms last;  // empty when there are no steps: the start's terms are the result's
};

Refinement refine_on(const DurationFit::Model& m, const Retained& r, const std::array<double, 4>& start,
                     std::optional<double> prior_t_s, double prior_weight) {
    // Gauss-Newton in ln d on the model's medians, EM-style (the class responsibilities of the current point
    // held fixed per step): residual ln d - ln mu_c, Jacobian DESIGN_c / mu_c, weights lambda^age x
    // responsibility / s_c^2; the T_P prior as one more residual ln T_P - ln T with weight prior_weight /
    // sigma_P^2; damping toward the current point with standard deviation 0.2 T per parameter. After each
    // step: T floored at 0.1 ms, w clipped to [-0.6, 1.2] T, qT to [2, 6] T, T_g to [0.8, 10] T.
    //
    // Only the classes of each observation's kind are summed. For a finite theta (every call from best: grid
    // points are finite and the clipped steps keep each median <= 0, then 1 is used, or >= about 1e-20 s) the
    // other kind's responsibilities are exp(-inf - total) = 0 and the Jacobian and residuals are finite, so
    // their weights and every product of them are +0 or -0, and adding a zero leaves every sum unchanged (the
    // sums start at +0 and a sum is -0 only if both addends are -0, so no sum is ever -0; NaN stays NaN). With
    // a non-finite start through the public refine (e.g. qT = +inf on a history of spaces only) the code before
    // B2(a) could get NaN from 0 x inf where this one does not.
    Refinement out;
    out.theta = start;
    out.start = terms(start, r.is_mark, r.log_d, r.var_t, m.lp, m.sigma2, m.log_out);
    const Terms* t = &out.start;
    const std::size_t n = r.log_d.size();
    for (int it = 0; it < m.refine_iterations; ++it) {
        std::array<double, 4>& theta = out.theta;
        double jac[5][4];
        for (std::size_t c = 0; c < 5; ++c)
            for (std::size_t i = 0; i < 4; ++i) jac[c][i] = kDesign[c][i] / t->safe[c];
        std::array<std::array<double, 4>, 4> mm{};
        std::array<double, 4> b{};
        for (std::size_t o = 0; o < n; ++o) {
            for (std::size_t c = first_class(r.is_mark[o]); c < end_class(r.is_mark[o]); ++c) {
                const double wgt = r.age[o] * std::exp(t->ll[o][c] - t->total[o]) / t->s2[o][c];
                const double resid = r.log_d[o] - t->log_mu[c];
                for (std::size_t i = 0; i < 4; ++i) {
                    const double wj = wgt * jac[c][i];
                    for (std::size_t j = 0; j < 4; ++j) mm[i][j] += wj * jac[c][j];
                    b[i] += wj * resid;
                }
            }
        }
        if (prior_t_s && prior_weight > 0.0) {
            const double wp = prior_weight / (m.prior_sigma_ln * m.prior_sigma_ln);
            mm[0][0] += wp / (theta[0] * theta[0]);
            b[0] += wp * (std::log(*prior_t_s) - std::log(theta[0])) / theta[0];
        }
        const double sd = 0.2 * theta[0];
        const double damping = 1.0 / (sd * sd);
        for (std::size_t i = 0; i < 4; ++i) mm[i][i] += damping;
        const std::array<double, 4> step = solve4(mm, b);
        std::array<double, 4> next{};
        for (std::size_t i = 0; i < 4; ++i) next[i] = theta[i] + step[i];
        const double tt = std::max(next[0], 1e-4);
        theta = {tt, std::min(std::max(next[1], -0.6 * tt), 1.2 * tt), std::min(std::max(next[2], 2.0 * tt), 6.0 * tt),
                 std::min(std::max(next[3], 0.8 * tt), 10.0 * tt)};
        out.last = terms(theta, r.is_mark, r.log_d, r.var_t, m.lp, m.sigma2, m.log_out);
        t = &out.last;
    }
    return out;
}

}  // namespace

std::array<double, 4> DurationFit::refine(const std::array<double, 4>& start, std::optional<double> prior_t_s,
                                          double prior_weight) const {
    const Model& m = *m_;
    return refine_on(m, retained(history_, m.lam, m.lo, m.hi), start, prior_t_s, prior_weight).theta;
}

std::optional<Fit> DurationFit::best(std::optional<double> prior_t_s, double prior_weight) const {
    // The grid point's and the refined point's weighted log-likelihoods come from the terms the refinement
    // evaluated at its start and at its result (the same values the plain evaluation gives: terms is a
    // function of the point and the retained history only).
    const auto grid = grid_theta(prior_t_s, prior_weight);
    if (!grid) return std::nullopt;
    const Model& m = *m_;
    const Retained r = retained(history_, m.lam, m.lo, m.hi);
    const Refinement ref = refine_on(m, r, *grid, prior_t_s, prior_weight);
    const std::array<double, 4>& refined = ref.theta;
    const double grid_sum = aged_sum(r, ref.start.total);
    const double refined_sum = m.refine_iterations > 0 ? aged_sum(r, ref.last.total) : grid_sum;
    std::array<double, 4> theta = *grid;
    double total_sum = grid_sum;
    if (refined_sum + m.prior_term(refined[0], prior_t_s, prior_weight) >=
        grid_sum + m.prior_term((*grid)[0], prior_t_s, prior_weight)) {
        theta = refined;
        total_sum = refined_sum;
    }
    return Fit{theta[0], theta[2] / theta[0], theta[1], theta[3], total_sum / r.age_sum, weight_};
}

}  // namespace kz4ap::bank
