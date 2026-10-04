// The bank decoder's keying against the prototype's golden values (engine/tests/data/bank/keying.json, from
// training/kz4ap_proto/golden.py golden_keying). The keyer is driven exactly as ChannelDecoder.run drove the
// prototype's: noise.json's stream u (noise_stream.c64, complex64), P = |boxcar(u, N_k)|^2 rounded to float32 (as
// run stores it), the default "spectrum" noise updated once per block, step on each block, and run's recorded
// start_over and
// finish_over_start calls replayed after the block they followed. Edges (keyed sample indices) and `unknown`
// exactly; a_k, the posterior's row sums, weight, amp2 and prev_amp2 to relative 1e-9 (floor 1e-12). The golden run
// is the prototype's at its W_min = 0.8 s for every branch, set explicitly (Plan B's B4a made W_min,k 16.7 nominal
// dits of the branch); the per-branch values in dits are tested after the goldens.
#include "kz4ap/bank/channel.hpp"
#include "kz4ap/bank/keying.hpp"
#include "kz4ap/bank/noise.hpp"
#include "kz4ap/bank/timing.hpp"

#include "golden.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace kz4ap::bank;
using kz4ap::test::expect_close;
using kz4ap::test::load_golden;
using kz4ap::test::powers;
using kz4ap::test::stream;

// The golden encoding of an edge: n + 1 for a key-down after sample n, -(n + 1) for a key-up.
int signed_edge(const std::pair<std::int64_t, bool>& e) {
    return static_cast<int>(e.second ? e.first + 1 : -(e.first + 1));
}

Matrix columns(const Matrix& P, int n0, int n1) {
    Matrix out;
    out.rows = P.rows;
    out.cols = n1 - n0;
    out.v.resize(static_cast<std::size_t>(out.rows) * static_cast<std::size_t>(out.cols));
    for (int k = 0; k < P.rows; ++k)
        for (int i = n0; i < n1; ++i) out.at(k, i - n0) = P.at(k, i);
    return out;
}

std::vector<double> row(const Matrix& P, int k, int n0, int n1) {
    std::vector<double> out;
    for (int i = n0; i < n1; ++i) out.push_back(P.at(k, i));
    return out;
}

TEST(BankKeying, HysteresisMatchesPrototype) {
    const auto g = load_golden("keying");
    const auto down = g.at("hysteresis_down").get<std::vector<std::vector<double>>>();
    const auto up = g.at("hysteresis_up").get<std::vector<std::vector<double>>>();
    const auto initial = g.at("hysteresis_initial").get<std::vector<int>>();
    const auto want = g.at("hysteresis").get<std::vector<std::vector<int>>>();
    for (std::size_t r = 0; r < want.size(); ++r) EXPECT_EQ(hysteresis(down[r], up[r], initial[r]), want[r]);
}

TEST(BankKeying, HysteresisOfNoSamplesIsEmpty) {
    EXPECT_TRUE(hysteresis(std::vector<double>{}, std::vector<double>{}, 1).empty());
}

TEST(BankKeying, EdgesAgainstTheStateBefore) {
    Matrix key;
    key.rows = 2;
    key.cols = 4;
    key.v = {1, 1, 0, 1,
             0, 0, 0, 0};
    const auto e = edges(key, {0, 1}, 100);
    ASSERT_EQ(e.size(), 2u);
    EXPECT_EQ(e[0], (std::vector<std::pair<std::int64_t, bool>>{{100, true}, {102, false}, {103, true}}));
    EXPECT_EQ(e[1], (std::vector<std::pair<std::int64_t, bool>>{{100, false}}));
}

