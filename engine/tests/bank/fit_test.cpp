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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
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
            // Measured, not asserted beyond expect_close: how many entries differ from the prototype's and by how
            // much (bit-identical before Plan B task B2(a)'s near-exact step).
            std::size_t differ = 0;
            double max_abs = 0.0, max_rel = 0.0;
            for (const auto& [got, want] : {std::pair{&fit.mark_table(), &mt}, std::pair{&fit.space_table(), &st}})
                for (std::size_t i = 0; i < want->size(); ++i)
                    if (bits((*got)[i]) != bits((*want)[i])) {
                        ++differ;
                        const double dd = std::abs((*got)[i] - (*want)[i]);
                        max_abs = std::max(max_abs, dd);
                        max_rel = std::max(max_rel, dd / std::abs((*want)[i]));
                    }
            std::printf("tables after observation %zu: %zu of %zu entries differ from the prototype's, largest "
                        "%.3g nats absolute, %.3g relative\n",
                        n, differ, mt.size() + st.size(), max_abs, max_rel);
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

// fit.json's resolution_cases: the prototype's resolution_var_s2 at 24 (L, a, r), s^2. The port squares by
// products where the prototype calls pow (section 8c, "Duration fit"), so relative 1e-9, not exact.
TEST(BankFit, ResolutionVarianceMatchesPrototype) {
    const auto g = load_golden("fit");
    const auto& cases = g.at("resolution_cases");
    ASSERT_EQ(cases.size(), 24u);
    for (const auto& c : cases)
        expect_close(resolution_var_s2(c.at(0).get<double>(), c.at(1).get<double>(), c.at(2).get<double>()),
                     c.at(3).get<double>());
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
    for (int seed = 1; seed <= 3; ++seed)
        all.push_back({load_case("model30_" + std::to_string(seed)), std::nullopt, 0.0});
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
    // accumulated (bit for bit until Plan B task B2(a); now within the traced allowance below), also after the
    // history wraps (> 2 x capacity adds); weighted_loglik and the quality equal the plain class_logliks sums
    // bit for bit.
    const BankConfig cfg;
    const Durations obs = load_case("fast_paths");
    const auto var = cases().at("fast_paths_var_t_s2").get<std::vector<double>>();
    DurationFit fit(cfg);
    ASSERT_EQ(obs.size(), 2 * fit.history_capacity() + 30);
    const ClassPriors pr = class_priors();
    const double keep = 1.0 - cfg.outlier_prior;
    const double lp[5] = {std::log(keep * pr.marks[0]), std::log(keep * pr.marks[1]), std::log(keep * pr.spaces[0]),
                          std::log(keep * pr.spaces[1]), std::log(keep * pr.spaces[2])};
    const double log_out =
        std::log(cfg.outlier_prior) - std::log(std::log(cfg.outlier_range_s[1] / cfg.outlier_range_s[0]));
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
                        total = logaddexp(
                            total, term(t[i] * (3.0 * g[l] - w[j]), log_d, var_t, cfg.sigma_ln_space, lp[3]));
                        total = logaddexp(
                            total, term(t[i] * (7.0 * g[l] - w[j]), log_d, var_t, cfg.sigma_ln_space, lp[4]));
                        out.push_back(logaddexp(total, log_out));
                    }
            }
        }
        return out;
    };

    // Plan B task B2(a), near-exact step: the port now combines the classes and the outlier in one pass
    // (m + ln sum e^(x - m)) instead of the plain formula's logaddexp chain, so the grid's values may differ
    // from the plain formula's in the last bits (traced allowance, Plan A's noise #38 precedent): at most
    // 2^-50 nats (8.9e-16, one unit in the last place at 4 to 8 nats; the largest difference measured on this
    // test's values; B2(a)'s full sweep measured at most 1.33e-15 nats over 8.5e9 values), and the tables,
    // which sum about N_mem = 48 effective observations' values, at most kTableAllowance (measured here:
    // 2.8e-14 nats). Every value that differs is counted and the largest difference
    // printed; NaN and infinities must still match bit for bit.
    constexpr double kValueAllowance = 0x1p-50;
    constexpr double kTableAllowance = 64 * 0x1p-50;
    std::size_t values_differ = 0;
    double value_max = 0.0;
    std::vector<double> marks(fit.mark_table().size(), 0.0);
    std::vector<double> spaces(fit.space_table().size(), 0.0);
    for (std::size_t i = 0; i < obs.size(); ++i) {
        const auto [is_mark, d] = obs[i];
        const std::vector<double> expected = plain_grid_loglik(is_mark, d, var[i]);
        const std::vector<double> got = fit.grid_loglik(is_mark, d, var[i]);
        ASSERT_EQ(got.size(), expected.size());
        for (std::size_t k = 0; k < got.size(); ++k) {
            if (bits(got[k]) == bits(expected[k])) continue;
            ASSERT_TRUE(std::isfinite(got[k]) && std::isfinite(expected[k])) << "obs " << i << " point " << k;
            ++values_differ;
            value_max = std::max(value_max, std::abs(got[k] - expected[k]));
            ASSERT_LE(std::abs(got[k] - expected[k]), kValueAllowance) << "obs " << i << " point " << k;
        }
        fit.add(is_mark, d, var[i]);
        for (double& x : marks) x = x * fit.lambda();
        for (double& x : spaces) x = x * fit.lambda();
        std::vector<double>& table = is_mark ? marks : spaces;
        for (std::size_t k = 0; k < table.size(); ++k) table[k] = table[k] + expected[k];
    }
    std::size_t tables_differ = 0;
    double table_max = 0.0;
    auto table_check = [&](const std::vector<double>& got, const std::vector<double>& want) {
        for (std::size_t k = 0; k < got.size(); ++k) {
            if (bits(got[k]) == bits(want[k])) continue;
            ++tables_differ;
            table_max = std::max(table_max, std::abs(got[k] - want[k]));
            ASSERT_LE(std::abs(got[k] - want[k]), kTableAllowance) << "point " << k;
        }
    };
    table_check(fit.mark_table(), marks);
    table_check(fit.space_table(), spaces);
    std::size_t values = 0;
    for (const auto& o : obs) values += o.first ? marks.size() : spaces.size();
    std::printf("grid values differing from the plain formula: %zu of %zu, largest %.3g nats; table entries "
                "differing: %zu of %zu, largest %.3g nats\n",
                values_differ, values, value_max, tables_differ, marks.size() + spaces.size(), table_max);

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

