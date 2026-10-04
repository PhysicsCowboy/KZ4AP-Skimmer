// The bank decoder's noise estimates against the prototype's golden values
// (engine/tests/data/bank/noise.json, from training/kz4ap_proto/golden.py golden_noise): sigma2 to
// relative 1e-9 (floor 1e-12 absolute), segment counts exactly. The estimates are driven as
// ChannelDecoder.run drives them: P = |boxcar(u, N_k)|^2 rounded to float32 (as run stores it), one update
// per block of round(block_s r) samples, sigma2() after each.
#include "kz4ap/bank/noise.hpp"

#include "golden.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <span>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace kz4ap::bank;
using kz4ap::test::expect_close;
using kz4ap::test::load_golden;

constexpr double kRate = 1500.0;  // samples/s

using kz4ap::test::powers;
using kz4ap::test::stream;

std::vector<int> ladder(double rate_hz) { return branch_samples(branch_lengths_s(BankConfig{}), rate_hz); }

int block_samples(double rate_hz) {
    return std::max(1, static_cast<int>(std::nearbyint(BankConfig{}.block_s * rate_hz)));
}

void check_method(const std::string& method) {
    const auto g = load_golden("noise");
    const auto u = stream(g);
    const auto n = ladder(kRate);
    const Matrix P = powers(u, n);
    BankConfig cfg;
    cfg.noise_method = method;
    auto est = make_noise(cfg, kRate, n);
    const auto want = g.at(method + "_sigma2").get<std::vector<std::vector<double>>>();
    const auto want_n1 = g.at(method + "_n1").get<std::vector<int>>();
    const bool spectrum = method != "branch";
    std::vector<int> want_seg, want_offered;
    if (spectrum) {
        want_seg = g.at(method + "_segments").get<std::vector<int>>();
        want_offered = g.at(method + "_segments_offered").get<std::vector<int>>();
    }
    const int total = static_cast<int>(u.size());
    const int block = block_samples(kRate);
    std::size_t snap = 0;
    int b = 0;
    for (int n0 = 0; n0 < total; n0 += block, ++b) {
        const int n1 = std::min(n0 + block, total);
        est->update(u, P, n0, n1);
        const auto s = est->sigma2();
        if (b % 10 != 9) continue;
        ASSERT_LT(snap, want.size());
        SCOPED_TRACE(method + " block " + std::to_string(b));
        EXPECT_EQ(n1, want_n1[snap]);
        ASSERT_EQ(s.size(), want[snap].size());
        for (std::size_t k = 0; k < s.size(); ++k) expect_close(s[k], want[snap][k]);
        if (spectrum) {
            const auto& sp = dynamic_cast<const SpectrumNoise&>(*est);
            EXPECT_EQ(sp.segments(), want_seg[snap]);
            EXPECT_EQ(sp.segments_offered(), want_offered[snap]);
        }
        ++snap;
    }
    EXPECT_EQ(snap, want.size());
}

TEST(BankNoise, GuardMean) {
    const auto g = load_golden("noise");
    const auto kappa = g.at("guard_mean_kappa").get<std::vector<double>>();
    const auto want = g.at("guard_mean").get<std::vector<double>>();
    for (std::size_t i = 0; i < kappa.size(); ++i) expect_close(guard_mean(kappa[i]), want[i]);
}

TEST(BankNoise, QuantileLinearIsNumpysDefault) {
    // numpy.quantile(x, 0.2): virtual index 0.2 (n - 1), interpolated between its neighbors.
    EXPECT_EQ(quantile_linear({5.0}, 0.2), 5.0);
    EXPECT_DOUBLE_EQ(quantile_linear({4.0, 1.0, 3.0, 2.0, 0.0}, 0.2), 0.8);   // index 0.8
    EXPECT_DOUBLE_EQ(quantile_linear({3.0, 0.0, 1.0, 2.0}, 0.2), 0.6);        // index 0.6
    EXPECT_EQ(quantile_linear({7.0, 9.0}, 1.0), 9.0);
    EXPECT_TRUE(std::isnan(quantile_linear({1.0, std::nan(""), 2.0}, 0.2)));
}

TEST(BankNoise, SpectrumMatchesPrototype) { check_method("spectrum"); }
TEST(BankNoise, SpectrumLevelMatchesPrototype) { check_method("spectrum-level"); }
TEST(BankNoise, BranchFallbackMatchesPrototype) { check_method("branch"); }

