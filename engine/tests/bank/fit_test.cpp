// The bank decoder's duration fit against the prototype (training/kz4ap_proto/fit.py).
// 1. Golden values (engine/tests/data/bank/fit.json, from golden.py golden_fit): 300 observations added one
//    by one; after observations 1, 2, 8, 48, 100 and 300 the grid indices exactly, grid_theta, best, the
//    weighted log-likelihoods and observations_loglik to relative 1e-9 (floor 1e-12), the classifications
//    of 20 probe durations exactly; after 8, 48 and 300 also the full mark and space tables.
// 2. The Python fit tests (training/tests/test_proto_fit.py), one C++ test per Python test with the same
//    inputs (engine/tests/data/bank/fit_cases.json, golden.py golden_fit_cases: the synthesis library is
//    Python only) and the same assertions; the strict xfail is a GTEST_SKIP with the same reason.
#include "kz4ap/bank/fit.hpp"
#include "kz4ap/bank/numpy_sum.hpp"

#include "golden.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace kz4ap::bank;
using kz4ap::test::expect_close;
using kz4ap::test::load_golden;

using Durations = std::vector<std::pair<bool, double>>;

constexpr double kInf = std::numeric_limits<double>::infinity();

// pytest.approx(expected, rel=rel): |actual - expected| <= rel |expected|.
void expect_rel(double actual, double expected, double rel) {
    EXPECT_NEAR(actual, expected, rel * std::abs(expected));
}

std::uint64_t bits(double x) {
    std::uint64_t b;
    std::memcpy(&b, &x, sizeof b);
    return b;
}

const nlohmann::json& cases() {
    static const nlohmann::json g = load_golden("fit_cases");
    return g;
}

Durations load_case(const std::string& name) {
    const auto mark = cases().at(name + "_mark").get<std::string>();
    const auto d = cases().at(name + "_d_s").get<std::vector<double>>();
    if (mark.size() != d.size()) throw std::runtime_error("fit_cases: " + name + " sizes differ");
    Durations out;
    for (std::size_t i = 0; i < d.size(); ++i) out.emplace_back(mark[i] == '1', d[i]);
    return out;
}

Durations concat(Durations a, const Durations& b, std::size_t take) {
    a.insert(a.end(), b.begin(), b.begin() + static_cast<std::ptrdiff_t>(std::min(take, b.size())));
    return a;
}

// test_proto_fit.fitted: every observation added with sigma_t^2 = var_t, then best.
std::optional<Fit> fitted(const Durations& obs, std::optional<double> prior_t = std::nullopt, double prior_w = 0.0,
                          double var_t = 1e-8, const BankConfig& cfg = BankConfig{}) {
    DurationFit fit(cfg);
    for (const auto& [m, d] : obs) fit.add(m, d, var_t);
    return fit.best(prior_t, prior_w);
}

std::array<double, 4> arr4(const nlohmann::json& j) {
    const auto v = j.get<std::vector<double>>();
    return {v.at(0), v.at(1), v.at(2), v.at(3)};
}

// ---------------------------------------------------------------------------------------------------------
// Golden values

TEST(BankFit, LogaddexpOfEqualArguments) {
    // The prototype's grid computes logaddexp by parts, max + log1p(exp(-|x - y|)), which for x == y is
    // x + log1p(1); np.logaddexp gives x + ln 2. The port uses the latter; they agree when log1p(1) is the
    // double nearest ln 2, as the prototype checks at import.
    EXPECT_EQ(bits(std::log1p(1.0)), bits(0.693147180559945309417232121458176568));
    EXPECT_EQ(bits(logaddexp(-5.2, -5.2)), bits(-5.2 + std::log1p(1.0)));
    EXPECT_EQ(logaddexp(-kInf, -kInf), -kInf);
    EXPECT_EQ(logaddexp(3.0, -kInf), 3.0);
    EXPECT_EQ(logaddexp(-kInf, 2.0), 2.0);
    EXPECT_EQ(bits(logaddexp(-1.0, -3.0)), bits(-1.0 + std::log1p(std::exp(-2.0))));
}