// ---------------------------------------------------------------------------------------------------------
// Plan B task B1: the grid constants are shared process-wide per configuration.

// The T axis a configuration gives: T_min (1 + t_grid_step)^i, T_min = 1.2 s / max_wpm, up to 1.2 s / min_wpm.
std::vector<double> expected_t_grid(const BankConfig& cfg) {
    const double t_min = 1.2 / cfg.max_wpm;
    const double t_max = 1.2 / cfg.min_wpm;
    const int count = static_cast<int>(std::ceil(std::log(t_max / t_min) / std::log1p(cfg.t_grid_step))) + 1;
    std::vector<double> t;
    for (int i = 0; i < count; ++i) t.push_back(t_min * std::pow(1.0 + cfg.t_grid_step, static_cast<double>(i)));
    return t;
}

// Every bit of a fit's state after the observations: tables, weight, grid maximum and best (with and without
// the T_P prior).
struct FitBits {
    std::vector<std::uint64_t> v;
    bool operator==(const FitBits&) const = default;
};
FitBits fit_bits(const BankConfig& cfg, const Durations& obs) {
    DurationFit fit(cfg);
    for (const auto& [m, d] : obs) fit.add(m, d, 1e-8);
    FitBits out;
    for (double x : fit.mark_table()) out.v.push_back(bits(x));
    for (double x : fit.space_table()) out.v.push_back(bits(x));
    out.v.push_back(bits(fit.weight()));
    for (const auto& [pt, pw] : {std::pair<std::optional<double>, double>{std::nullopt, 0.0}, {0.05, 1.0}}) {
        for (int i : *fit.grid_index(pt, pw)) out.v.push_back(static_cast<std::uint64_t>(i));
        const Fit b = *fit.best(pt, pw);
        for (double x : {b.t_s, b.q, b.w_s, b.tg_s, b.quality, b.weight}) out.v.push_back(bits(x));
    }
    return out;
}

TEST(BankFitShared, FitsOfOneConfigurationShareTheGridConstantsAndOthersDoNot) {
    const BankConfig a;
    BankConfig b;
    b.t_grid_step = 0.02;
    const DurationFit a1(a);
    const DurationFit a2(a);
    const DurationFit b1(b);
    const DurationFit b2(b);
    EXPECT_EQ(a1.model(), a2.model());
    EXPECT_EQ(a1.model(), a1.copy().model());
    EXPECT_EQ(b1.model(), b2.model());
    EXPECT_NE(a1.model(), b1.model());
    // Each decodes its own grid: its axes, its tables' sizes, and the fit of machine keying.
    for (const BankConfig* cfg : std::array<const BankConfig*, 2>{&a, &b}) {
        DurationFit fit(*cfg);
        const std::vector<double> t = expected_t_grid(*cfg);
        ASSERT_EQ(fit.t_grid_s().size(), t.size());
        for (std::size_t i = 0; i < t.size(); ++i) EXPECT_EQ(bits(fit.t_grid_s()[i]), bits(t[i]));
        EXPECT_EQ(fit.mark_table().size(), t.size() * cfg->q_grid.size() * cfg->w_grid.size());
        EXPECT_EQ(fit.space_table().size(), t.size() * cfg->w_grid.size() * cfg->tg_grid.size());
        for (const auto& [m, d] : load_case("machine")) fit.add(m, d, 1e-8);
        const auto theta = *fit.grid_theta();
        EXPECT_NE(std::find(t.begin(), t.end(), theta[0]), t.end());  // a point of its own T axis
        const Fit f = *fit.best();
        expect_rel(f.t_s, 0.048, 0.02);
        EXPECT_NEAR(f.q, 3.0, 0.1);
        expect_rel(f.tg_s, 0.048, 0.05);
    }
    EXPECT_LT(DurationFit(b).t_grid_s().size(), DurationFit(a).t_grid_s().size());
}