// The columns [from, to) of P as a window (column 0 = absolute sample from).
Matrix window_of(const Matrix& P, int from, int to) {
    Matrix w;
    w.rows = P.rows;
    w.cols = to - from;
    w.v.resize(static_cast<std::size_t>(w.rows) * static_cast<std::size_t>(w.cols));
    for (int k = 0; k < P.rows; ++k)
        for (int i = from; i < to; ++i) w.at(k, i - from) = P.at(k, i);
    return w;
}

// Sample indices past 2^31 (a channel open for more than 16.6 days at 1500 samples/s): the same stream with
// every absolute index shifted by 2^31 + 7 (the stream's first sample there), given the whole of P and then
// only a sliding window of recent columns, gives bit-identical estimates after every block.
void check_large_indices(const std::string& method) {
    const auto g = load_golden("noise");
    const auto u = stream(g);
    const auto n = ladder(kRate);
    const Matrix P = powers(u, n);
    BankConfig cfg;
    cfg.noise_method = method;
    const std::int64_t offset = (std::int64_t{1} << 31) + 7;
    auto small = make_noise(cfg, kRate, n);
    auto large = make_noise(cfg, kRate, n, offset);
    auto sliding = make_noise(cfg, kRate, n, offset);
    const int total = static_cast<int>(u.size());
    const int block = block_samples(kRate);
    constexpr int kWindow = 2000;  // samples kept back: more than every look-back of the noise estimates
    for (int n0 = 0; n0 < total; n0 += block) {
        const int n1 = std::min(n0 + block, total);
        small->update(u, P, n0, n1);
        large->update(u, P, offset + n0, offset + n1, offset);
        // the sliding window starts at the stream's start until the warm-up is over, as the channel's does
        const int from = n1 <= kWindow ? 0 : n1 - kWindow;
        const Matrix w = window_of(P, from, n1);
        sliding->update(std::span(u).subspan(static_cast<std::size_t>(from), static_cast<std::size_t>(n1 - from)),
                        w, offset + n0, offset + n1, offset + from);
        const auto s = small->sigma2();
        const auto l = large->sigma2();
        const auto sl = sliding->sigma2();
        ASSERT_EQ(l.size(), s.size());
        for (std::size_t k = 0; k < s.size(); ++k) {
            const bool both_nan = std::isnan(s[k]) && std::isnan(l[k]) && std::isnan(sl[k]);
            ASSERT_TRUE(both_nan || (l[k] == s[k] && sl[k] == s[k])) << method << " block at " << n0 << " branch " << k;
        }
    }
    if (method != "branch") {
        EXPECT_EQ(dynamic_cast<const SpectrumNoise&>(*large).segments(),
                  dynamic_cast<const SpectrumNoise&>(*small).segments());
        EXPECT_EQ(dynamic_cast<const SpectrumNoise&>(*sliding).segments(),
                  dynamic_cast<const SpectrumNoise&>(*small).segments());
        EXPECT_GT(dynamic_cast<const SpectrumNoise&>(*small).segments(), 0);
    }
}

TEST(BankNoise, SampleIndicesPast2To31GiveTheSameEstimates) {
    check_large_indices("spectrum");
    check_large_indices("spectrum-level");
    check_large_indices("branch");
}

TEST(BankNoise, UnknownMethodThrows) {
    BankConfig cfg;
    cfg.noise_method = "nonsense";
    EXPECT_THROW(make_noise(cfg, kRate, ladder(kRate)), std::invalid_argument);
}

TEST(BankNoise, MaskBiasMustMatchTheLadder) {
    BankConfig cfg;
    EXPECT_THROW(SpectrumNoise(cfg, kRate, {14, 15}), std::invalid_argument);
}

// ---- Plan B task B3: exact zeros and the stuck-level recovery ----------------------------------------------

// White complex Gaussian noise of `power` FS^2 per complex sample (power / 2 per real component).
std::vector<std::complex<double>> white(std::size_t samples, double power, std::uint64_t seed) {
    std::vector<std::complex<double>> u(samples);
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> gauss(0.0, std::sqrt(0.5 * power));
    for (auto& x : u) x = {gauss(rng), gauss(rng)};
    return u;
}