TEST(BankKeying, KeyerMatchesPrototypeThroughRun) {
    const auto gn = load_golden("noise");
    const auto g = load_golden("keying");
    const double rate = g.at("rate_hz").get<double>();
    const BankConfig cfg;
    const auto n = branch_samples(branch_lengths_s(cfg), rate);
    const auto u = stream(gn);
    const Matrix P = powers(u, n);
    auto noise = make_noise(cfg, rate, n);
    // the prototype's W_min, 0.8 s of keyed time for every branch (stage 1's value, in seconds)
    BankKeyer keyer(cfg, rate, realized_lengths_s(cfg, rate), std::vector<double>(n.size(), 0.8));
    const int K = static_cast<int>(n.size());

    const int block = g.at("block_samples").get<int>();
    ASSERT_EQ(block, std::max(1, static_cast<int>(std::nearbyint(cfg.block_s * rate))));
    const auto want_unknown = g.at("unknown").get<std::vector<std::string>>();
    const auto sampled = g.at("sampled_blocks").get<std::vector<int>>();
    const auto want_a = g.at("a").get<std::vector<std::vector<double>>>();
    const auto want_psum = g.at("p_sum").get<std::vector<std::vector<double>>>();
    const auto want_weight = g.at("weight").get<std::vector<std::vector<double>>>();
    const auto want_edges = g.at("edges").get<std::vector<std::vector<int>>>();
    const auto& calls = g.at("calls");

    std::vector<std::vector<int>> got_edges(static_cast<std::size_t>(K));
    std::size_t snap = 0, call = 0;
    const int total = static_cast<int>(u.size());
    int b = 0;
    for (int n0 = 0; n0 < total; n0 += block, ++b) {
        const int n1 = std::min(n0 + block, total);
        SCOPED_TRACE("block " + std::to_string(b));
        noise->update(u, P, n0, n1);
        const auto sigma2 = noise->sigma2();
        ASSERT_LT(static_cast<std::size_t>(b), want_unknown.size());
        std::string unknown;
        for (int k = 0; k < K; ++k) unknown += keyer.unknown[static_cast<std::size_t>(k)] ? '1' : '0';
        EXPECT_EQ(unknown, want_unknown[static_cast<std::size_t>(b)]);

        const KeyStep s = keyer.step(columns(P, n0, n1), sigma2);
        ASSERT_EQ(s.key.rows, K);
        ASSERT_EQ(s.key.cols, n1 - n0);
        for (int k = 0; k < K; ++k)
            EXPECT_EQ(keyer.key[static_cast<std::size_t>(k)], static_cast<int>(s.key.at(k, s.key.cols - 1)));
        const auto e = edges(s.key, s.before, n0);
        for (int k = 0; k < K; ++k)
            for (const auto& x : e[static_cast<std::size_t>(k)])
                got_edges[static_cast<std::size_t>(k)].push_back(signed_edge(x));

        if (snap < sampled.size() && sampled[snap] == b) {
            for (int k = 0; k < K; ++k) {
                SCOPED_TRACE("branch " + std::to_string(k));
                const auto kk = static_cast<std::size_t>(k);
                expect_close(s.a[kk], want_a[snap][kk]);
                double psum = 0.0;
                for (int i = 0; i < s.p.cols; ++i) psum += s.p.at(k, i);
                expect_close(psum, want_psum[snap][kk]);
                expect_close(keyer.weight[kk], want_weight[snap][kk]);
            }
            ++snap;
        }

        // run's keyer calls after this block, in run's order, with the state run read before each.
        while (call < calls.size() && calls[call].at(0).get<int>() == b) {
            const auto& c = calls[call];
            const auto name = c.at(1).get<std::string>();
            const int k = c.at(2).get<int>();
            const auto kk = static_cast<std::size_t>(k);
            SCOPED_TRACE(name + " branch " + std::to_string(k));
            const std::size_t at = name == "finish_over_start" ? 5 : 3;
            expect_close(keyer.weight[kk], c.at(at).get<double>());
            expect_close(keyer.amp2[kk], c.at(at + 1).get<double>());
            if (c.at(at + 2).is_null())
                EXPECT_TRUE(std::isnan(keyer.prev_amp2[kk]));
            else
                expect_close(keyer.prev_amp2[kk], c.at(at + 2).get<double>());
            EXPECT_EQ(keyer.ready_to_rekey()[kk], c.at(at + 3).get<bool>());
            if (name == "start_over")
                keyer.start_over(k);
            else
                keyer.finish_over_start(k, c.at(3).get<double>(), c.at(4).get<bool>());
            ++call;
        }
    }
    EXPECT_EQ(b, g.at("blocks").get<int>());
    EXPECT_EQ(snap, sampled.size());
    EXPECT_EQ(call, calls.size());
    for (int k = 0; k < K; ++k) {
        SCOPED_TRACE("branch " + std::to_string(k));
        EXPECT_EQ(got_edges[static_cast<std::size_t>(k)], want_edges[static_cast<std::size_t>(k)]);
    }
}

TEST(BankKeying, RekeyMatchesPrototype) {
    const auto gn = load_golden("noise");
    const auto g = load_golden("keying");
    const double rate = g.at("rate_hz").get<double>();
    const BankConfig cfg;
    const int k = g.at("rekey_branch").get<int>();
    const int n1 = g.at("rekey_n1").get<int>();
    const auto n = branch_samples(branch_lengths_s(cfg), rate);
    const auto u = stream(gn);
    const Matrix P = powers(u, {n[static_cast<std::size_t>(k)]});
    const auto stretch = row(P, 0, 0, n1);
    const double sigma2 = g.at("rekey_sigma2").get<double>();
    const double a_min = g.at("rekey_a_min").get<double>();
    expect_close(BankKeyer(cfg, rate, realized_lengths_s(cfg, rate), bank_timing(cfg).rekey_wait_s)
                     .a_min[static_cast<std::size_t>(k)],
                 a_min);
    const auto amps = g.at("rekey_amp2").get<std::vector<double>>();
    const auto want = g.at("rekey_edges").get<std::vector<std::vector<int>>>();
    for (std::size_t i = 0; i < amps.size(); ++i) {
        SCOPED_TRACE("amp2 " + std::to_string(amps[i]));
        const auto key = rekey(stretch, sigma2, amps[i], cfg, a_min);
        ASSERT_EQ(key.size(), stretch.size());
        Matrix m;
        m.rows = 1;
        m.cols = static_cast<int>(key.size());
        m.v.assign(key.begin(), key.end());
        std::vector<int> got;
        const auto e = edges(m, {0}, 0);  // kept: a range-for over edges(...)[0] would dangle before C++23
        for (const auto& x : e[0]) got.push_back(signed_edge(x));
        EXPECT_EQ(got, want[i]);
    }
}