TEST(BankFitShared, EveryFieldTheConstantsReadSeparatesConfigurationsAndNoOtherDoes) {
    const BankConfig base;
    const DurationFit ref(base);
    // A field the grid constants do not read leaves the sharing alone.
    BankConfig other = base;
    other.correction_reach_s += 1.0;
    other.text_window_chars += 1;
    EXPECT_EQ(DurationFit(other).model(), ref.model());
    // Each field they read gives its own constants (kept alive together: all distinct).
    const std::vector<std::function<void(BankConfig&)>> changes = {
        [](BankConfig& c) { c.min_wpm = 6.0; },
        [](BankConfig& c) { c.max_wpm = 90.0; },
        [](BankConfig& c) { c.t_grid_step = 0.015; },
        [](BankConfig& c) { c.q_grid = {3.0, 4.0}; },
        [](BankConfig& c) { c.w_grid = {-0.4, 0.0, 0.4, 0.7}; },
        [](BankConfig& c) { c.tg_grid = {1.0, 1.59, 2.52, 4.0, 6.0}; },
        [](BankConfig& c) { c.outlier_prior = 0.04; },
        [](BankConfig& c) { c.outlier_range_s = {0.001, 9.0}; },
        [](BankConfig& c) { c.sigma_ln_mark = 0.14; },
        [](BankConfig& c) { c.sigma_ln_space = 0.24; },
        [](BankConfig& c) { c.fit_memory = 47.0; },
        [](BankConfig& c) { c.prior_sigma_ln = 0.11; },
        [](BankConfig& c) { c.refine_iterations = 3; },
    };
    std::vector<DurationFit> fits;
    fits.reserve(changes.size());
    for (const auto& change : changes) {
        BankConfig c = base;
        change(c);
        fits.emplace_back(c);
        EXPECT_EQ(DurationFit(c).model(), fits.back().model());
    }
    for (std::size_t i = 0; i < fits.size(); ++i) {
        EXPECT_NE(fits[i].model(), ref.model()) << "field " << i;
        for (std::size_t j = i + 1; j < fits.size(); ++j) EXPECT_NE(fits[i].model(), fits[j].model());
    }
}

TEST(BankFitShared, FitsBuiltAndUsedOnEightThreadsAtOnceGiveTheSingleThreadedResultBitForBit) {
    // The constants are const (shared_ptr<const Model>) and only read; eight threads build fits of two
    // configurations, interleaved, add the same observations and compare every bit of their state with fits
    // built on this thread. Under ThreadSanitizer this is also the data-race check.
    const BankConfig a;
    BankConfig b;
    b.t_grid_step = 0.02;
    static_assert(std::is_const_v<std::remove_pointer_t<decltype(DurationFit(a).model())>>);
    const Durations obs = load_case("machine");
    const FitBits want_a = fit_bits(a, obs);
    const FitBits want_b = fit_bits(b, obs);
    const DurationFit keep_a(a);  // keeps a's constants alive: every thread's fits of a share them
    constexpr int kThreads = 8;
    constexpr int kRounds = 6;
    std::vector<int> ok(kThreads, 0);
    std::vector<int> same_model(kThreads, 0);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([&, t] {
            for (int r = 0; r < kRounds; ++r) {
                const bool use_a = (t + r) % 2 == 0;
                ok[static_cast<std::size_t>(t)] += fit_bits(use_a ? a : b, obs) == (use_a ? want_a : want_b);
                same_model[static_cast<std::size_t>(t)] += DurationFit(a).model() == keep_a.model();
            }
        });
    for (auto& th : threads) th.join();
    for (int t = 0; t < kThreads; ++t) {
        EXPECT_EQ(ok[static_cast<std::size_t>(t)], kRounds) << "thread " << t;
        EXPECT_EQ(same_model[static_cast<std::size_t>(t)], kRounds) << "thread " << t;
    }
}

// ---------------------------------------------------------------------------------------------------------
// Plan B task B2(a): the fit's arithmetic restructured; every result compared bit for bit with a frozen copy
// of the code before the change (fit.cpp at 41b8f5e), over the golden inputs and randomized sweeps.