// sigma_v,k^2 of white noise of `power` FS^2 per complex sample through branch k's boxcar of N_k samples, per real
// component, FS^2: power / (2 N_k) (derived).
double white_sigma2(double power, int n_k) { return power / (2.0 * n_k); }

// Runs `method` over u block by block, as the channel does, and returns sigma2() after every block, with the
// block's end n1 (an absolute sample index).
struct Snapshot {
    int n1;
    std::vector<double> s;
};
struct NoiseRun {
    std::vector<Snapshot> blocks;
    int recoveries = 0;
    std::int64_t zero_blocks = 0;
};
NoiseRun run_noise(const std::string& method, const std::vector<std::complex<double>>& u, const Matrix& P,
              const BankConfig& base_cfg = BankConfig{}) {
    BankConfig cfg = base_cfg;
    cfg.noise_method = method;
    auto est = make_noise(cfg, kRate, ladder(kRate));
    NoiseRun r;
    const int block = block_samples(kRate);
    for (int n0 = 0; n0 < P.cols; n0 += block) {
        const int n1 = std::min(n0 + block, P.cols);
        est->update(u, P, n0, n1);
        r.blocks.push_back(Snapshot{n1, est->sigma2()});
    }
    r.recoveries = est->recoveries();
    r.zero_blocks = est->zero_blocks();
    return r;
}

// 5 s of exact zeros, then 5 s of white noise (1 FS^2 per complex sample).
std::vector<std::complex<double>> zeros_then_noise() {
    const auto half = static_cast<std::size_t>(5 * kRate);
    std::vector<std::complex<double>> u(half);
    const auto noise = white(half, 1.0, 3);
    u.insert(u.end(), noise.begin(), noise.end());
    return u;
}

// (i) Review Focus 3 (Plan A): no NaN or Inf, and every sigma2 finite and positive after the warm-up. Plan A
// kept this DISABLED (the prototype fails it: section 8c, "Exact zeros"); Plan B's B3 enables it. "After the
// warm-up" now means from 0.32 s (noise_warmup_s) after the first non-zero input, not from the stream's
// start, because exact zeros are missing data and the warm-up runs on non-zero input; before the first block
// of non-zero input sigma2 is unknown (NaN), as before the first block of any stream, and the channel keys
// nothing then (BankChannel.LeadingExactZerosDecodeAsTheStationAlone). At the end (5 s of noise) every branch's
// estimate is within a factor 2 of the noise's sigma_v,k^2 = 0.5 / N_k FS^2.
TEST(BankNoise, ExactZerosThenNoiseStayFiniteAndPositive) {
    const auto u = zeros_then_noise();
    const auto n = ladder(kRate);
    const Matrix P = powers(u, n);
    const int first = static_cast<int>(5 * kRate);  // the first non-zero sample
    const int warmup = static_cast<int>(std::nearbyint(BankConfig{}.noise_warmup_s * kRate));
    for (const char* method : {"spectrum", "spectrum-level", "branch"}) {
        const NoiseRun r = run_noise(method, u, P);
        for (const auto& b : r.blocks) {
            for (double s : b.s) {
                if (b.n1 <= first) {
                    ASSERT_TRUE(std::isnan(s)) << method << " at n1 = " << b.n1 << ": unknown before any input";
                    continue;
                }
                ASSERT_TRUE(std::isfinite(s)) << method << " at n1 = " << b.n1;
                if (b.n1 >= first + warmup) ASSERT_GT(s, 0.0) << method << " at n1 = " << b.n1;
            }
        }
        const auto& last = r.blocks.back().s;
        for (std::size_t k = 0; k < n.size(); ++k) {
            const double want = white_sigma2(1.0, n[k]);
            EXPECT_GT(last[k], 0.5 * want) << method << " branch " << k;
            EXPECT_LT(last[k], 2.0 * want) << method << " branch " << k;
        }
        EXPECT_EQ(r.zero_blocks, first / block_samples(kRate)) << method;  // 234 whole blocks of zeros
        EXPECT_EQ(r.recoveries, 0) << method;
    }
}

