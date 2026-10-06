// The bank decoder's periodicity estimator (the comb on Pi = 2T) against the prototype
// (training/kz4ap_proto/periodicity.py).
// 1. Golden values (engine/tests/data/bank/periodicity.json, from golden.py golden_periodicity): the comb as
//    ChannelDecoder.run drives it on three keyed streams (12, 25 and 40 WPM, 12 s at 1500 samples/s): the
//    same pushes (branch 1's posterior, block by block) and an update after each; at every recomputation T_P,
//    the window and each window's T exactly (grid points and window lengths are discrete, D2), the
//    confidence and the scores to relative 1e-9 (floor 1e-12).
// 2. The Python comb tests (training/tests/test_proto_periodicity.py), one C++ test per Python test with the
//    same inputs (engine/tests/data/bank/periodicity_cases.json: the key-down intervals and the noise draws,
//    from which keyed_p and the noise are rebuilt as the Python test builds them) and the same assertions; the
//    strict xfail is a GTEST_SKIP with the same reason. The edge-comb and spectrum tests are not ported (those
//    methods are not).
// Both use the prototype's windows, shared by every candidate and in seconds, set explicitly (Plan B's B4a made
// each candidate's window N_w x T, tested in 3).
// 3. Plan B, B4a: each candidate dit T judged over its own window N_w x T.
// 4. Plan B, B4a-C (the "shared" variant, not the default): one window per row, N_w x T-hat, shared by every candidate.
#include "kz4ap/bank/filters.hpp"
#include "kz4ap/bank/periodicity.hpp"
#include "kz4ap/bank/timing.hpp"

#include "golden.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <cstdio>
#include <limits>
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
        EXPECT_EQ(*actual, expected.get<double>());  // grid points and windows: discrete, exact (D2)
    }
}

// Windows in seconds shared by every candidate (the prototype's periodicity_windows_s): one row of one value each.
std::vector<std::vector<double>> shared(const std::vector<double>& windows_s) {
    std::vector<std::vector<double>> rows;
    for (const double w : windows_s) rows.push_back({w});
    return rows;
}

// ProtoConfig(comb_confidence_min=0, ...): every estimate confident (with shared windows).
BankConfig open_config() {
    BankConfig cfg;
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
        Periodicity per(BankConfig{}, rate, shared({2.0, 5.0, 10.0}));  // the prototype's windows, s
        EXPECT_EQ(per.factor(), g.at("factor").get<int>());
        std::vector<int> windows;
        for (const auto& row : per.windows()) {
            ASSERT_EQ(row.size(), 1u);
            windows.push_back(row[0]);
        }
        EXPECT_EQ(windows, g.at("windows_samples").get<std::vector<int>>());
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
        Periodicity per(open_config(), kRate, shared({10.0}));
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
        Periodicity per(open_config(), kRate, shared({10.0}));
        per.push(p);
        scores.push_back(per.update(true).confidence);
    }
    EXPECT_GT(scores[0], scores[1]);
}

TEST(BankPeriodicityPython, TheShortestFullWindowIsUsedAndUpdatesFollowTheInterval) {
    Periodicity per(open_config(), kRate, shared({2.0, 5.0, 10.0}));
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
    cfg.comb_confidence_min = 10.0;
    Periodicity per(cfg, kRate, shared({10.0}));
    per.push(ten_seconds("25_machine"));
    const PeriodicityUpdate r = per.update(true);
    EXPECT_FALSE(r.t_p_s.has_value());
    EXPECT_FALSE(r.window_s.has_value());
    EXPECT_GT(r.confidence, 0.0);
}

// ---------------------------------------------------------------- Plan B, B4a: a window per candidate (a variant
// since B4d)

// The per-candidate variant (periodicity_window_mode = "per_candidate"), the default from B4a until B4d.
BankConfig per_candidate_mode() {
    BankConfig cfg;
    cfg.periodicity_window_mode = "per_candidate";
    return cfg;
}

