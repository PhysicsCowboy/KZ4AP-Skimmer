#include "kz4ap/bank/fit.hpp"

#include "kz4ap/morse.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
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

// VE3NEA's CW character frequencies and word-length probabilities (kz4ap_synth.messages, MIT, from DeepCW),
// in the prototype's order.
struct CharWeight {
    std::string_view symbol;
    int weight;
};
constexpr CharWeight kVe3neaCharWeights[] = {
    {"1", 13},  {"2", 14},  {"3", 33},  {"4", 43},  {"5", 41},  {"6", 8},   {"7", 14},  {"8", 10},  {"9", 14},
    {"0", 11},  {"A", 127}, {"B", 62},  {"C", 69},  {"D", 84},  {"E", 321}, {"F", 55},  {"G", 43},  {"H", 68},
    {"I", 130}, {"J", 8},   {"K", 117}, {"L", 100}, {"M", 76},  {"N", 168}, {"O", 126}, {"P", 57},  {"Q", 68},
    {"R", 95},  {"S", 159}, {"T", 236}, {"U", 61},  {"V", 23},  {"W", 95},  {"X", 16},  {"Y", 40},  {"Z", 12},
    {"/", 19},  {".", 12},  {",", 9},   {"?", 16},  {"<BT>", 15},
};
constexpr double kVe3neaWordLengthProbs[] = {0.0,   0.1672, 0.2569, 0.1939, 0.1745, 0.0921, 0.025, 0.008, 0.006,
                                             0.004, 0.003,  0.003,  0.002,  0.002,  0.002,  0.001, 0.001};

// numpy's pairwise summation of a contiguous float64 array (np.sum): under 8 values a plain loop; up to 128,
// eight interleaved partial sums combined pairwise, then the remainder; above, the halves (split at a
// multiple of 8) summed recursively. The same sum in numpy's order of additions (as keying.cpp's).
double pairwise_sum(const double* a, std::size_t n) {
    if (n < 8) {
        double res = 0.0;
        for (std::size_t i = 0; i < n; ++i) res += a[i];
        return res;
    }
    if (n <= 128) {
        double r[8];
        for (std::size_t j = 0; j < 8; ++j) r[j] = a[j];
        std::size_t i = 8;
        for (; i < n - (n % 8); i += 8)
            for (std::size_t j = 0; j < 8; ++j) r[j] += a[i + j];
        double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; ++i) res += a[i];
        return res;
    }
    std::size_t n2 = n / 2;
    n2 -= n2 % 8;
    return pairwise_sum(a, n2) + pairwise_sum(a + n2, n - n2);
}

double pairwise_sum(const std::vector<double>& a) { return pairwise_sum(a.data(), a.size()); }

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

// What the prototype's _terms / _class_terms compute for each observation: ll (n, 5), total (n), s2 (n, 5),
// and per class the median (1 where not positive) and its log.
struct Terms {
    std::vector<std::array<double, 5>> ll;
    std::vector<double> total;
    std::vector<std::array<double, 5>> s2;
    std::array<double, 5> safe{};
    std::array<double, 5> log_mu{};
};

// log_d: ln of the clamped durations; s2 = sigma_ln^2 + var_t / mu^2; ll = lp - 0.5 z^2 / s2 - 0.5 ln s2 -
// ln sqrt(2 pi), z = ln d - ln mu, -inf for the other kind or a median not positive; total = the classes
// combined by logaddexp in class order, then with the outlier.
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
        for (std::size_t c = 0; c < 5; ++c) {
            const double s2 = sigma2[c] + var_t[i] / (t.safe[c] * t.safe[c]);
            const double z = log_d[i] - t.log_mu[c];
            double ll = lp[c] - 0.5 * z * z / s2 - 0.5 * std::log(s2) - kLogSqrt2Pi;
            if (!(kIsMarkClass[c] == is_mark[i] && valid[c])) ll = -kInf;
            t.ll[i][c] = ll;
            t.s2[i][c] = s2;
        }
        double acc = t.ll[i][0];
        for (std::size_t c = 1; c < 5; ++c) acc = logaddexp(acc, t.ll[i][c]);
        t.total[i] = logaddexp(acc, log_out);
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