TEST(BankKeying, XOnNeedsOneValuePerBranch) {
    BankConfig cfg;
    cfg.x_on_values = {4.6, 4.5};
    EXPECT_THROW(BankKeyer(cfg, 1500.0, {0.01, 0.02, 0.03}, {0.8, 0.8, 0.8}), std::invalid_argument);
}

TEST(BankKeying, RekeyWaitNeedsOneValuePerBranch) {
    BankConfig cfg;
    cfg.x_on_values.clear();
    EXPECT_THROW(BankKeyer(cfg, 1500.0, {0.01, 0.02, 0.03}, {0.8, 0.8}), std::invalid_argument);
}

TEST(BankKeying, NominalXOnWithoutCalibratedValues) {
    // x_on,k = sqrt(-2 ln(min(0.5, R_fa L_k))): the nominal value, clamped at sqrt(2 ln 2).
    BankConfig cfg;
    cfg.x_on_values.clear();
    const BankKeyer keyer(cfg, 1500.0, {0.0096, 100.0}, {0.8, 0.8});
    EXPECT_DOUBLE_EQ(keyer.x_on[0], std::sqrt(-2.0 * std::log(0.01 * 0.0096)));
    EXPECT_DOUBLE_EQ(keyer.x_on[1], std::sqrt(2.0 * std::log(2.0)));
    EXPECT_DOUBLE_EQ(keyer.x_off, std::sqrt(-2.0 * std::log(0.3)));
}

// ---- Plan B, B4a: W_min,k in nominal dits of the branch (stage-2 spec section 3.1) ---------------------------

// W_min,k = 16.7 d_k of keyed time and the time-out 2.5 W_min,k = 41.75 d_k of channel time, d_k = L_k / 0.8 with the
// nominal L_k = 9.6 ms x 1.1^(k-1): derived values at 1500 samples/s for k = 1, 16 and 32. W in samples is not
// rounded (the keyed-sample count is compared with it); the seed's memory is round(4 W) samples and the time-out
// round(r x time-out) samples (Python's rounding, ties to even).
TEST(BankKeyingDits, RekeyWaitAndTimeOutPerBranch) {
    const BankConfig cfg;
    const double rate = 1500.0;
    const BankTiming t = bank_timing(cfg);
    ASSERT_EQ(t.rekey_wait_s.size(), 32u);
    ASSERT_EQ(t.rekey_timeout_s.size(), 32u);
    const auto d = branch_dits_s(cfg);
    EXPECT_NEAR(d[0], 0.012, 1e-15);    // 100 WPM
    EXPECT_NEAR(d[15], 0.05013, 1e-5);  // 23.9 WPM
    EXPECT_NEAR(d[31], 0.23033, 1e-5);  // 5.2 WPM
    const BankKeyer keyer(cfg, rate, realized_lengths_s(cfg, rate), t.rekey_wait_s);
    const BankChannel ch(cfg, rate);
    const auto& timeout = ch.rekey_timeout_samples();
    ASSERT_EQ(timeout.size(), 32u);
    struct Want {
        std::size_t k;           // branch index (branch k + 1)
        double wait_s;           // W_min,k = 16.7 d_k, s
        double wait_samples;     // x 1500 samples/s
        int cap;                 // round(4 W), samples
        double timeout_s;        // 2.5 W_min,k, s
        std::int64_t timeout_n;  // round(1500 x time-out), samples
    };
    // d_1 = 12 ms: W_min = 200.4 ms = 300.6 samples, time-out 501.0 ms = 751.5 samples, a tie that the product's
    // last bit decides (751 on this expression; either is within half a sample, so either is accepted). d_16 = 50.13 ms: 837.1 ms = 1255.7 samples, 2.093 s = 3139 samples. d_32 = 230.3 ms:
    // 3.847 s = 5769.8 samples, 9.616 s = 14425 samples.
    const Want want[] = {{0, 0.2004, 300.6, 1202, 0.501, 751},
                         {15, 0.83712, 1255.68, 5023, 2.09280, 3139},
                         {31, 3.84655, 5769.82, 23079, 9.61637, 14425}};
    for (const auto& w : want) {
        SCOPED_TRACE("branch index " + std::to_string(w.k));
        EXPECT_NEAR(t.rekey_wait_s[w.k], w.wait_s, 1e-5);
        EXPECT_NEAR(t.rekey_wait_s[w.k], 16.7 * d[w.k], 1e-15);
        EXPECT_NEAR(keyer.rekey_weight[w.k], w.wait_samples, 0.01);
        EXPECT_EQ(keyer.keyed_cap[w.k], w.cap);
        EXPECT_NEAR(t.rekey_timeout_s[w.k], w.timeout_s, 1e-5);
        EXPECT_NEAR(t.rekey_timeout_s[w.k], 2.5 * 16.7 * d[w.k], 1e-14);
        if (w.k == 0)
            EXPECT_NEAR(static_cast<double>(timeout[w.k]), 751.5, 0.5);  // the tie: 751 or 752
        else
            EXPECT_EQ(timeout[w.k], w.timeout_n);
    }
}