// The default since B4d (the owner's decision of 2026-10-06): stage 1's windows, 2, 5 and 10 s, the same for every
// candidate (1500, 3750 and 7500 samples at 750 samples/s), computed as the prototype's comb over the most recent
// samples: equal bit for bit to the estimator given those windows explicitly, on 60 s of keyed posteriors pushed block
// by block.
TEST(BankPeriodicitySeconds, TheDefaultWindowsAreStageOnesTwoFiveAndTenSeconds) {
    const BankConfig cfg;
    EXPECT_EQ(cfg.periodicity_window_mode, "seconds");
    const BankTiming t = bank_timing(cfg);
    EXPECT_EQ(t.periodicity_windows_s, (std::vector<std::vector<double>>{{2.0}, {5.0}, {10.0}}));
    EXPECT_TRUE(t.periodicity_window_dits.empty());
    Periodicity def(cfg, kRate);
    Periodicity ref(cfg, kRate, shared({2.0, 5.0, 10.0}));
    ASSERT_EQ(def.windows(), (std::vector<std::vector<int>>{{1500}, {3750}, {7500}}));
    std::vector<double> p;
    for (const char* name : {"5_machine", "12_machine", "25_machine", "40_machine", "25_paddle", "100_machine"}) {
        const auto x = ten_seconds(name);
        p.insert(p.end(), x.begin(), x.end());
    }
    std::size_t updates = 0;
    for (std::size_t i = 0; i < p.size(); i += 32) {
        const std::span<const double> piece(p.data() + i, std::min<std::size_t>(32, p.size() - i));
        def.push(piece);
        ref.push(piece);
        const PeriodicityUpdate a = def.update(), b = ref.update();
        ASSERT_EQ(a.updated, b.updated);
        ASSERT_EQ(a.t_p_s, b.t_p_s);
        ASSERT_EQ(std::memcmp(&a.confidence, &b.confidence, sizeof(double)), 0);
        ASSERT_EQ(a.window_s, b.window_s);
        updates += a.updated ? 1 : 0;
    }
    EXPECT_GT(updates, 200u);
    // the mode needs windows; another mode is refused
    BankConfig none;
    none.periodicity_windows_s.clear();
    EXPECT_THROW(bank_timing(none), std::invalid_argument);
}

// The configuration's windows: N_w x T for each candidate T, N_w = 41.7, 104 and 208, in samples at 750 samples/s
// max(16, round(N_w T 750)): 375 (41.7 x 12 ms), 936 and 1872 samples for the 12 ms candidate; 7576, 18894 and 37789
// for the 242.2 ms one (50.4 s). Every candidate is inside the comb's reach in every window, (4 + 1/2 + 0.075) 2T =
// 9.15 T <= (n - 1) / 2: N_w >= 41.7 > 18.3 (derived; the rounding of n costs at most half a sample).
TEST(BankPeriodicityDits, TheWindowOfACandidateIsNwTimesItsDit) {
    const BankConfig cfg = per_candidate_mode();
    const Periodicity per(cfg, kRate);
    ASSERT_EQ(per.rate_hz(), 750.0);
    const auto& grid = per.grid();
    ASSERT_EQ(grid.size(), 303u);
    ASSERT_EQ(per.windows().size(), 3u);
    const double n_w[] = {41.7, 104.0, 208.0};
    for (std::size_t r = 0; r < 3; ++r) {
        ASSERT_EQ(per.windows()[r].size(), grid.size());
        for (std::size_t g = 0; g < grid.size(); ++g) {
            const int want = std::max(16, static_cast<int>(std::nearbyint(n_w[r] * grid[g] * 750.0)));
            EXPECT_EQ(per.window_samples(r, g), want) << r << ", " << g;
            const double reach = (cfg.comb_teeth + 0.5 + cfg.comb_width) * 2.0 * grid[g] * 750.0;
            EXPECT_LE(reach, (per.window_samples(r, g) - 1) / 2.0) << r << ", " << g;
        }
    }
    EXPECT_EQ(per.window_samples(0, 0), 375);
    EXPECT_EQ(per.window_samples(1, 0), 936);
    EXPECT_EQ(per.window_samples(2, 0), 1872);
    EXPECT_EQ(per.window_samples(0, 302), 7576);
    EXPECT_EQ(per.window_samples(1, 302), 18894);
    EXPECT_EQ(per.window_samples(2, 302), 37789);
}