namespace frozen {

constexpr double kLogE2 = 0.693147180559945309417232121458176568;
const double kLogSqrt2Pi = 0.5 * std::log(2.0 * std::numbers::pi);
constexpr double kDesign[5][4] = {{1.0, 1.0, 0.0, 0.0},
                                  {0.0, 1.0, 1.0, 0.0},
                                  {1.0, -1.0, 0.0, 0.0},
                                  {0.0, -1.0, 0.0, 3.0},
                                  {0.0, -1.0, 0.0, 7.0}};
constexpr bool kIsMarkClass[5] = {true, true, false, false, false};

double logaddexp(double x, double y) {
    if (x == y) return x + kLogE2;
    return std::max(x, y) + std::log1p(std::exp(-std::abs(x - y)));
}

// The model's constants, as Model(cfg) computes them.
struct Consts {
    std::array<double, 5> lp{};
    std::array<double, 5> sigma2{};
    double log_out = 0.0;
    double lo = 0.0, hi = 0.0;
    explicit Consts(const BankConfig& cfg) {
        const ClassPriors p = class_priors();
        const double keep = 1.0 - cfg.outlier_prior;
        lp = {std::log(keep * p.marks[0]), std::log(keep * p.marks[1]), std::log(keep * p.spaces[0]),
              std::log(keep * p.spaces[1]), std::log(keep * p.spaces[2])};
        lo = cfg.outlier_range_s.at(0);
        hi = cfg.outlier_range_s.at(1);
        log_out = std::log(cfg.outlier_prior) - std::log(std::log(hi / lo));
        for (int c = 0; c < 5; ++c) {
            const double sigma = kIsMarkClass[c] ? cfg.sigma_ln_mark : cfg.sigma_ln_space;
            sigma2[static_cast<std::size_t>(c)] = sigma * sigma;
        }
    }
};

std::array<double, 5> medians(const std::array<double, 4>& theta) {
    std::array<double, 5> mu{};
    for (int c = 0; c < 5; ++c) {
        double s = 0.0;
        for (int j = 0; j < 4; ++j) s += kDesign[c][j] * theta[static_cast<std::size_t>(j)];
        mu[static_cast<std::size_t>(c)] = s;
    }
    return mu;
}

struct Terms {
    std::vector<std::array<double, 5>> ll;
    std::vector<double> total;
    std::vector<std::array<double, 5>> s2;
    std::array<double, 5> safe{};
    std::array<double, 5> log_mu{};
};

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

double aged_sum(const Retained& r, const std::vector<double>& total) {
    std::vector<double> prod(total.size());
    for (std::size_t i = 0; i < total.size(); ++i) prod[i] = r.age[i] * total[i];
    return pairwise_sum(prod);
}

double prior_term(const BankConfig& cfg, double t_s, std::optional<double> prior_t_s, double prior_weight) {
    if (!prior_t_s || !(prior_weight > 0.0)) return 0.0;
    const double z = (std::log(t_s) - std::log(*prior_t_s)) / cfg.prior_sigma_ln;
    return -prior_weight * 0.5 * z * z;
}

double weighted_loglik(const DurationFit& fit, const BankConfig& cfg, const std::array<double, 4>& theta,
                       std::optional<double> prior_t_s, double prior_weight) {
    const Consts k(cfg);
    const Retained r = retained(fit.history(), fit.lambda(), k.lo, k.hi);
    const Terms t = terms(theta, r.is_mark, r.log_d, r.var_t, k.lp, k.sigma2, k.log_out);
    return aged_sum(r, t.total) + prior_term(cfg, theta[0], prior_t_s, prior_weight);
}

std::array<double, 4> refine(const DurationFit& fit, const BankConfig& cfg, const std::array<double, 4>& start,
                             std::optional<double> prior_t_s, double prior_weight) {
    const Consts k(cfg);
    const Retained r = retained(fit.history(), fit.lambda(), k.lo, k.hi);
    std::array<double, 4> theta = start;
    Terms t = terms(theta, r.is_mark, r.log_d, r.var_t, k.lp, k.sigma2, k.log_out);
    const std::size_t n = r.log_d.size();
    for (int it = 0; it < cfg.refine_iterations; ++it) {
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
            const double wp = prior_weight / (cfg.prior_sigma_ln * cfg.prior_sigma_ln);
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
        t = terms(theta, r.is_mark, r.log_d, r.var_t, k.lp, k.sigma2, k.log_out);
    }
    return theta;
}

std::optional<Fit> best(const DurationFit& fit, const BankConfig& cfg, std::optional<double> prior_t_s,
                        double prior_weight) {
    const auto grid = fit.grid_theta(prior_t_s, prior_weight);
    if (!grid) return std::nullopt;
    const Consts k(cfg);
    const Retained r = retained(fit.history(), fit.lambda(), k.lo, k.hi);
    const std::array<double, 4> refined = refine(fit, cfg, *grid, prior_t_s, prior_weight);
    const double refined_sum =
        aged_sum(r, terms(refined, r.is_mark, r.log_d, r.var_t, k.lp, k.sigma2, k.log_out).total);
    const double grid_sum = aged_sum(r, terms(*grid, r.is_mark, r.log_d, r.var_t, k.lp, k.sigma2, k.log_out).total);
    std::array<double, 4> theta = *grid;
    double total_sum = grid_sum;
    if (refined_sum + prior_term(cfg, refined[0], prior_t_s, prior_weight) >=
        grid_sum + prior_term(cfg, (*grid)[0], prior_t_s, prior_weight)) {
        theta = refined;
        total_sum = refined_sum;
    }
    return Fit{theta[0], theta[2] / theta[0], theta[1], theta[3], total_sum / r.age_sum, fit.weight()};
}

// The grid's per-class constants (Model::prep): ln median, 1 / median^2, median > 0.
struct GridClass {
    std::vector<double> log_mu, inv_mu2;
    std::vector<char> valid;
    void prep(double mu) {
        const bool v = mu > 0.0;
        const double safe = v ? mu : 1.0;
        log_mu.push_back(std::log(safe));
        inv_mu2.push_back(1.0 / (safe * safe));
        valid.push_back(v ? 1 : 0);
    }
};

struct Grid {
    std::array<GridClass, 2> marks;
    std::array<GridClass, 3> spaces;
    Consts k;
    double sigma_ln_mark, sigma_ln_space;
    explicit Grid(const BankConfig& cfg)
        : k(cfg), sigma_ln_mark(cfg.sigma_ln_mark), sigma_ln_space(cfg.sigma_ln_space) {
        const DurationFit fit(cfg);
        const auto& t = fit.t_grid_s();
        const auto& q = fit.q_grid();
        const auto& w = fit.w_grid();
        const auto& g = fit.tg_grid();
        for (std::size_t i = 0; i < t.size(); ++i) {
            const double T = t[i];
            for (std::size_t kq = 0; kq < q.size(); ++kq)
                for (std::size_t j = 0; j < w.size(); ++j) {
                    marks[0].prep(T * (1.0 + w[j]));
                    marks[1].prep(T * (q[kq] + w[j]));
                }
            for (std::size_t j = 0; j < w.size(); ++j)
                for (std::size_t l = 0; l < g.size(); ++l) {
                    spaces[0].prep(T * (1.0 - w[j]));
                    spaces[1].prep(T * (3.0 * g[l] - w[j]));
                    spaces[2].prep(T * (7.0 * g[l] - w[j]));
                }
        }
    }