TEST(BankFit, ClassPriorsAndGridMatchPrototype) {
    const auto g = load_golden("fit");
    const ClassPriors p = class_priors();
    const auto marks = g.at("class_priors_marks").get<std::vector<double>>();
    const auto spaces = g.at("class_priors_spaces").get<std::vector<double>>();
    for (std::size_t i = 0; i < 2; ++i) expect_close(p.marks[i], marks.at(i));
    for (std::size_t i = 0; i < 3; ++i) expect_close(p.spaces[i], spaces.at(i));
    const DurationFit fit{BankConfig{}};
    const auto t = g.at("t_grid_s").get<std::vector<double>>();
    ASSERT_EQ(fit.t_grid_s().size(), t.size());
    for (std::size_t i = 0; i < t.size(); ++i) expect_close(fit.t_grid_s()[i], t[i]);
    EXPECT_EQ(fit.q_grid(), g.at("q_grid").get<std::vector<double>>());
    EXPECT_EQ(fit.w_grid(), g.at("w_grid").get<std::vector<double>>());
    EXPECT_EQ(fit.tg_grid(), g.at("tg_grid").get<std::vector<double>>());
    expect_close(fit.lambda(), g.at("lambda").get<double>());
    EXPECT_EQ(fit.history_capacity(), g.at("history_capacity").get<std::size_t>());
    EXPECT_EQ(fit.mark_table().size(), t.size() * 3 * 4);
    EXPECT_EQ(fit.space_table().size(), t.size() * 4 * 5);
}

TEST(BankFit, SnapshotsMatchPrototype) {
    const auto g = load_golden("fit");
    const BankConfig cfg;
    const auto mark = g.at("obs_mark").get<std::string>();
    const auto d = g.at("obs_d_s").get<std::vector<double>>();
    const auto v = g.at("obs_var_t_s2").get<std::vector<double>>();
    const auto probes = g.at("probes_s").get<std::vector<double>>();
    std::vector<Obs> seq;
    for (std::size_t i = 0; i < d.size(); ++i) seq.push_back({mark.at(i) == '1', d[i], v[i]});
    DurationFit fit(cfg);
    std::size_t next = 0;
    const auto& snaps = g.at("snapshots");
    int tables_checked = 0;
    for (std::size_t n = 1; n <= seq.size(); ++n) {
        fit.add(seq[n - 1].is_mark, seq[n - 1].d_s, seq[n - 1].var_t_s2);
        if (next >= snaps.size() || snaps[next].at("n").get<std::size_t>() != n) continue;
        const auto& s = snaps[next++];
        SCOPED_TRACE("after observation " + std::to_string(n));
        expect_close(fit.weight(), s.at("weight").get<double>());
        EXPECT_EQ(fit.history().size(), s.at("history").get<std::size_t>());
        const std::vector<Obs> last(seq.begin() + static_cast<std::ptrdiff_t>(n >= 20 ? n - 20 : 0),
                                    seq.begin() + static_cast<std::ptrdiff_t>(n));
        for (const std::string name : {"none", "prior"}) {
            SCOPED_TRACE(name);
            const std::optional<double> pt = name == "prior" ? std::optional<double>(0.05) : std::nullopt;
            const double pw = name == "prior" ? 3.0 : 0.0;
            const auto idx = fit.grid_index(pt, pw);
            ASSERT_TRUE(idx.has_value());
            const auto want_idx = s.at("grid_index_" + name).get<std::vector<int>>();
            EXPECT_EQ(std::vector<int>(idx->begin(), idx->end()), want_idx);
            const auto th = fit.grid_theta(pt, pw);
            const auto want_th = arr4(s.at("grid_theta_" + name));
            for (std::size_t i = 0; i < 4; ++i) expect_close((*th)[i], want_th[i]);
            expect_close(fit.weighted_loglik(*th, pt, pw), s.at("grid_loglik_" + name).get<double>());
            const auto b = fit.best(pt, pw);
            ASSERT_TRUE(b.has_value());
            const auto want_b = s.at("best_" + name).get<std::vector<double>>();
            expect_close(b->t_s, want_b.at(0));
            expect_close(b->q, want_b.at(1));
            expect_close(b->w_s, want_b.at(2));
            expect_close(b->tg_s, want_b.at(3));
            expect_close(b->quality, want_b.at(4));
            expect_close(b->weight, want_b.at(5));
            expect_close(fit.weighted_loglik(b->theta(), pt, pw), s.at("best_loglik_" + name).get<double>());
            const auto want_m = s.at("classify_mark_" + name).get<std::vector<int>>();
            const auto want_s = s.at("classify_space_" + name).get<std::vector<std::string>>();
            for (std::size_t i = 0; i < probes.size(); ++i) {
                EXPECT_EQ(classify_mark(*b, probes[i], 1e-6, cfg) ? 1 : 0, want_m.at(i)) << "probe " << probes[i];
                EXPECT_EQ(classify_space(*b, probes[i], 1e-6, cfg), want_s.at(i)) << "probe " << probes[i];
            }
            expect_close(observations_loglik(*b, last, cfg), s.at("observations_loglik_" + name).get<double>());
        }
        if (s.contains("mark_table")) {
            ++tables_checked;
            const auto mt = s.at("mark_table").get<std::vector<double>>();
            const auto st = s.at("space_table").get<std::vector<double>>();
            ASSERT_EQ(fit.mark_table().size(), mt.size());
            ASSERT_EQ(fit.space_table().size(), st.size());
            for (std::size_t i = 0; i < mt.size(); ++i) expect_close(fit.mark_table()[i], mt[i]);
            for (std::size_t i = 0; i < st.size(); ++i) expect_close(fit.space_table()[i], st[i]);
        }
    }
    EXPECT_EQ(next, snaps.size());
    EXPECT_EQ(tables_checked, 3);
}