// The read-only grid and constants (the prototype's _grid, _log_priors, _log_outlier and lambda).
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
    double psum = 0.0;  // Python's sum(): left to right
    for (double v : kVe3neaWordLengthProbs) psum += v;
    constexpr std::size_t nl = std::size(kVe3neaWordLengthProbs);
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
    if (x == y) return x + kLogE2;
    return std::max(x, y) + std::log1p(std::exp(-std::abs(x - y)));
}

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

DurationFit::DurationFit(const BankConfig& cfg) : m_(std::make_shared<const Model>(cfg)) {
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
    // positive. Classes combined by logaddexp in class order, then the outlier.
    const Model& m = *m_;
    const double log_d = std::log(std::min(std::max(duration_s, m.lo), m.hi));
    const double sigma = is_mark ? m.sigma_ln_mark : m.sigma_ln_space;
    const double sigma2 = sigma * sigma;
    const std::size_t nclass = is_mark ? 2 : 3;
    const std::size_t size = is_mark ? mark_table_.size() : space_table_.size();
    std::vector<double> total(size);
    for (std::size_t at = 0; at < size; ++at) {
        double acc = 0.0;
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
            q -= std::log(s2) * 0.5;
            q -= kLogSqrt2Pi;
            if (!cls.valid[at]) q = -kInf;
            acc = c == 0 ? q : logaddexp(acc, q);
        }
        total[at] = logaddexp(acc, m.log_out);
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

std::array<double, 4> DurationFit::refine(const std::array<double, 4>& start, std::optional<double> prior_t_s,
                                          double prior_weight) const {
    // Gauss-Newton in ln d on the model's medians, EM-style (the class responsibilities of the current point
    // held fixed per step): residual ln d - ln mu_c, Jacobian DESIGN_c / mu_c, weights lambda^age x
    // responsibility / s_c^2; the T_P prior as one more residual ln T_P - ln T with weight prior_weight /
    // sigma_P^2; damping toward the current point with standard deviation 0.2 T per parameter. After each
    // step: T floored at 0.1 ms, w clipped to [-0.6, 1.2] T, qT to [2, 6] T, T_g to [0.8, 10] T.
    const Model& m = *m_;
    const Retained r = retained(history_, m.lam, m.lo, m.hi);
    std::array<double, 4> theta = start;
    Terms t = terms(theta, r.is_mark, r.log_d, r.var_t, m.lp, m.sigma2, m.log_out);
    const std::size_t n = r.log_d.size();
    for (int it = 0; it < m.refine_iterations; ++it) {
        double jac[5][4];
        for (std::size_t c = 0; c < 5; ++c)
            for (std::size_t i = 0; i < 4; ++i) jac[c][i] = kDesign[c][i] / t.safe[c];
        std::array<std::array<double, 4>, 4> mm{};
        std::array<double, 4> b{};
        for (std::size_t o = 0; o < n; ++o) {
            for (std::size_t c = 0; c < 5; ++c) {
                const double wgt = r.age[o] * std::exp(t.ll[o][c] - t.total[o]) / t.s2[o][c];
                const double resid = r.log_d[o] - t.log_mu[c];
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
        t = terms(theta, r.is_mark, r.log_d, r.var_t, m.lp, m.sigma2, m.log_out);
    }
    return theta;
}

std::optional<Fit> DurationFit::best(std::optional<double> prior_t_s, double prior_weight) const {
    const auto grid = grid_theta(prior_t_s, prior_weight);
    if (!grid) return std::nullopt;
    const Model& m = *m_;
    const Retained r = retained(history_, m.lam, m.lo, m.hi);
    const std::array<double, 4> refined = refine(*grid, prior_t_s, prior_weight);
    const double refined_sum = aged_sum(r, terms(refined, r.is_mark, r.log_d, r.var_t, m.lp, m.sigma2, m.log_out).total);
    const double grid_sum = aged_sum(r, terms(*grid, r.is_mark, r.log_d, r.var_t, m.lp, m.sigma2, m.log_out).total);
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