// (ii) Noise, 5 s of exact zeros, then noise at the same level: the gap is missing data. sigma2 is finite
// throughout and does not change during the gap, bit for bit, once the spectrum has examined the last segment
// holding samples from before the gap (a segment is examined when the block's end has passed it by
// N_1 - 1 + R samples, R = round(guard_margin_s r) = 30; so from M + N_1 + R + one block after the gap's start
// on; the three-tap level from the gap's first block on). After the gap every estimate stays within a factor 2
// of the value it held during the gap (the gap's zeros pull nothing down). (Against the noise's own
// sigma_v,k^2 = 0.5 / N_k FS^2 the "branch" fallback's branch k = 24 reads 0.34 to 0.47 of it here with or
// without the gap, seeds 21 and 22: the three-tap estimate's slow rise from a low warm-up, not the gap.)
TEST(BankNoise, AGapOfExactZerosLeavesTheEstimateUnchanged) {
    const auto five = static_cast<std::size_t>(5 * kRate);
    auto u = white(five, 1.0, 21);
    const std::size_t gap0 = u.size();
    u.resize(u.size() + five, {0.0, 0.0});
    const auto after = white(five, 1.0, 22);
    u.insert(u.end(), after.begin(), after.end());
    const auto n = ladder(kRate);
    const Matrix P = powers(u, n);
    const int block = block_samples(kRate);
    const BankConfig cfg;
    const int M = static_cast<int>(std::nearbyint(cfg.segment_s * kRate));
    const int R = static_cast<int>(std::nearbyint(cfg.guard_margin_s * kRate));
    for (const char* method : {"spectrum", "spectrum-level", "branch"}) {
        const std::string m = method;
        const NoiseRun r = run_noise(method, u, P);
        const int settle = m == "branch" ? block : M + n[0] + R + block;
        const Snapshot* held = nullptr;
        for (const auto& b : r.blocks) {
            for (double s : b.s) ASSERT_TRUE(std::isfinite(s) && s > 0.0) << m << " at n1 = " << b.n1;
            const bool in_gap = b.n1 >= static_cast<int>(gap0) + settle && b.n1 <= static_cast<int>(gap0 + five);
            if (!in_gap) continue;
            if (!held) {
                held = &b;
                continue;
            }
            for (std::size_t k = 0; k < n.size(); ++k)
                ASSERT_EQ(b.s[k], held->s[k]) << m << " at n1 = " << b.n1 << " branch " << k;
        }
        ASSERT_NE(held, nullptr);
        for (const auto& b : r.blocks) {
            if (b.n1 <= static_cast<int>(gap0 + five)) continue;
            for (std::size_t k = 0; k < n.size(); ++k) {
                ASSERT_GT(b.s[k], 0.5 * held->s[k]) << m << " at n1 = " << b.n1 << " branch " << k;
                ASSERT_LT(b.s[k], 2.0 * held->s[k]) << m << " at n1 = " << b.n1 << " branch " << k;
            }
        }
        EXPECT_EQ(r.recoveries, 0) << m;
    }
}