// ---------------------------------------------------------------------------------------------------------
// training/tests/test_proto_fit.py, one test per Python test

TEST(BankFitPython, ClassPriorsFollowVe3neaStatistics) {
    const ClassPriors p = class_priors();
    EXPECT_NEAR(p.marks[0], 0.5716, 1e-4);
    EXPECT_NEAR(p.marks[1], 0.4284, 1e-4);
    EXPECT_NEAR(p.spaces[0], 0.6467, 1e-4);
    EXPECT_NEAR(p.spaces[1], 0.2379, 1e-4);
    EXPECT_NEAR(p.spaces[2], 0.1154, 1e-4);
}

TEST(BankFitPython, ResolutionVarianceIsTwoEdgesOfLOverAPlusSampling) {
    const double want = 2 * 0.004 * 0.004 + 2 / (12 * 1500.0 * 1500.0);
    EXPECT_NEAR(resolution_var_s2(0.04, 10.0, 1500.0), want, 1e-6 * want);  // pytest.approx's default rel 1e-6
    EXPECT_EQ(resolution_var_s2(0.04, 0.1, 1500.0), resolution_var_s2(0.04, 1.0, 1500.0));  // a floored at 1
}

TEST(BankFitPython, FitsMachineKeying) {
    const auto f = fitted(load_case("machine"));
    ASSERT_TRUE(f.has_value());
    expect_rel(f->t_s, 0.048, 0.02);
    EXPECT_NEAR(f->q, 3.0, 0.1);
    EXPECT_LT(std::abs(f->w_s), 0.05 * 0.048);
    expect_rel(f->tg_s, 0.048, 0.05);
}