// Each candidate's score is the comb's score of that candidate alone on its own most recent N_w T samples
// (comb_estimate(recent n samples, rate, {T})): over 60 s of keyed posteriors at five speeds with noise, pushed block
// by block as the channel does (32 samples at 1500 samples/s, an update after each), with one NaN at 5.0 s. Checked
// at every 25th recomputation, every 4th candidate. The slid sums are a different summation from the FFT's: they
// agree to a largest absolute difference of 1e-12 (bound) in the score, a measured margin (the largest measured
// difference is printed; scores are of order 1e-2 to 1). A candidate whose window holds the NaN has no finite score
// (comb_estimate: none), and the row then has no estimate, as the prototype's comb on a window with a NaN; once the
// NaN has left a candidate's window its score is finite again and agrees.
TEST(BankPeriodicityDits, EachCandidateIsJudgedOverItsOwnWindow) {
    const BankConfig cfg = per_candidate_mode();
    Periodicity per(cfg, kRate);
    std::vector<double> p;
    const auto uniform = decode_f64_base64(cases().at("noise_uniform_b64").get<std::string>());
    for (const char* name : {"5_machine", "12_machine", "25_machine", "40_machine", "25_paddle", "100_machine"}) {
        const auto x = ten_seconds(name);
        for (std::size_t i = 0; i < x.size(); ++i) p.push_back(0.9 * x[i] + 0.1 * uniform[i % uniform.size()]);
    }
    p[7500] = std::numeric_limits<double>::quiet_NaN();
    const std::size_t block = 32;
    std::size_t updates = 0, checked = 0, nonfinite = 0;
    double worst = 0.0;
    for (std::size_t i = 0; i < p.size(); i += block) {
        per.push(std::span<const double>(p.data() + i, std::min(block, p.size() - i)));
        if (!per.update().updated || ++updates % 25 != 0) continue;
        const auto& buf = per.buffer();
        for (std::size_t r = 0; r < 3; ++r) {
            const auto& scores = per.candidate_scores()[r];
            ASSERT_EQ(scores.size(), per.grid().size());
            for (std::size_t g = 0; g < per.grid().size(); g += 4) {
                const auto n = static_cast<std::size_t>(per.window_samples(r, g));
                if (buf.size() < n) {
                    EXPECT_EQ(scores[g], -std::numeric_limits<double>::infinity());
                    continue;
                }
                const std::span<const double> recent(buf.data() + (buf.size() - n), n);
                const auto [t, want] = comb_estimate(recent, per.rate_hz(), {per.grid()[g]}, cfg.comb_teeth,
                                                     cfg.comb_width);
                if (!t) {
                    EXPECT_FALSE(std::isfinite(scores[g])) << r << ", " << g;
                    ++nonfinite;
                    continue;
                }
                ++checked;
                worst = std::max(worst, std::abs(scores[g] - want));
                EXPECT_NEAR(scores[g], want, 1e-12) << "row " << r << ", T " << per.grid()[g] << " s, update " << updates;
            }
            // the row's estimate: the first maximum of the candidates' scores, none if a NaN is among them
            std::optional<double> pick;
            double best = -std::numeric_limits<double>::infinity();
            bool nan = false;
            for (std::size_t g = 0; g < scores.size(); ++g) {
                if (std::isnan(scores[g])) nan = true;
                if (!nan && scores[g] > best) {
                    best = scores[g];
                    pick = per.grid()[g];
                }
            }
            if (nan || !std::isfinite(best)) pick.reset();
            EXPECT_EQ(per.per_window()[r].first, pick) << "row " << r << ", update " << updates;
        }
    }
    std::printf("[ info ] %zu candidate scores checked, %zu without a finite score; largest difference %.3e\n", checked,
                nonfinite, worst);
    EXPECT_GT(checked, 1000u);
    EXPECT_GT(nonfinite, 0u);  // the NaN was inside some windows
}

// The shortest confident window gives T_P, now per candidate: a 25 WPM stream (48 ms) is found in the shortest window
// (41.7 T = 2.0 s), whose window_s is that of the chosen candidate.
TEST(BankPeriodicityDits, TheShortestConfidentWindowGivesTp) {
    const BankConfig cfg = per_candidate_mode();
    Periodicity per(cfg, kRate);
    per.push(ten_seconds("25_machine"));
    const PeriodicityUpdate r = per.update(true);
    ASSERT_TRUE(r.t_p_s.has_value());
    expect_rel(*r.t_p_s, 0.048, 0.05);
    ASSERT_TRUE(r.window_s.has_value());
    EXPECT_NEAR(*r.window_s, 41.7 * *r.t_p_s, 1.0 / 750.0);
}

// A per-candidate row of the wrong size is refused; one value per row is a shared window.
TEST(BankPeriodicityDits, AWindowRowNeedsOneValueOrOnePerCandidate) {
    const BankConfig cfg;
    EXPECT_THROW(Periodicity(cfg, kRate, {{1.0, 2.0}}), std::invalid_argument);
    EXPECT_THROW(Periodicity(cfg, kRate, {}), std::invalid_argument);
    EXPECT_NO_THROW(Periodicity(cfg, kRate, {{2.0}}));
    EXPECT_EQ(bank_timing(cfg).periodicity_windows_s.size(), 3u);
}

// ---------------------------------------------------------------- Plan B, B4a-C: one window per row, N_w x T-hat

BankConfig shared_mode() {
    BankConfig cfg;
    cfg.periodicity_window_mode = "shared";
    return cfg;
}