    // DurationFit::grid_loglik before B2(a), into total (resized).
    void grid_loglik(bool is_mark, double duration_s, double var_t, std::vector<double>& total) const {
        const double log_d = std::log(std::min(std::max(duration_s, k.lo), k.hi));
        const double sigma = is_mark ? sigma_ln_mark : sigma_ln_space;
        const double sigma2 = sigma * sigma;
        const std::size_t nclass = is_mark ? 2 : 3;
        const std::size_t size = is_mark ? marks[0].valid.size() : spaces[0].valid.size();
        total.resize(size);
        for (std::size_t at = 0; at < size; ++at) {
            double acc = 0.0;
            for (std::size_t c = 0; c < nclass; ++c) {
                const GridClass& cls = is_mark ? marks[c] : spaces[c];
                const double lp = k.lp[is_mark ? c : 2 + c];
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
            total[at] = logaddexp(acc, k.log_out);
        }
    }
};

}  // namespace frozen

// The configurations of the sweeps: the default, and grids that give medians not positive (-inf class terms)
// at some grid points: w / T up to 1.2 (element space T - w <= 0) and T_g / T down to 0.2 (character gap
// 3 T_g - w <= 0).
std::vector<BankConfig> sweep_configs() {
    BankConfig def;
    BankConfig invalid;
    invalid.w_grid = {-0.6, 0.0, 1.0, 1.2};
    invalid.tg_grid = {0.2, 0.3, 1.0, 4.0};
    invalid.q_grid = {2.0, 3.0};
    return {def, invalid};
}

// A random observation: durations log-uniform over 10 us to 100 s (beyond the outlier clamps 1 ms and 10 s),
// one in sixteen exactly at a clamp, one step beside it, or extreme; var_t 0 (one in eight), very large (1,
// 1e10, 1e300 s^2 or +inf; one in sixteen), else log-uniform over 1e-14 to 1e-2 s^2.
Obs random_obs(std::mt19937_64& rng) {
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::uniform_int_distribution<int> pick(0, 15);
    Obs o{};
    o.is_mark = pick(rng) % 2 == 0;
    if (pick(rng) == 0) {
        constexpr double edge[] = {0.001, 10.0, 0.0009999999999999998, 0.0010000000000000002, 9.999999999999998,
                                   10.000000000000002, 1e-300, 1e300};
        o.d_s = edge[std::uniform_int_distribution<int>(0, 7)(rng)];
    } else {
        o.d_s = std::exp(std::log(1e-5) + u(rng) * (std::log(100.0) - std::log(1e-5)));
    }
    const int vk = pick(rng);
    if (vk < 2) {
        o.var_t_s2 = 0.0;
    } else if (vk == 2) {
        constexpr double big[] = {1.0, 1e10, 1e300, kInf};
        o.var_t_s2 = big[std::uniform_int_distribution<int>(0, 3)(rng)];
    } else {
        o.var_t_s2 = std::exp(std::log(1e-14) + u(rng) * (std::log(1e-2) - std::log(1e-14)));
    }
    return o;
}

// The sweeps' sizes: by default small enough for every ctest run (about 20 s on the Windows PC); with the
// environment variable KZ4AP_FIT_FULL_SWEEP=1 the full sizes the plan asks for (10^6 observations per
// configuration at every grid point; 2000 random fits per configuration and refine_iterations), run once per
// change on each platform (about 20 min on the Windows PC with the code before B2(a) on both sides).
bool full_sweep() {
    const char* v = std::getenv("KZ4AP_FIT_FULL_SWEEP");
    return v != nullptr && std::string(v) == "1";
}

std::string describe(const Obs& o) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "is_mark %d d %.17g s var_t %.17g s^2", o.is_mark ? 1 : 0, o.d_s, o.var_t_s2);
    return buf;
}