TEST(BankFitPython, FitsFarnsworthSpacing) {
    GTEST_SKIP() << "Finding (Task 14, E5): with the coarse grids adopted by E5 (T_g/T in {1, 1.59, 2.52, 4, 6.35}) "
                    "the fit gives T = 66.72 ms (true 66.67 ms) but T_g = 192.2 ms against the true 207.0 ms "
                    "(3.11 T), 7.2% low; the default grid before E5 (with 3.17) was within 5%. Cause not traced "
                    "beyond that (refinement starts from a grid point).";
    const auto f = fitted(load_case("farnsworth"));
    ASSERT_TRUE(f.has_value());
    expect_rel(f->t_s, 1.2 / 18.0, 0.02);
    expect_rel(f->tg_s, cases().at("farnsworth_gap_s").get<double>(), 0.05);  // 207 ms, 3.11 T
}

TEST(BankFitPython, FitsFarnsworthSpacingOnThePreE5Grids) {
    // Keeps the T_g refinement covered beside the skipped test above (Task 14 review M9): the grids before E5.
    BankConfig cfg;
    cfg.q_grid = {3.0, 3.5, 4.0, 4.5, 5.0};
    cfg.w_grid = {-0.4, -0.2, 0.0, 0.2, 0.4, 0.6, 0.8, 1.0};
    cfg.tg_grid = {1.0, 1.26, 1.59, 2.0, 2.52, 3.17, 4.0, 5.04, 6.35, 8.0};
    const auto f = fitted(load_case("farnsworth"), std::nullopt, 0.0, 1e-8, cfg);
    ASSERT_TRUE(f.has_value());
    expect_rel(f->t_s, 1.2 / 18.0, 0.02);
    expect_rel(f->tg_s, cases().at("farnsworth_gap_s").get<double>(), 0.05);  // 207 ms, 3.11 T
}

TEST(BankFitPython, FitsKeyWeighting) {
    const auto f = fitted(load_case("key_weighting"));
    ASSERT_TRUE(f.has_value());
    EXPECT_NEAR(f->w_s, 0.2 * 0.048, 0.002);
    expect_rel(f->t_s, 0.048, 0.03);
}

TEST(BankFitPython, FitsHeavyDahs) {
    // HandKey: dah median e^1.5 = 4.48 dits with sigma_ln 0.3 (its mean is 4.69 dits).
    const auto f = fitted(load_case("heavy_dahs"));
    ASSERT_TRUE(f.has_value());
    EXPECT_GE(f->q, 4.0);
    EXPECT_LE(f->q, 5.2);
    expect_rel(f->t_s, 0.05, 0.06);
}

TEST(BankFitPython, SlowFirstDitsAreNotReadAsDahs) {
    // Regression R2 (milestone-2a results 3.8): a 12 WPM station's first marks are dits.
    const auto f = fitted(load_case("hi"));
    ASSERT_TRUE(f.has_value());
    expect_rel(f->t_s, 0.1, 0.1);
}

TEST(BankFitPython, TheFitFollowsASpeedStepWithinItsMemory) {
    // Within three memory lengths (3 x N_mem marks and spaces; 144 at N_mem = 48, E4).
    const auto n = static_cast<std::size_t>(std::lround(3 * BankConfig{}.fit_memory));
    const auto f = fitted(concat(load_case("speed_before"), load_case("speed_after"), n));
    ASSERT_TRUE(f.has_value());
    expect_rel(f->t_s, 1.2 / 35.0, 0.05);
}

TEST(BankFitPython, ATuneUpCarrierIsAnOutlier) {
    const Durations obs = load_case("machine");
    Durations with(obs.begin(), obs.begin() + 40);
    with.insert(with.end(), {{false, 0.5}, {true, 2.0}, {false, 0.5}});
    with.insert(with.end(), obs.begin() + 40, obs.end());
    const auto f = fitted(with);
    ASSERT_TRUE(f.has_value());
    expect_rel(f->t_s, 0.048, 0.02);
}

