// The bank decoder's periodicity estimator (the comb on Pi = 2T) against the prototype
// (training/kz4ap_proto/periodicity.py).
// 1. Golden values (engine/tests/data/bank/periodicity.json, from golden.py golden_periodicity): the comb as
//    ChannelDecoder.run drives it on three keyed streams (12, 25 and 40 WPM, 12 s at 1500 samples/s): the
//    same pushes (branch 1's posterior, block by block) and an update after each; at every recomputation T_P,
//    the window and each window's T to relative 1e-9 (a T one grid point off fails that by far), the
//    confidence and the scores to relative 1e-9 (floor 1e-12).
// 2. The Python comb tests (training/tests/test_proto_periodicity.py), one C++ test per Python test with the
//    same inputs (engine/tests/data/bank/periodicity_cases.json: the key-down intervals and the noise draws,
//    from which keyed_p and the noise are rebuilt as the Python test builds them) and the same assertions; the
//    strict xfail is a GTEST_SKIP with the same reason. The edge-comb and spectrum tests are not ported (those
//    methods are not).
#include "kz4ap/bank/periodicity.hpp"

#include "golden.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace kz4ap::bank;
using kz4ap::test::decode_f64_base64;
using kz4ap::test::expect_close;
using kz4ap::test::load_golden;

constexpr double kRate = 1500.0;

// pytest.approx(expected, rel=rel): |actual - expected| <= rel |expected|.
void expect_rel(double actual, double expected, double rel) {
    EXPECT_NEAR(actual, expected, rel * std::abs(expected));
}

void expect_optional(const std::optional<double>& actual, const nlohmann::json& expected) {
    if (expected.is_null()) {
        EXPECT_FALSE(actual.has_value());
    } else {
        ASSERT_TRUE(actual.has_value());
        expect_close(*actual, expected.get<double>());
    }
}

// ProtoConfig(periodicity_windows_s=windows, comb_confidence_min=0, ...): every estimate confident.
BankConfig open_config(std::vector<double> windows) {
    BankConfig cfg;
    cfg.periodicity_windows_s = std::move(windows);
    cfg.comb_confidence_min = 0.0;
    cfg.edge_confidence_min = 0.0;
    cfg.spectrum_confidence_min = 0.0;
    return cfg;
}

const nlohmann::json& cases() {
    static const nlohmann::json g = load_golden("periodicity_cases");
    return g;
}

// np.convolve(x, np.ones(14) / 14)[m] for m in [m0, m1): the sum over j = 0 ... 13 of x[m - j] / 14.
std::vector<double> smooth14(const std::vector<double>& x, std::size_t m0, std::size_t m1) {
    std::vector<double> out;
    for (std::size_t m = m0; m < m1; ++m) {
        double s = 0.0;
        for (std::size_t j = 0; j < 14 && j <= m; ++j)
            if (m - j < x.size()) s += x[m - j] * (1.0 / 14.0);
        out.push_back(s);
    }
    return out;
}

// test_proto_periodicity.ten_seconds for a case of periodicity_cases.json: keyed_p (1 while keyed, through
// branch 1's 14-sample boxcar) over samples [1500, 16500), 1 s to 11 s.
std::vector<double> ten_seconds(const std::string& name) {
    const auto a = cases().at(name + "_a_s").get<std::vector<double>>();
    const auto b = cases().at(name + "_b_s").get<std::vector<double>>();
    const long long n = cases().at(name + "_n").get<long long>();
    const std::size_t end = 11 * 1500;
    std::vector<double> p(end, 0.0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        const long long lo = static_cast<long long>(std::nearbyint(a[i] * kRate));
        const long long hi = std::min(n, static_cast<long long>(std::nearbyint(b[i] * kRate)));
        for (long long m = lo; m < hi && m < static_cast<long long>(end); ++m) p[static_cast<std::size_t>(m)] = 1.0;
    }
    return smooth14(p, 1500, end);
}

// ---------------------------------------------------------------- golden values

TEST(BankPeriodicity, TGridMatchesPrototype) {
    const auto g = load_golden("periodicity");
    const auto want = g.at("t_grid_s").get<std::vector<double>>();
    const auto got = t_grid(BankConfig{});
    ASSERT_EQ(got.size(), want.size());
    for (std::size_t i = 0; i < want.size(); ++i) expect_close(got[i], want[i]);
}

TEST(BankPeriodicity, CombMatchesPrototypeThroughRun) {
    const auto g = load_golden("periodicity");
    const double rate = g.at("rate_hz").get<double>();
    for (const int wpm : g.at("wpm").get<std::vector<int>>()) {
        SCOPED_TRACE(std::to_string(wpm) + " WPM");
        const std::string key = std::to_string(wpm);
        Periodicity per(BankConfig{}, rate);
        EXPECT_EQ(per.factor(), g.at("factor").get<int>());
        EXPECT_EQ(per.windows(), g.at("windows_samples").get<std::vector<int>>());
        EXPECT_EQ(per.update_every(), g.at("update_every").get<int>());
        const auto p = decode_f64_base64(g.at(key + "_p_b64").get<std::string>());
        const auto lengths = g.at(key + "_block_lengths").get<std::vector<int>>();
        const auto& updates = g.at(key + "_updates");
        std::size_t u = 0, n0 = 0;
        for (std::size_t b = 0; b < lengths.size(); ++b) {
            const auto len = static_cast<std::size_t>(lengths[b]);
            ASSERT_LE(n0 + len, p.size());
            per.push(std::span<const double>(p.data() + n0, len));
            n0 += len;
            const PeriodicityUpdate r = per.update(false);
            const bool want_update = u < updates.size() && updates[u].at(0).get<std::size_t>() == b;
            ASSERT_EQ(r.updated, want_update) << "block " << b;
            if (!want_update) continue;
            SCOPED_TRACE("update at block " + std::to_string(b));
            const auto& w = updates[u++];
            expect_optional(r.t_p_s, w.at(1));
            expect_close(r.confidence, w.at(2).get<double>());
            expect_optional(r.window_s, w.at(3));
            const auto& pw = w.at(4);
            ASSERT_EQ(per.per_window().size(), pw.size());
            for (std::size_t i = 0; i < pw.size(); ++i) {
                expect_optional(per.per_window()[i].first, pw[i].at(0));
                expect_close(per.per_window()[i].second, pw[i].at(1).get<double>());
            }
        }
        EXPECT_EQ(n0, p.size());
        EXPECT_EQ(u, updates.size());
    }
}