TEST(BankFitB2a, LogaddexpIsBitIdenticalToTheFrozenFormula) {
    // Pairs of special values; then 4 x 10^6 larger terms m over every exponent (and of moderate size), each
    // with a gap d at the skip threshold (58 - k) ln 2, one step below it, within relative 1e-12 of it, or
    // random below twice it, and at 0.8 d and 0.5 d (where the formula's value is not max(x, y)), both orders.
    std::mt19937_64 rng(20261004);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    const std::vector<double> special = {0.0, -0.0, kInf, -kInf, std::numeric_limits<double>::quiet_NaN(),
                                         std::numeric_limits<double>::denorm_min(),
                                         -std::numeric_limits<double>::min(), std::numeric_limits<double>::max(),
                                         -std::numeric_limits<double>::max(), -5.216, 0.37, -40.0, 1e-300, -1e-300};
    for (double x : special)
        for (double y : special) ASSERT_EQ(bits(logaddexp(x, y)), bits(frozen::logaddexp(x, y))) << x << " " << y;
    std::size_t n = 0;
    for (int i = 0; i < 4'000'000; ++i) {
        const int e = std::uniform_int_distribution<int>(-1030, 1022)(rng);
        const double mag = i % 2 == 0 ? std::ldexp(1.0 + u(rng), e) : u(rng) * 100.0;
        const double m = u(rng) < 0.5 ? -mag : mag;
        int k = 0;
        std::frexp(m, &k);  // |m| in [2^(k-1), 2^k)
        const double thr = (58.0 - (k - 1)) * std::numbers::ln2;
        double d;
        switch (i % 4) {
            case 0: d = thr; break;
            case 1: d = thr * (1.0 + (u(rng) - 0.5) * 1e-12); break;
            case 2: d = std::nextafter(thr, 0.0); break;
            default: d = u(rng) * 2.0 * thr; break;
        }
        for (const double dd : {d, 0.8 * d, 0.5 * d}) {
            const double y = m - dd;
            ASSERT_EQ(bits(logaddexp(m, y)), bits(frozen::logaddexp(m, y))) << m << " " << y;
            ASSERT_EQ(bits(logaddexp(y, m)), bits(frozen::logaddexp(y, m))) << y << " " << m;
            ++n;
        }
    }
    EXPECT_EQ(n, 12'000'000u);
}

// The differences of the near-exact step (one-pass log-sum-exp) from the frozen code: where the frozen value
// or the new one is not finite, the new one must have the frozen one's bits; elsewhere |new - frozen| is
// measured, absolute (nats) and relative to |frozen|, and checked against a bound relative to
// max(|frozen|, 1 nat).
struct Diffs {
    std::size_t n = 0;
    std::size_t nonfinite_bad = 0;
    std::size_t over_bound = 0;
    double max_abs = 0.0;
    double max_rel = 0.0;
    double bound = 0.0;
    std::string first;
    explicit Diffs(double b = 0.0) : bound(b) {}
    void add(double got, double want, const std::function<std::string()>& where) {
        ++n;
        if (!std::isfinite(want) || !std::isfinite(got)) {
            if (bits(got) != bits(want)) {
                ++nonfinite_bad;
                if (first.empty()) first = where() + ": not finite";
            }
            return;
        }
        const double d = std::abs(got - want);
        max_abs = std::max(max_abs, d);
        if (want != 0.0) max_rel = std::max(max_rel, d / std::abs(want));
        if (d > bound * std::max(std::abs(want), 1.0)) {
            ++over_bound;
            if (first.empty()) {
                char buf[200];
                std::snprintf(buf, sizeof buf, ": %.17g against %.17g", got, want);
                first = where() + buf;
            }
        }
    }
    void merge(const Diffs& o) {
        n += o.n;
        nonfinite_bad += o.nonfinite_bad;
        over_bound += o.over_bound;
        max_abs = std::max(max_abs, o.max_abs);
        max_rel = std::max(max_rel, o.max_rel);
        if (first.empty()) first = o.first;
    }
    void expect_ok(const std::string& what) const {
        std::printf("%s: %zu values, largest difference %.3g absolute (in the value's unit: nats, s or none), %.3g "
                    "relative; %zu above the bound "
                    "%.3g x max(|value|, 1 unit); %zu non-finite mismatches\n",
                    what.c_str(), n, max_abs, max_rel, over_bound, bound, nonfinite_bad);
        EXPECT_EQ(nonfinite_bad, 0u) << what << ": " << first;
        EXPECT_EQ(over_bound, 0u) << what << ": " << first;
    }
};

// The bound on the near-exact differences, relative to max(|value|, 1 nat): 1e-14, 45 units in the last
// place of 1 (heuristic, above the measured largest differences stated in docs/signal-processing.md, 8c).
constexpr double kNearExactBound = 1e-14;

TEST(BankFitB2a, GridLoglikMatchesTheFrozenCodeToRoundingAtEveryGridPoint) {
    // 10^6 random observations per configuration (two configurations; 20 000 without KZ4AP_FIT_FULL_SWEEP),
    // each at every grid point of its table (3636 mark or 6060 space points with the defaults), then the
    // golden observations.
    const std::size_t kObs = full_sweep() ? 1'000'000 : 20'000;
    const char* names[] = {"grid_loglik, default grids", "grid_loglik, grids with medians <= 0"};
    int which = 0;
    for (const BankConfig& cfg : sweep_configs()) {
        const frozen::Grid grid(cfg);
        const DurationFit fit(cfg);
        const std::size_t nt = std::max<std::size_t>(1, std::thread::hardware_concurrency());
        std::vector<Diffs> part(nt, Diffs(kNearExactBound));
        std::vector<std::thread> threads;
        for (std::size_t t = 0; t < nt; ++t)
            threads.emplace_back([&, t] {
                const std::size_t b = kObs * t / nt;
                std::mt19937_64 rng(1000003 * (b + 1));
                std::vector<double> want;
                for (std::size_t i = b; i < kObs * (t + 1) / nt; ++i) {
                    const Obs o = random_obs(rng);
                    grid.grid_loglik(o.is_mark, o.d_s, o.var_t_s2, want);
                    const std::vector<double> got = fit.grid_loglik(o.is_mark, o.d_s, o.var_t_s2);
                    if (got.size() != want.size()) {
                        part[t].add(kInf, 0.0, [&] { return "size " + describe(o); });
                        continue;
                    }
                    for (std::size_t k = 0; k < got.size(); ++k)
                        part[t].add(got[k], want[k], [&] { return describe(o) + " point " + std::to_string(k); });
                }
            });
        for (auto& th : threads) th.join();
        Diffs all(kNearExactBound);
        for (const Diffs& p : part) all.merge(p);
        all.expect_ok(names[which++]);
    }
    const auto g = load_golden("fit");
    const auto mark = g.at("obs_mark").get<std::string>();
    const auto d = g.at("obs_d_s").get<std::vector<double>>();
    const auto v = g.at("obs_var_t_s2").get<std::vector<double>>();
    const frozen::Grid grid{BankConfig{}};
    const DurationFit fit{BankConfig{}};
    std::vector<double> want;
    Diffs golden(kNearExactBound);
    for (std::size_t i = 0; i < d.size(); ++i) {
        grid.grid_loglik(mark.at(i) == '1', d[i], v[i], want);
        const std::vector<double> got = fit.grid_loglik(mark.at(i) == '1', d[i], v[i]);
        ASSERT_EQ(got.size(), want.size());
        for (std::size_t k = 0; k < got.size(); ++k)
            golden.add(got[k], want[k], [&] { return "golden " + std::to_string(i) + " point " + std::to_string(k); });
    }
    golden.expect_ok("grid_loglik, golden observations");
}