Periodicity shared_dits_periodicity(const BankConfig& cfg) {
    const BankTiming t = bank_timing(cfg);
    return Periodicity(cfg, kRate, t.periodicity_windows_s, t.periodicity_window_dits);
}

// 60 s of keyed posteriors at six speeds with noise (as EachCandidateIsJudgedOverItsOwnWindow, without the NaN).
std::vector<double> sixty_seconds() {
    std::vector<double> p;
    const auto uniform = decode_f64_base64(cases().at("noise_uniform_b64").get<std::string>());
    for (const char* name : {"5_machine", "12_machine", "25_machine", "40_machine", "25_paddle", "100_machine"}) {
        const auto x = ten_seconds(name);
        for (std::size_t i = 0; i < x.size(); ++i) p.push_back(0.9 * x[i] + 0.1 * uniform[i % uniform.size()]);
    }
    return p;
}

// The configuration's timing in the shared mode: rows of stage 1's windows in seconds (before a selection) and N_w per
// row; the default mode ("seconds") and the per-candidate variant have no N_w list; the shared mode does not use
// periodicity_windows_s (since B4d no longer an override); another mode is refused.
TEST(BankPeriodicityShared, TheTimingOfTheSharedMode) {
    const BankTiming t = bank_timing(shared_mode());
    EXPECT_EQ(t.periodicity_windows_s, (std::vector<std::vector<double>>{{2.0}, {5.0}, {10.0}}));
    EXPECT_EQ(t.periodicity_window_dits, (std::vector<double>{41.7, 104.0, 208.0}));
    EXPECT_TRUE(bank_timing(BankConfig{}).periodicity_window_dits.empty());
    EXPECT_EQ(Periodicity(shared_mode(), kRate).window_dits(), t.periodicity_window_dits);  // the configuration's
    EXPECT_TRUE(bank_timing(per_candidate_mode()).periodicity_window_dits.empty());
    EXPECT_EQ(BankConfig{}.periodicity_window_mode, "seconds");
    BankConfig over = shared_mode();
    over.periodicity_windows_s = {3.0};
    EXPECT_EQ(bank_timing(over).periodicity_windows_s, t.periodicity_windows_s);
    EXPECT_EQ(bank_timing(over).periodicity_window_dits, t.periodicity_window_dits);
    BankConfig bad;
    bad.periodicity_window_mode = "per-candidate";
    EXPECT_THROW(bank_timing(bad), std::invalid_argument);
    // windows that follow a dit need as many shared rows in seconds
    EXPECT_THROW(Periodicity(BankConfig{}, kRate, {{2.0}, {5.0}}, {41.7, 104.0, 208.0}), std::invalid_argument);
    const auto per_candidate = bank_timing(per_candidate_mode()).periodicity_windows_s;
    EXPECT_THROW(Periodicity(BankConfig{}, kRate, per_candidate, {41.7, 104.0, 208.0}), std::invalid_argument);
}

// T-hat's cap: the longest branch's realized length (k = 32) over 0.8 dits, times exp(eligibility_tolerance) = 1.1:
// about 0.253 s; the buffer holds 208 x the cap, about 52.6 s at 750 samples/s.
TEST(BankPeriodicityShared, TheBufferHoldsTheLongestWindowAtTheCap) {
    const BankConfig cfg = shared_mode();
    Periodicity per = shared_dits_periodicity(cfg);
    const std::vector<int> n = branch_samples(branch_lengths_s(cfg), kRate);
    EXPECT_NEAR(per.dit_cap_s(), n.back() / kRate / 0.8 * 1.1, 1e-15);
    std::printf("[ info ] T-hat cap %.6f s (branch 32: %d samples)\n", per.dit_cap_s(), n.back());
    EXPECT_EQ(per.window_dits(), (std::vector<double>{41.7, 104.0, 208.0}));
    const auto p = sixty_seconds();
    per.push(p);
    per.update(true, 10.0);  // above the cap: capped
    const int want = static_cast<int>(std::nearbyint(208.0 * per.dit_cap_s() * 750.0));
    EXPECT_GE(per.buffer().size(), static_cast<std::size_t>(want));
    EXPECT_EQ(per.used_windows()[2], want);
}