TEST(BankFitPython, ThePeriodicityPriorDecidesAnAmbiguousStart) {
    const Durations two_marks = {{true, 0.1}, {true, 0.1}};  // dits of 12 WPM, or dahs of 36 WPM
    expect_rel(fitted(two_marks, 0.1, 1.0)->t_s, 0.1, 0.1);
    expect_rel(fitted(two_marks, 1.2 / 36, 1.0)->t_s, 1.2 / 36, 0.1);
}

TEST(BankFitPython, QualityIsHigherForMorseThanForRandomDurations) {
    EXPECT_GT(fitted(load_case("machine"))->quality, fitted(load_case("random"))->quality + 1.0);
}

TEST(BankFitPython, ClassificationFollowsTheFit) {
    const BankConfig cfg;
    const Fit f = *fitted(load_case("machine"));
    EXPECT_FALSE(classify_mark(f, 0.048, 1e-8, cfg));
    EXPECT_TRUE(classify_mark(f, 0.144, 1e-8, cfg));
    EXPECT_EQ(classify_space(f, 0.048, 1e-8, cfg), "element");
    EXPECT_EQ(classify_space(f, 0.144, 1e-8, cfg), "character");
    EXPECT_EQ(classify_space(f, 0.336, 1e-8, cfg), "word");
    const std::vector<Obs> obs = {{true, 0.048, 1e-8}, {false, 0.144, 1e-8}};
    EXPECT_GT(observations_loglik(f, obs, cfg), observations_loglik(Fit{0.1, 3.0, 0.0, 0.1, 0.0, 1.0}, obs, cfg));
}

TEST(BankFitPython, TheFitIsUnbiasedOnModelMatchedDurations) {
    // N_mem = 1000 and about 3400 observations per seed; bounds 1% for T and 0.009 T for w (the Python test's
    // derivation: sd(ln T) = sd(w / T) = 0.0030 for the mean of 4 seeds).
    BankConfig cfg;
    cfg.fit_memory = 1000.0;
    double t_err = 0.0;
    double w_rel = 0.0;
    for (int seed = 1; seed <= 4; ++seed) {
        const auto f = fitted(load_case("model200_" + std::to_string(seed)), std::nullopt, 0.0, 1e-8, cfg);
        ASSERT_TRUE(f.has_value());
        t_err += f->t_s / 0.048 - 1.0;
        w_rel += f->w_s / 0.048;
    }
    EXPECT_LT(std::abs(t_err / 4.0), 0.01);
    EXPECT_LT(std::abs(w_rel / 4.0), 0.009);
}

TEST(BankFitPython, RefinementNeverLowersTheWeightedLoglik) {
    struct Case {
        Durations obs;
        std::optional<double> prior_t;
        double prior_w;
    };
    std::vector<Case> all = {
        {load_case("machine"), std::nullopt, 0.0},
        {load_case("heavy_dahs"), std::nullopt, 0.0},
        {load_case("hi"), std::nullopt, 0.0},
        {{{true, 0.1}, {true, 0.1}}, 1.2 / 36, 1.0},
        {concat(load_case("speed_before"), load_case("speed_after"), 36), 0.04, 1.0},
        {load_case("random"), std::nullopt, 0.0},
    };
    for (int seed = 1; seed <= 3; ++seed) all.push_back({load_case("model30_" + std::to_string(seed)), std::nullopt, 0.0});
    for (const auto& c : all) {
        DurationFit fit{BankConfig{}};
        for (const auto& [m, d] : c.obs) fit.add(m, d, 1e-8);
        const auto best = fit.best(c.prior_t, c.prior_w);
        ASSERT_TRUE(best.has_value());
        EXPECT_GE(fit.weighted_loglik(best->theta(), c.prior_t, c.prior_w),
                  fit.weighted_loglik(*fit.grid_theta(c.prior_t, c.prior_w), c.prior_t, c.prior_w) - 1e-9);
    }
}