// What the fit's searches give, the new code against the frozen copy, on one fit's history (the grid point
// comes from the fit's own tables on both sides): best, refine from the grid point, weighted_loglik at the
// grid point, the refined point and the given points, with three T_P priors (none, 50 ms with weight 1,
// 200 ms with weight 3); class_logliks at the given points (ll and var_lin bit for bit, total to rounding).
// best's quality Q, a weighted mean of up to 192 observations' totals, nats, gets a bound of 1e-13 x max(|Q|,
// 1 nat) (heuristic; measured on the Linux machine: 4.7e-14 nats at Q = -2.56 nats with 5 refinement steps).
constexpr double kQualityBound = 1e-13;

struct SearchDiffs {
    Diffs best{kNearExactBound}, quality{kQualityBound}, refine{kNearExactBound}, wll{kNearExactBound},
        total{kNearExactBound};
    std::size_t bits_bad = 0;
    std::string first_bits;
    void merge(const SearchDiffs& o) {
        best.merge(o.best);
        quality.merge(o.quality);
        refine.merge(o.refine);
        wll.merge(o.wll);
        total.merge(o.total);
        bits_bad += o.bits_bad;
        if (first_bits.empty()) first_bits = o.first_bits;
    }
    void expect_ok(const std::string& what) const {
        best.expect_ok(what + ", best (T, q, w, T_g, weight)");
        quality.expect_ok(what + ", best quality");
        refine.expect_ok(what + ", refine");
        wll.expect_ok(what + ", weighted_loglik");
        total.expect_ok(what + ", class_logliks total");
        EXPECT_EQ(bits_bad, 0u) << what << ": " << first_bits;
    }
};

void compare_searches(const DurationFit& fit, const BankConfig& cfg, const std::vector<std::array<double, 4>>& points,
                      const std::string& where, SearchDiffs& out) {
    const std::vector<Obs> h(fit.history().begin(), fit.history().end());
    const frozen::Consts k(cfg);
    auto at_where = [&](const char* what) { return [&where, what] { return where + ": " + what; }; };
    const std::pair<std::optional<double>, double> priors[] = {{std::nullopt, 0.0}, {0.05, 1.0}, {0.2, 3.0}};
    for (const auto& [pt, pw] : priors) {
        const auto got = fit.best(pt, pw);
        const auto want = frozen::best(fit, cfg, pt, pw);
        if (got.has_value() != want.has_value()) {
            out.best.add(kInf, 0.0, at_where("best presence"));
            continue;
        }
        if (!got) continue;
        for (const auto& [a, b] : {std::pair{got->t_s, want->t_s}, std::pair{got->q, want->q},
                                   std::pair{got->w_s, want->w_s}, std::pair{got->tg_s, want->tg_s},
                                   std::pair{got->weight, want->weight}})
            out.best.add(a, b, at_where("best"));
        out.quality.add(got->quality, want->quality, at_where("best quality"));
        const auto grid = *fit.grid_theta(pt, pw);
        const auto r1 = fit.refine(grid, pt, pw);
        const auto r0 = frozen::refine(fit, cfg, grid, pt, pw);
        for (std::size_t i = 0; i < 4; ++i) out.refine.add(r1[i], r0[i], at_where("refine"));
        std::vector<std::array<double, 4>> at = points;
        at.push_back(grid);
        at.push_back(r0);
        for (const auto& th : at)
            out.wll.add(fit.weighted_loglik(th, pt, pw), frozen::weighted_loglik(fit, cfg, th, pt, pw),
                        at_where("weighted_loglik"));
    }
    if (h.empty()) return;
    std::vector<bool> is_mark;
    std::vector<double> log_d, var_t;
    for (const Obs& o : h) {
        is_mark.push_back(o.is_mark);
        log_d.push_back(std::log(std::min(std::max(o.d_s, k.lo), k.hi)));
        var_t.push_back(o.var_t_s2);
    }
    for (const auto& th : points) {
        const ClassLogliks got = class_logliks(th, h, cfg);
        const frozen::Terms want = frozen::terms(th, is_mark, log_d, var_t, k.lp, k.sigma2, k.log_out);
        for (std::size_t i = 0; i < h.size(); ++i) {
            out.total.add(got.total[i], want.total[i], at_where("class_logliks total"));
            for (std::size_t c = 0; c < 5; ++c) {
                const bool same = bits(got.ll[i][c]) == bits(want.ll[i][c]) &&
                                  bits(got.var_lin[i][c]) == bits(want.s2[i][c] * (want.safe[c] * want.safe[c]));
                if (!same) {
                    ++out.bits_bad;
                    if (out.first_bits.empty()) out.first_bits = where + ": class_logliks ll or var_lin";
                }
            }
        }
    }
}