// Every candidate of a row is scored on the same samples: each row's estimate is comb_estimate (every candidate of the
// grid) on its most recent max(16, round(N_w T-hat 750)) samples, bit for bit; the window follows T-hat as it changes
// (none for the first 10 s: stage 1's windows of 1500, 3750 and 7500 samples; then 48 ms, 100 ms, 12 ms and 240 ms);
// and a candidate is in the comb's reach of its row's window ((4 + 1/2 + 0.075) 2T <= (n - 1) / 2, so T <= about
// N_w T-hat / 18.3), so the 41.7 T-hat window cannot pick 3 T-hat. T_P is the shortest confident row's estimate.
TEST(BankPeriodicityShared, EveryCandidateOfARowIsScoredOnTheSameWindow) {
    const BankConfig cfg = shared_mode();
    Periodicity per = shared_dits_periodicity(cfg);
    const auto p = sixty_seconds();
    const std::size_t block = 32;
    const double stage1[] = {2.0, 5.0, 10.0};
    std::size_t checked = 0, estimates = 0;
    for (std::size_t i = 0; i < p.size(); i += block) {
        const double t = static_cast<double>(i) / kRate;
        std::optional<double> dit;
        if (t >= 10.0) dit = t < 20.0 ? 0.048 : t < 30.0 ? 0.100 : t < 45.0 ? 0.012 : 0.240;
        per.push(std::span<const double>(p.data() + i, std::min(block, p.size() - i)));
        const PeriodicityUpdate upd = per.update(false, dit);
        if (!upd.updated) continue;
        const auto& buf = per.buffer();
        bool found = false;
        for (std::size_t r = 0; r < 3; ++r) {
            const int n = dit ? std::max(16, static_cast<int>(std::nearbyint(per.window_dits()[r] * *dit * 750.0)))
                              : static_cast<int>(std::nearbyint(stage1[r] * 750.0));
            if (buf.size() < static_cast<std::size_t>(n)) {
                EXPECT_EQ(per.used_windows()[r], 0);
                EXPECT_FALSE(per.per_window()[r].first.has_value());
                continue;
            }
            ASSERT_EQ(per.used_windows()[r], n) << "row " << r << " at " << t << " s";
            const std::span<const double> recent(buf.data() + (buf.size() - static_cast<std::size_t>(n)),
                                                 static_cast<std::size_t>(n));
            const auto want = comb_estimate(recent, per.rate_hz(), per.grid(), cfg.comb_teeth, cfg.comb_width);
            EXPECT_EQ(per.per_window()[r].first, want.first) << "row " << r << " at " << t << " s";
            EXPECT_EQ(per.per_window()[r].second, want.second) << "row " << r << " at " << t << " s";
            ++checked;
            if (!want.first) continue;
            ++estimates;
            const double reach = (cfg.comb_teeth + 0.5 + cfg.comb_width) * 2.0 * *want.first * 750.0;
            EXPECT_LE(reach, (n - 1) / 2.0);
            if (r == 0 && dit) EXPECT_LT(*want.first, 2.3 * *dit);  // 41.7 / 18.3 = 2.28
            if (!found && want.second >= cfg.comb_confidence_min) {
                found = true;
                ASSERT_TRUE(upd.t_p_s.has_value());
                EXPECT_EQ(*upd.t_p_s, *want.first);
                ASSERT_TRUE(upd.window_s.has_value());
                EXPECT_EQ(*upd.window_s, n / 750.0);
            }
        }
        if (!found) EXPECT_FALSE(upd.t_p_s.has_value());
    }
    std::printf("[ info ] %zu rows checked, %zu with an estimate\n", checked, estimates);
    EXPECT_GT(checked, 500u);  // 239 recomputations x 3 rows, less those not filled yet
    EXPECT_GT(estimates, 500u);
}

// Before any selection (no T-hat) the shared mode is stage 1's estimator: the same estimates as Periodicity with
// stage 1's windows in seconds, bit for bit, over 60 s pushed block by block.
TEST(BankPeriodicityShared, BeforeASelectionTheWindowsAreStageOnes) {
    const BankConfig cfg = shared_mode();
    Periodicity per = shared_dits_periodicity(cfg);
    Periodicity ref(BankConfig{}, kRate, shared({2.0, 5.0, 10.0}));
    const auto p = sixty_seconds();
    std::size_t compared = 0;
    for (std::size_t i = 0; i < p.size(); i += 32) {
        const std::span<const double> x(p.data() + i, std::min<std::size_t>(32, p.size() - i));
        per.push(x);
        ref.push(x);
        const PeriodicityUpdate a = per.update();
        const PeriodicityUpdate b = ref.update();
        ASSERT_EQ(a.updated, b.updated);
        EXPECT_EQ(a.t_p_s, b.t_p_s);
        EXPECT_EQ(a.confidence, b.confidence);
        EXPECT_EQ(a.window_s, b.window_s);
        EXPECT_EQ(per.per_window(), ref.per_window());
        compared += a.updated ? 1 : 0;
    }
    EXPECT_GT(compared, 200u);
}

}  // namespace