TEST(BankFitPython, DurationsMustBePositiveAndAreClampedToTheOutlierRange) {
    const BankConfig cfg;
    const Fit f{0.048, 3.0, 0.0, 0.048, 0.0, 1.0};
    EXPECT_THROW(observations_loglik(f, {{true, 0.0, 1e-8}}, cfg), std::invalid_argument);
    EXPECT_THROW(classify_space(f, -0.1, 1e-8, cfg), std::invalid_argument);
    EXPECT_EQ(observations_loglik(f, {{false, 50.0, 1e-8}}, cfg), observations_loglik(f, {{false, 10.0, 1e-8}}, cfg));
    DurationFit fit(cfg);
    fit.add(true, 0.0, 1e-8);  // ignored
    EXPECT_TRUE(fit.history().empty());
}

TEST(BankFitPython, CopyIsIndependentAndBestNeedsAnObservation) {
    const BankConfig cfg;
    DurationFit fit(cfg);
    EXPECT_FALSE(fit.best().has_value());
    fit.add(true, 0.048, 1e-8);
    DurationFit other = fit.copy();
    other.add(false, 0.048, 1e-8);
    EXPECT_EQ(fit.history().size(), 1u);
    EXPECT_EQ(other.history().size(), 2u);
    EXPECT_NEAR(fit.weight(), 1.0, 1e-6);
    const double want = 1.0 + std::exp(-1 / cfg.fit_memory);
    EXPECT_NEAR(other.weight(), want, 1e-6 * want);
}