// theta = (T, w, qT, T_g), s: plausible points, points whose medians are not positive (w >= T: element space;
// 3 T_g <= w: character gap; qT <= -w: dah), T at its 0.1 ms floor, and random points.
std::vector<std::array<double, 4>> sweep_points(std::mt19937_64& rng) {
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<std::array<double, 4>> p = {{0.048, 0.0, 0.144, 0.048},  {0.048, 0.06, 0.144, 0.048},
                                            {0.048, 0.048, 0.144, 0.016}, {0.048, -0.05, 0.03, 0.048},
                                            {0.048, 0.2, 0.144, 0.05},    {1e-4, 0.0, 3e-4, 1e-4}};
    for (int i = 0; i < 4; ++i) {
        const double t = std::exp(std::log(0.012) + u(rng) * std::log(20.0));
        p.push_back({t, (u(rng) * 2.4 - 1.2) * t, (1.0 + 6.0 * u(rng)) * t, (0.2 + 10.0 * u(rng)) * t});
    }
    return p;
}

TEST(BankFitB2a, SearchesMatchTheFrozenCodeToRounding) {
    // The golden sequence (300 observations; the searches after each of the first 60 and every 10th), six
    // fit_cases sequences, and per configuration (two) and refine_iterations (2, 0, 5) 2000 random fits (100
    // without KZ4AP_FIT_FULL_SWEEP) of 1 to 400 observations (the history wraps beyond 192), three in four
    // Morse-like around a random dit, one in four random_obs (its edge cases included).
    const auto g = load_golden("fit");
    const auto mark = g.at("obs_mark").get<std::string>();
    const auto d = g.at("obs_d_s").get<std::vector<double>>();
    const auto v = g.at("obs_var_t_s2").get<std::vector<double>>();
    std::mt19937_64 rng(7);
    {
        const BankConfig cfg;
        DurationFit fit(cfg);
        SearchDiffs out;
        for (std::size_t i = 0; i < d.size(); ++i) {
            fit.add(mark.at(i) == '1', d[i], v[i]);
            if (i < 60 || i % 10 == 9)
                compare_searches(fit, cfg, sweep_points(rng), "golden " + std::to_string(i), out);
        }
        for (const std::string name : {"machine", "farnsworth", "key_weighting", "heavy_dahs", "hi", "random"}) {
            DurationFit f(cfg);
            for (const auto& [m, dd] : load_case(name)) f.add(m, dd, 1e-8);
            compare_searches(f, cfg, sweep_points(rng), name, out);
        }
        out.expect_ok("searches, golden and fit cases");
    }
    int which = 0;
    for (const BankConfig& base : sweep_configs()) {
        ++which;
        for (const int iters : {2, 0, 5}) {
            BankConfig cfg = base;
            cfg.refine_iterations = iters;
            const std::size_t fits = full_sweep() ? 2000 : 100;
            const std::size_t nt = std::max<std::size_t>(1, std::thread::hardware_concurrency());
            std::vector<SearchDiffs> part(nt);
            std::vector<std::thread> threads;
            for (std::size_t th = 0; th < nt; ++th)
                threads.emplace_back([&, th] {
                    const std::size_t b = fits * th / nt;
                    std::mt19937_64 r(99991 * (b + 1) + static_cast<std::size_t>(iters));
                    std::uniform_real_distribution<double> u(0.0, 1.0);
                    for (std::size_t i = b; i < fits * (th + 1) / nt; ++i) {
                        DurationFit fit(cfg);
                        const int n = std::uniform_int_distribution<int>(1, 400)(r);
                        const double t = std::exp(std::log(0.012) + u(r) * std::log(20.0));
                        for (int j = 0; j < n; ++j) {
                            Obs o = random_obs(r);
                            if (std::uniform_int_distribution<int>(0, 3)(r) != 0) {
                                constexpr double mult[] = {1.0, 3.0, 1.0, 3.0, 7.0};
                                const int c = std::uniform_int_distribution<int>(0, 4)(r);
                                o.is_mark = c < 2;
                                o.d_s = t * mult[c] * std::exp(0.2 * std::normal_distribution<double>(0.0, 1.0)(r));
                            }
                            fit.add(o.is_mark, o.d_s, o.var_t_s2);
                        }
                        compare_searches(fit, cfg, sweep_points(r), "random fit " + std::to_string(i), part[th]);
                    }
                });
            for (auto& th : threads) th.join();
            SearchDiffs all;
            for (const SearchDiffs& p : part) all.merge(p);
            all.expect_ok("searches, configuration " + std::to_string(which) + ", refine_iterations " +
                          std::to_string(iters));
        }
    }
}

}  // namespace