TEST(BankPeriodicity, OnlyTheCombIsPorted) {
    for (const char* method : {"edge", "spectrum", "unknown"}) {
        BankConfig cfg;
        cfg.periodicity_method = method;
        EXPECT_THROW(Periodicity(cfg, kRate), std::invalid_argument) << method;
    }
}

TEST(BankPeriodicity, CombOfInputThatDoesNotVaryIsNone) {
    const auto grid = t_grid(BankConfig{});
    const std::vector<double> flat(1000, 0.25);
    const auto [t, score] = comb_estimate(flat, 750.0, grid, 4, 0.075);
    EXPECT_FALSE(t.has_value());
    EXPECT_EQ(score, 0.0);
    const std::vector<double> few = {0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0};
    EXPECT_FALSE(comb_estimate(few, 750.0, grid, 4, 0.075).first.has_value());  // fewer than 16 samples
}

// ---------------------------------------------------------------- the Python tests

// test_periodicity_finds_the_dit[comb-*] except Farnsworth 18/10 (below). Expected T = 1.2 s / WPM (derived).
TEST(BankPeriodicityPython, FindsTheDit) {
    const std::vector<std::pair<std::string, double>> runs = {{"5_machine", 5.0},   {"12_machine", 12.0},
                                                              {"25_machine", 25.0}, {"40_machine", 40.0},
                                                              {"100_machine", 100.0}, {"25_paddle", 25.0}};
    for (const auto& [name, wpm] : runs) {
        SCOPED_TRACE(name);
        Periodicity per(open_config({10.0}), kRate);
        per.push(ten_seconds(name));
        const PeriodicityUpdate r = per.update(true);
        EXPECT_TRUE(r.updated);
        ASSERT_TRUE(r.window_s.has_value());
        expect_rel(*r.window_s, 10.0, 1e-6);
        ASSERT_TRUE(r.t_p_s.has_value());
        expect_rel(*r.t_p_s, 1.2 / wpm, 0.05);
    }
}

TEST(BankPeriodicityPython, FindsTheDitFarnsworth) {
    GTEST_SKIP() << "measured: comb T = 204.5 ms (score 0.1464) vs true 66.7 ms, locking near the Farnsworth gap "
                    "timebase T_g = 207 ms (about 3.1 T); the comb's best within +/-5% of T scores 0.1190; miss "
                    "rates over seeds 1-5 x 10 windows: 2/50 at 10 s, 4/50 at 5 s, 7/50 at 2 s (at 0.19-1.38 T); "
                    "the spectrum fit (67.8 ms) and edge comb (66.4 ms) get it right";
}

// test_noise_scores_below_keying[comb].
TEST(BankPeriodicityPython, NoiseScoresBelowKeying) {
    const auto uniform = decode_f64_base64(cases().at("noise_uniform_b64").get<std::string>());
    ASSERT_EQ(uniform.size(), 15000u);
    std::vector<double> scaled;
    for (double v : uniform) scaled.push_back(v * 0.2);
    const auto noise = smooth14(scaled, 0, 15000);
    std::vector<double> scores;
    for (const auto& p : {ten_seconds("25_machine"), noise}) {
        Periodicity per(open_config({10.0}), kRate);
        per.push(p);
        scores.push_back(per.update(true).confidence);
    }
    EXPECT_GT(scores[0], scores[1]);
}

TEST(BankPeriodicityPython, TheShortestFullWindowIsUsedAndUpdatesFollowTheInterval) {
    Periodicity per(open_config({2.0, 5.0, 10.0}), kRate);
    const auto p = ten_seconds("25_machine");
    const std::span<const double> s(p);
    per.push(s.subspan(0, 1500));
    EXPECT_FALSE(per.update(true).t_p_s.has_value());  // no window is full yet
    per.push(s.subspan(1500, 150));
    EXPECT_FALSE(per.update().updated);                // less than 0.25 s since the last update
    per.push(s.subspan(1650));
    const PeriodicityUpdate r = per.update();
    EXPECT_TRUE(r.updated);
    ASSERT_TRUE(r.window_s.has_value());
    expect_rel(*r.window_s, 2.0, 1e-6);
    ASSERT_TRUE(r.t_p_s.has_value());
    expect_rel(*r.t_p_s, 0.048, 0.05);
    ASSERT_EQ(per.per_window().size(), 3u);
    for (const auto& w : per.per_window()) EXPECT_TRUE(w.first.has_value());
}

TEST(BankPeriodicityPython, AnUnconfidentEstimateIsNotUsed) {
    BankConfig cfg;
    cfg.periodicity_windows_s = {10.0};
    cfg.comb_confidence_min = 10.0;
    Periodicity per(cfg, kRate);
    per.push(ten_seconds("25_machine"));
    const PeriodicityUpdate r = per.update(true);
    EXPECT_FALSE(r.t_p_s.has_value());
    EXPECT_FALSE(r.window_s.has_value());
    EXPECT_GT(r.confidence, 0.0);
}

}  // namespace