// (iii) Noise stepping up by 60 dB (the noise power per complex sample after the step relative to before: 1e-6 to
// 1 FS^2), e.g. a band change or a receiver gain step. Without the recovery the estimate stays where it was:
// every tap of branch 1 now exceeds kappa 2 sigma^2 (acceptance about 1e-17 per tap at rho = 1e-6, section 8c).
// Derived expectation: branch 1's last accepted tap is just before the step, so the recovery fires at the first
// block end at or after step + noise_stuck_s (<= 8 s + one block); its quantile window, the last noise_warmup_s =
// 0.32 s of input, then holds only the new noise, so every branch restarts from a 480-sample warm-up estimate of
// the new level, whose error the three-tap update reduces as e^(-t / tau_n): by a further noise_warmup_s + 2 tau_n
// to e^-2 = 0.135 of itself (a factor 2 is then met unless that warm-up estimate is more than 1 + e^2 = 8.4 times
// too high or more than 2 times too low). The spectrum-level shape follows after branch 1 recovers (segments enter
// again) with beta per segment, i.e. tau_n, from 1e-6 of the new level: tau_n ln 2 = 1.39 s to reach half of it.
// Checked: still stuck (below 1e-3 of the new level) 0.5 s before step + noise_stuck_s; within a factor 2 of the
// new sigma_v,k^2 from step + noise_stuck_s + noise_warmup_s + 2 tau_n (12.32 s) to the end (step + 16 s);
// exactly one recovery.
TEST(BankNoise, ANoiseRiseOf60dBRecoversByTheDerivedTime) {
    const BankConfig cfg;
    const int step = static_cast<int>(4 * kRate);
    auto u = white(static_cast<std::size_t>(step), 1e-6, 31);
    const auto after = white(static_cast<std::size_t>(16 * kRate), 1.0, 32);
    u.insert(u.end(), after.begin(), after.end());
    const auto n = ladder(kRate);
    const Matrix P = powers(u, n);
    const int block = block_samples(kRate);
    const int stuck_end = step + static_cast<int>((cfg.noise_stuck_s - 0.5) * kRate);
    const int ready = step + static_cast<int>((cfg.noise_stuck_s + cfg.noise_warmup_s + 2 * cfg.noise_tau_s) * kRate);
    for (const char* method : {"spectrum", "spectrum-level", "branch"}) {
        const std::string m = method;
        const NoiseRun r = run_noise(method, u, P);
        bool checked_stuck = false;
        for (const auto& b : r.blocks) {
            if (b.n1 <= stuck_end && b.n1 + block > stuck_end) {
                for (std::size_t k = 0; k < n.size(); ++k)
                    EXPECT_LT(b.s[k], 1e-3 * white_sigma2(1.0, n[k])) << m << " branch " << k;
                checked_stuck = true;
            }
            if (b.n1 < ready) continue;
            for (std::size_t k = 0; k < n.size(); ++k) {
                const double want = white_sigma2(1.0, n[k]);
                ASSERT_GT(b.s[k], 0.5 * want) << m << " at n1 = " << b.n1 << " branch " << k;
                ASSERT_LT(b.s[k], 2.0 * want) << m << " at n1 = " << b.n1 << " branch " << k;
            }
        }
        EXPECT_TRUE(checked_stuck) << m;
        EXPECT_EQ(r.recoveries, 1) << m;
    }
}