TEST(BankFitPython, FastPathsAreBitIdenticalToThePlainFormulas) {
    // The prototype's Task 11p speed-ups have no counterpart in the port, which computes the plain formulas;
    // the assertions are kept: the tables equal the plain per-observation formula (Task 7's DurationFit.add)
    // accumulated bit for bit, also after the history wraps (> 2 x capacity adds); weighted_loglik and the
    // quality equal the plain class_logliks sums bit for bit.
    const BankConfig cfg;
    const Durations obs = load_case("fast_paths");
    const auto var = cases().at("fast_paths_var_t_s2").get<std::vector<double>>();
    DurationFit fit(cfg);
    ASSERT_EQ(obs.size(), 2 * fit.history_capacity() + 30);
    const ClassPriors pr = class_priors();
    const double keep = 1.0 - cfg.outlier_prior;
    const double lp[5] = {std::log(keep * pr.marks[0]), std::log(keep * pr.marks[1]), std::log(keep * pr.spaces[0]),
                          std::log(keep * pr.spaces[1]), std::log(keep * pr.spaces[2])};
    const double log_out = std::log(cfg.outlier_prior) - std::log(std::log(cfg.outlier_range_s[1] / cfg.outlier_range_s[0]));
    const double log_sqrt_2pi = 0.5 * std::log(2.0 * std::numbers::pi);
    const auto& t = fit.t_grid_s();
    const auto& q = fit.q_grid();
    const auto& w = fit.w_grid();
    const auto& g = fit.tg_grid();

    // One class's term at median mu (the prototype's plain formula).
    auto term = [&](double mu, double log_d, double var_t, double sigma, double prior) {
        const bool valid = mu > 0;
        const double safe = valid ? mu : 1.0;
        const double s2 = sigma * sigma + var_t * (1.0 / (safe * safe));
        const double z = log_d - std::log(safe);
        return valid ? prior - 0.5 * z * z / s2 - 0.5 * std::log(s2) - log_sqrt_2pi : -kInf;
    };
    auto plain_grid_loglik = [&](bool is_mark, double d, double var_t) {
        const double log_d = std::log(std::min(std::max(d, cfg.outlier_range_s[0]), cfg.outlier_range_s[1]));
        std::vector<double> out;
        for (std::size_t i = 0; i < t.size(); ++i) {
            if (is_mark) {
                for (std::size_t k = 0; k < q.size(); ++k)
                    for (std::size_t j = 0; j < w.size(); ++j) {
                        double total = term(t[i] * (1.0 + w[j]), log_d, var_t, cfg.sigma_ln_mark, lp[0]);
                        total = logaddexp(total, term(t[i] * (q[k] + w[j]), log_d, var_t, cfg.sigma_ln_mark, lp[1]));
                        out.push_back(logaddexp(total, log_out));
                    }
            } else {
                for (std::size_t j = 0; j < w.size(); ++j)
                    for (std::size_t l = 0; l < g.size(); ++l) {
                        double total = term(t[i] * (1.0 - w[j]), log_d, var_t, cfg.sigma_ln_space, lp[2]);
                        total = logaddexp(total, term(t[i] * (3.0 * g[l] - w[j]), log_d, var_t, cfg.sigma_ln_space, lp[3]));
                        total = logaddexp(total, term(t[i] * (7.0 * g[l] - w[j]), log_d, var_t, cfg.sigma_ln_space, lp[4]));
                        out.push_back(logaddexp(total, log_out));
                    }
            }
        }
        return out;
    };

    std::vector<double> marks(fit.mark_table().size(), 0.0);
    std::vector<double> spaces(fit.space_table().size(), 0.0);
    for (std::size_t i = 0; i < obs.size(); ++i) {
        const auto [is_mark, d] = obs[i];
        const std::vector<double> expected = plain_grid_loglik(is_mark, d, var[i]);
        const std::vector<double> got = fit.grid_loglik(is_mark, d, var[i]);
        ASSERT_EQ(got.size(), expected.size());
        for (std::size_t k = 0; k < got.size(); ++k) ASSERT_EQ(bits(got[k]), bits(expected[k])) << "obs " << i;
        fit.add(is_mark, d, var[i]);
        for (double& x : marks) x = x * fit.lambda();
        for (double& x : spaces) x = x * fit.lambda();
        std::vector<double>& table = is_mark ? marks : spaces;
        for (std::size_t k = 0; k < table.size(); ++k) table[k] = table[k] + expected[k];
    }
    for (std::size_t k = 0; k < marks.size(); ++k) ASSERT_EQ(bits(fit.mark_table()[k]), bits(marks[k]));
    for (std::size_t k = 0; k < spaces.size(); ++k) ASSERT_EQ(bits(fit.space_table()[k]), bits(spaces[k]));

    const std::vector<Obs> h(fit.history().begin(), fit.history().end());
    std::vector<double> age(h.size());
    for (std::size_t i = 0; i < h.size(); ++i) age[i] = std::pow(fit.lambda(), static_cast<double>(i));
    auto plain_sum = [&](const std::array<double, 4>& theta) {
        const ClassLogliks c = class_logliks(theta, h, cfg);
        std::vector<double> prod(h.size());
        for (std::size_t i = 0; i < h.size(); ++i) prod[i] = age[i] * c.total[i];
        return pairwise_sum(prod.data(), prod.size());
    };
    auto prior_term = [&](double t_s, std::optional<double> pt, double pw) {
        if (!pt || !(pw > 0)) return 0.0;
        const double z = (std::log(t_s) - std::log(*pt)) / cfg.prior_sigma_ln;
        return -pw * 0.5 * z * z;
    };
    const std::pair<std::optional<double>, double> priors[] = {{std::nullopt, 0.0}, {0.05, 1.0}};
    for (const auto& [pt, pw] : priors) {
        const auto grid = *fit.grid_theta(pt, pw);
        const double plain = plain_sum(grid) + prior_term(grid[0], pt, pw);
        EXPECT_EQ(bits(fit.weighted_loglik(grid, pt, pw)), bits(plain));
        const Fit best = *fit.best(pt, pw);
        const auto refined = fit.refine(grid, pt, pw);
        std::optional<std::array<double, 4>> theta;
        for (const auto& c : {refined, grid})
            if (!theta && best.t_s == c[0] && best.w_s == c[1] && best.tg_s == c[3]) theta = c;
        ASSERT_TRUE(theta.has_value());
        EXPECT_EQ(bits(best.quality), bits(plain_sum(*theta) / pairwise_sum(age.data(), age.size())));
    }
}

}  // namespace
