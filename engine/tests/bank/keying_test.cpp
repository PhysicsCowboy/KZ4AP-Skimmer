// The bank decoder's keying against the prototype's golden values (engine/tests/data/bank/keying.json, from
// training/kz4ap_proto/golden.py golden_keying). The keyer is driven exactly as ChannelDecoder.run drove the
// prototype's: noise.json's stream u (noise_stream.c64, complex64), P = |boxcar(u, N_k)|^2 rounded to float32 (as run stores it), the
// default "spectrum" noise updated once per block, step on each block, and run's recorded start_over and
// finish_over_start calls replayed after the block they followed. Edges (keyed sample indices) and `unknown`
// exactly; a_k, the posterior's row sums, weight, amp2 and prev_amp2 to relative 1e-9 (floor 1e-12).
#include "kz4ap/bank/keying.hpp"
#include "kz4ap/bank/noise.hpp"

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
    BankKeyer keyer(cfg, rate, realized_lengths_s(cfg, rate));
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
    expect_close(BankKeyer(cfg, rate, realized_lengths_s(cfg, rate)).a_min[static_cast<std::size_t>(k)], a_min);
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
    EXPECT_THROW(BankKeyer(cfg, 1500.0, {0.01, 0.02, 0.03}), std::invalid_argument);
}

TEST(BankKeying, NominalXOnWithoutCalibratedValues) {
    // x_on,k = sqrt(-2 ln(min(0.5, R_fa L_k))): the nominal value, clamped at sqrt(2 ln 2).
    BankConfig cfg;
    cfg.x_on_values.clear();
    const BankKeyer keyer(cfg, 1500.0, {0.0096, 100.0});
    EXPECT_DOUBLE_EQ(keyer.x_on[0], std::sqrt(-2.0 * std::log(0.01 * 0.0096)));
    EXPECT_DOUBLE_EQ(keyer.x_on[1], std::sqrt(2.0 * std::log(2.0)));
    EXPECT_DOUBLE_EQ(keyer.x_off, std::sqrt(-2.0 * std::log(0.3)));
}

}  // namespace