// At 25 WPM (T = 48 ms) the values equal stage 1's (0.8 s, 2 s) within the ladder's granularity: the branch whose d_k
// is nearest 48 ms is k = 16 (50.13 ms; k = 15 has 45.57 ms); a dit inside the ladder's range is within a factor
// 1.1^(1/2) = 1.0488 of the nearest d_k, and 16.7 = 1.002 x 0.8 s / 48 ms, so W_min,16 / 0.8 s and the time-out / 2 s
// lie within a factor 1.0488 x 1.002 = 1.0509 of 1 (derived). Here both are 1.0464 (837.1 ms, 2.093 s).
TEST(BankKeyingDits, At25WpmTheValuesAreStageOnes) {
    const BankConfig cfg;
    const auto d = branch_dits_s(cfg);
    std::size_t nearest = 0;
    for (std::size_t k = 0; k < d.size(); ++k)
        if (std::abs(std::log(d[k] / 0.048)) < std::abs(std::log(d[nearest] / 0.048))) nearest = k;
    EXPECT_EQ(nearest, 15u);
    const BankTiming t = bank_timing(cfg);
    const double bound = std::log(std::sqrt(1.1)) + std::log(16.7 / (0.8 / 0.048));
    EXPECT_LE(std::abs(std::log(t.rekey_wait_s[nearest] / 0.8)), bound);
    EXPECT_LE(std::abs(std::log(t.rekey_timeout_s[nearest] / 2.0)), bound);
    EXPECT_NEAR(t.rekey_wait_s[nearest] / 0.8, 1.0464, 1e-4);
    EXPECT_NEAR(t.rekey_timeout_s[nearest] / 2.0, 1.0464, 1e-4);
}

// ready_to_rekey turns true at the first block whose keyed-sample count reaches that branch's W_min,k: a strong
// carrier keys every sample of every branch, so branch k is ready at the end of the block (32 samples) in which the
// count reaches W_min,k x r.
TEST(BankKeyingDits, ReadyToRekeyAfterTheBranchsOwnWait) {
    const BankConfig cfg;
    const double rate = 1500.0;
    const BankTiming t = bank_timing(cfg);
    BankKeyer keyer(cfg, rate, realized_lengths_s(cfg, rate), t.rekey_wait_s);
    const int K = 32, block = 32;
    const std::vector<double> sigma2(K, 1e-4);
    Matrix P;
    P.rows = K;
    P.cols = block;
    P.v.assign(static_cast<std::size_t>(K * block), 1.0);  // |v|^2 = 1 FS^2: x = 100, above every x_on
    std::vector<int> ready_at(K, -1);
    for (int b = 0; b * block < 8000; ++b) {
        keyer.step(P, sigma2);
        const auto ready = keyer.ready_to_rekey();
        for (int k = 0; k < K; ++k)
            if (ready[static_cast<std::size_t>(k)] && ready_at[static_cast<std::size_t>(k)] < 0)
                ready_at[static_cast<std::size_t>(k)] = (b + 1) * block;
    }
    for (const std::size_t k : {std::size_t{0}, std::size_t{15}, std::size_t{22}, std::size_t{31}}) {
        SCOPED_TRACE("branch index " + std::to_string(k));
        const double w = t.rekey_wait_s[k] * rate;
        EXPECT_EQ(ready_at[k], static_cast<int>(std::ceil(w / block)) * block);
    }
    // branch 23 (d = 97.7 ms, the 12 WPM branch): 16.7 d = 1.631 s = 2447 samples of keyed time, not 0.8 s
    EXPECT_EQ(ready_at[22], 2464);
}

}  // namespace