// (iv) A 10 s continuous carrier 40 dB above the noise (A^2 = 1e4 x branch 1's complex noise power 1 / N_1 FS^2;
// the carrier passes every boxcar with gain 1, so in branch k it is 40 dB + 10 log10(N_k / N_1) above the noise),
// then noise. The carrier blocks branch 1 for noise_stuck_s, the recovery sets every branch to the carrier's
// level, and the level decays back with tau_n once the carrier stops. Derived recovery time of the three-tap
// level of branch k (sigma^2 in units of the noise's sigma_v,k^2, written rho):
//   at the recovery rho = (A^2 / sigma_k^2) q_k / (2 (-ln 0.8)), q_k = 1 - 2 z sigma_k / A (z = 0.8416, the 20%
//   point of a Gaussian: the 20% quantile of |A + n|^2 is about A^2 q_k);
//   for the carrier's remaining T_c - noise_stuck_s = 2 s every tap is accepted and rho decays with tau_n toward
//   (A^2 / sigma_k^2) / (2 m(kappa)) (the accepted taps' mean A^2, divided by 2 m); so at the carrier's end
//   rho_end = (A^2 / sigma_k^2) [1/(2m) + (q_k / (2 (-ln 0.8)) - 1/(2m)) e^(-2 s / tau_n)]
//   (branch 1: 2.63e4, 44.2 dB relative to the noise);
//   then every noise tap is accepted and rho decays with tau_n (2.17 dB per s) toward 1/m(kappa) = 1.58 (no
//   truncation while rho is large; with truncation the target is lower, so this is conservative), reaching 2 after
//   t_k = tau_n ln((rho_end - 1/m) / (2 - 1/m)) (branch 1: 22.1 s; branch 32: 28.0 s).
// The spectrum methods' other terms: once branch 1 has recovered, the carrier's segments enter the shape; their
// excess over the noise decays with beta per segment (tau_n) after the carrier, from a carrier-to-noise ratio in
// branch k of at most A^2 / (2 sigma_k^2) (derived bound), i.e. to a factor 2 after at most
// tau_n ln(A^2 / (2 sigma_k^2)) <= t_k. Checked: "branch", every branch k from t_k + 0.25 s; "spectrum" and
// "spectrum-level", branch 1 ("spectrum" branch 1 is the three-tap level itself) from t_1 + 0.25 s and every branch
// from max_k t_k + 0.25 s, to the end (30 s after the carrier). The 0.25 s covers the recovery's block quantization
// and the boxcar's tail after the carrier (at most N_32 / r = 276 / 1500 s = 0.184 s). The three-tap level of
// branch 1 is still above 2 at t_1 - 1 s, so the derived time is not merely a loose bound (measured on Windows:
// 44.2 dB at the carrier's end, 1.96 at 22.0 s).
TEST(BankNoise, ALongCarrierRecoversByTheDerivedTime) {
    const BankConfig cfg;
    const auto n = ladder(kRate);
    const int c0 = static_cast<int>(4 * kRate), c1 = c0 + static_cast<int>(10 * kRate);
    auto u = white(static_cast<std::size_t>(c1 + 30 * kRate), 1.0, 41);
    const double a2 = 1e4 / n[0];  // FS^2
    const double a = std::sqrt(a2);
    for (int i = c0; i < c1; ++i) u[static_cast<std::size_t>(i)] += a;
    const Matrix P = powers(u, n);
    const double m = guard_mean(cfg.noise_guard);
    const double warm = 2.0 * -std::log(0.8);
    const double tau = cfg.noise_tau_s, rest = 10.0 - cfg.noise_stuck_s;  // s
    std::vector<double> t_rec(n.size());
    for (std::size_t k = 0; k < n.size(); ++k) {
        const double s2 = white_sigma2(1.0, n[k]);  // sigma_k^2, FS^2
        const double snr = a2 / s2;
        const double q = 1.0 - 2.0 * 0.8416 * std::sqrt(s2) / a;
        const double rho_end = snr * (1.0 / (2 * m) + (q / warm - 1.0 / (2 * m)) * std::exp(-rest / tau));
        t_rec[k] = tau * std::log((rho_end - 1.0 / m) / (2.0 - 1.0 / m));
    }
    const double t_max = *std::max_element(t_rec.begin(), t_rec.end());
    EXPECT_NEAR(t_rec[0], 22.1, 0.05);
    for (const char* method : {"spectrum", "spectrum-level", "branch"}) {
        const std::string me = method;
        const NoiseRun r = run_noise(method, u, P);
        bool checked_before = false;
        for (const auto& b : r.blocks) {
            const double t = (b.n1 - c1) / kRate;  // s after the carrier
            for (std::size_t k = 0; k < n.size(); ++k) {
                ASSERT_TRUE(std::isfinite(b.s[k]) && b.s[k] > 0.0) << me << " at n1 = " << b.n1;
                const double rho = b.s[k] / white_sigma2(1.0, n[k]);
                if (k == 0 && me != "spectrum-level" && !checked_before && t >= t_rec[0] - 1.0) {
                    EXPECT_GT(rho, 2.0) << me << " at " << t << " s";
                    checked_before = true;
                }
                const bool due = me == "branch" ? t >= t_rec[k] + 0.25
                                                : (k == 0 && t >= t_rec[0] + 0.25) || t >= t_max + 0.25;
                if (!due) continue;
                ASSERT_GT(rho, 0.5) << me << " at " << t << " s, branch " << k;
                ASSERT_LT(rho, 2.0) << me << " at " << t << " s, branch " << k;
            }
        }
        EXPECT_EQ(r.recoveries, 1) << me;
    }
}

// The recovery is gated on branch 1 (the shortest boxcar, which sees noise in every character space up to
// max_wpm): on the noise golden stream (a 25 words/min station keying from 1.0 s to the end, 20 s) the long
// branches of the "branch" fallback accept no tap for more than noise_stuck_s, but branch 1 does, so no method
// recovers (a per-branch rule would have reset those branches to the station's power; Plan B, B3).
TEST(BankNoise, AKeyedStationDoesNotTriggerTheRecovery) {
    const auto g = load_golden("noise");
    const auto u = stream(g);
    const auto n = ladder(kRate);
    const Matrix P = powers(u, n);
    for (const char* method : {"spectrum", "spectrum-level", "branch"}) {
        const NoiseRun r = run_noise(method, u, P);
        EXPECT_EQ(r.recoveries, 0) << method;
        EXPECT_EQ(r.zero_blocks, 0) << method;
    }
}

}  // namespace
