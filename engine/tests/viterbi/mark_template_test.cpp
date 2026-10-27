#include "kz4ap/viterbi/mark_template.hpp"

#include "keyed_signals.hpp"
#include "kz4ap/channelizer.hpp"
#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/front_filter.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::viterbi;
namespace signals = kz4ap::test;

namespace {

constexpr double kChannelRate = 1500.0;            // samples/s
constexpr double kDelta = 4.0 / kChannelRate;      // s: the sample interval, 2.667 ms
constexpr double kRiseS = 0.0025;                  // s: the template's 10-90% rise
constexpr double kRiseMinS = 0.0008;               // s: the transmitters' range of rises
constexpr double kRiseMaxS = 0.0047;               // s
constexpr int kSpan = TemplateTables::kSpan;

// The engine's chain at an input rate: the channel filter's taps, the decimation and the front filter's taps.
struct Chain {
    std::vector<double> g;
    double fs;
    int decimation;
    std::vector<double> h;
};

Chain chain_at(int sample_rate, int fft_size) {
    ChannelizerConfig config;
    config.sample_rate = sample_rate;
    config.fft_size = fft_size;
    return {channel_filter_taps(config), static_cast<double>(sample_rate), fft_size / config.channel_bins,
            front_filter_taps(kChannelRate, 0.012, kRiseS)};
}

const Chain& chain192() {
    static const Chain c = chain_at(192000, 8192);
    return c;
}

TemplateTables tables_of(const Chain& c, double rise_min_s = kRiseMinS, double rise_max_s = kRiseMaxS) {
    return make_template_tables(c.g, c.fs, c.decimation, c.h, 4, kRiseS, rise_min_s, rise_max_s);
}

const TemplateTables& tables192() {
    static const TemplateTables t = tables_of(chain192());
    return t;
}

std::vector<double> response(const Chain& c, double rise_s, double offset_s) {
    return step_response(c.g, c.fs, c.decimation, c.h, rise_s, offset_s);
}

// Element j of a step response, 1 past its end (the response's support).
double at(const std::vector<double>& s, long long j) {
    EXPECT_GE(j, 0);
    return j < static_cast<long long>(s.size()) ? s[static_cast<std::size_t>(j)] : 1.0;
}

// The full length of a raised cosine of 10-90% time rise_s (s).
double edge_length(double rise_s) { return rise_s / signals::raised_cosine_10_90(); }

// The shift that aligns a rise of rise_s with the template at its half-amplitude point (s): its rise starts this long
// after the template's key-down.
double alignment(double rise_s) { return (edge_length(kRiseS) - edge_length(rise_s)) / 2.0; }

// The 79 rise times of the tables.
double rise_time(int i, double rise_min_s = kRiseMinS, double rise_max_s = kRiseMaxS) {
    return rise_min_s + (rise_max_s - rise_min_s) * static_cast<double>(i) / (kTemplateRiseCount - 1);
}

// A table's row in the log, j = 0 ... 15: the template (s_j) to 6 decimals, an edge term (v_j) to 3 significant
// digits.
void log_row(const char* name, const std::array<double, kSpan>& row, bool template_row) {
    std::cout << name << ":";
    for (double x : row) {
        if (template_row) {
            std::cout << " " << std::fixed << std::setprecision(6) << x;
        } else {
            std::cout << " " << std::scientific << std::setprecision(2) << x;
        }
    }
    std::cout << std::defaultfloat << std::setprecision(6) << "\n";
}

// The time (s) of the first upward crossing of level by the samples s[m] at m / rate, linearly interpolated.
double crossing_s(const std::vector<double>& s, double rate, double level) {
    for (std::size_t m = 1; m < s.size(); ++m) {
        if (s[m - 1] < level && s[m] >= level) {
            return (static_cast<double>(m - 1) + (level - s[m - 1]) / (s[m] - s[m - 1])) / rate;
        }
    }
    ADD_FAILURE() << "no crossing of " << level;
    return 0.0;
}

}  // namespace

TEST(MarkTemplate, MatchesTheDesign) {
    const TemplateTables& t = tables192();
    log_row("s_j", t.s, true);
    log_row("v_j", t.v, false);
    log_row("v_position_j", t.v_position, false);
    const std::array<double, 9> s = {0.0505, 0.2126, 0.4334, 0.6590, 0.8661, 0.9825, 1.0023, 0.9999, 0.9999};
    for (int j = 5; j <= 13; ++j) EXPECT_NEAR(t.s[j], s[static_cast<std::size_t>(j - 5)], 0.0002) << "s_" << j;
    for (int j = 14; j <= 15; ++j) EXPECT_NEAR(t.s[j], 1.0, 1e-4) << "s_" << j;
    const std::array<double, 5> s_low = {0.00000, 0.00001, 0.00016, -0.00064, -0.00080};
    for (int j = 0; j <= 4; ++j) EXPECT_NEAR(t.s[j], s_low[static_cast<std::size_t>(j)], 0.00002) << "s_" << j;

    const std::array<double, 9> v = {1.0e-3, 3.6e-3, 4.2e-3, 4.2e-3, 2.6e-3, 3.4e-4, 2.7e-6, 1.4e-7, 2.9e-9};
    for (int j = 5; j <= 13; ++j) {
        const double want = v[static_cast<std::size_t>(j - 5)];
        EXPECT_NEAR(t.v[j], want, 0.06 * want) << "v_" << j;
    }
    const std::array<double, 5> v_low = {8e-14, 3.1e-10, 7.8e-9, 5.8e-7, 2.8e-5};
    for (int j = 0; j <= 4; ++j) {
        const double want = v_low[static_cast<std::size_t>(j)];
        EXPECT_NEAR(t.v[j], want, 0.12 * want) << "v_" << j;
    }
    for (int j = 14; j <= 15; ++j) EXPECT_LT(t.v[j], 3e-11) << "v_" << j;

    const std::array<double, 9> vp = {9.5e-4, 3.6e-3, 4.2e-3, 4.2e-3, 2.6e-3, 2.7e-4, 6.4e-7, 6.5e-8, 3.2e-9};
    for (int j = 5; j <= 13; ++j) {
        const double want = vp[static_cast<std::size_t>(j - 5)];
        EXPECT_NEAR(t.v_position[j], want, 0.06 * want) << "v_position_" << j;
    }

    // The tables' fast route against the direct chain (step_response) averaged over the 512 offsets u = i / f_s.
    const Chain& c = chain192();
    const int positions = 4 * c.decimation;
    ASSERT_EQ(positions, 512);
    std::array<double, kSpan> mean{};
    std::array<double, kSpan> mean_square{};
    for (int i = 0; i < positions; ++i) {
        const auto r = response(c, kRiseS, static_cast<double>(i) / c.fs);
        for (int j = 0; j < kSpan; ++j) {
            const double x = at(r, 4 * j);
            mean[j] += x;
            mean_square[j] += (x - t.s[j]) * (x - t.s[j]);
        }
    }
    for (int j = 0; j < kSpan; ++j) {
        EXPECT_NEAR(t.s[j], mean[j] / positions, 1e-12) << "s_" << j;
        EXPECT_NEAR(t.v_position[j], mean_square[j] / positions, 1e-9 * t.v_position[j] + 1e-24) << "v_position_" << j;
        // v_j is c[j][j] plus the mean over rises of the variance over positions, so at least c[j][j] (>= 0).
        EXPECT_GE(t.c[j][j], 0.0) << j;
        EXPECT_GE(t.v[j], t.c[j][j]) << j;
        for (int k = 0; k < kSpan; ++k) EXPECT_EQ(t.c[j][k], t.c[k][j]) << j << ", " << k;
    }
}

TEST(MarkTemplate, StepResponseFigures) {
    const Chain& c = chain192();
    // S on the input grid, S(m / f_s) for m = j D + i, from the responses at the D offsets i / f_s.
    std::vector<std::vector<double>> by_offset;
    for (int i = 0; i < c.decimation; ++i) by_offset.push_back(response(c, kRiseS, static_cast<double>(i) / c.fs));
    const std::size_t n = by_offset.back().size() - 1;
    std::vector<double> grid(n * static_cast<std::size_t>(c.decimation));
    for (std::size_t m = 0; m < grid.size(); ++m) {
        grid[m] = by_offset[m % static_cast<std::size_t>(c.decimation)][m / static_cast<std::size_t>(c.decimation)];
    }
    const double half = crossing_s(grid, c.fs, 0.5);
    const double rise_10_90 = crossing_s(grid, c.fs, 0.9) - crossing_s(grid, c.fs, 0.1);
    const double rise_1_99 = crossing_s(grid, c.fs, 0.99) - crossing_s(grid, c.fs, 0.01);
    std::cout << "measured: half amplitude " << half * 1e3 << " ms after key-down; 10-90% rise " << rise_10_90 * 1e3
              << " ms; 1-99% rise " << rise_1_99 * 1e3 << " ms; tables' half point " << tables192().half_point_s * 1e3
              << " ms\n";
    EXPECT_NEAR(half, 0.02079, 0.00002);
    EXPECT_NEAR(rise_10_90, 0.01003, 0.00002);
    EXPECT_NEAR(rise_1_99, 0.01473, 0.00003);
    EXPECT_NEAR(tables192().half_point_s, 0.02079, 0.00002);
    EXPECT_NEAR(tables192().half_point_s, half, 1e-7);

    // S = 1 within 10^-12 from 16 samples (64 channel samples) after the key-down, at every offset within a sample.
    double worst = 0.0;
    for (int i = 0; i < 4 * c.decimation; ++i) {
        const auto r = response(c, kRiseS, static_cast<double>(i) / c.fs);
        ASSERT_GT(r.size(), 4u * kSpan) << i;
        for (std::size_t j = 4 * kSpan; j < r.size(); ++j) worst = std::max(worst, std::abs(r[j] - 1.0));
    }
    std::cout << "measured: largest |S - 1| from 16 samples on " << worst << "\n";
    EXPECT_LE(worst, 1e-12);
    // Before the key-down it is 0.
    const auto late = response(c, kRiseS, -0.010);  // key-down 10 ms after channel sample 0
    for (std::size_t j = 0; j < 15; ++j) EXPECT_EQ(late[j], 0.0) << j;
}

TEST(MarkTemplate, DotPeaks) {
    const Chain& c = chain192();
    auto peak = [&](double dot_s) {
        double best = -1.0;
        for (int i = 0; i < c.decimation; ++i) {
            const double u = static_cast<double>(i) / c.fs;
            const auto on = response(c, kRiseS, u);
            const auto off = response(c, kRiseS, u - dot_s);
            const auto n = static_cast<long long>(std::max(on.size(), off.size()));
            for (long long j = 0; j < n; ++j) best = std::max(best, at(on, j) - at(off, j));
        }
        return best;
    };
    const double p100 = peak(0.012);
    const double p80 = peak(0.015);
    const double p25 = peak(0.048);
    std::cout << "measured: dot peaks 12 ms " << p100 << ", 15 ms " << p80 << ", 48 ms " << p25 << " of full\n";
    EXPECT_NEAR(p100, 0.900, 0.002);
    EXPECT_NEAR(p80, 0.984, 0.002);
    // The design's "slower dots at 1.00" is to two places: the response overshoots by 0.32% of full (the channel
    // filter's ripple; s_11 = 1.0023 is a mean of S over a sample, so S's largest value is at least that), and a 48 ms
    // dot's peak is that overshoot (derived; plan 1b's 1.000 +/- 0.001 cannot hold, reported in Task 4).
    EXPECT_NEAR(p25, 1.003, 0.001);
}

TEST(MarkTemplate, MatchesTheChainOnKeyedCarriers) {
    // Part 15, item 1: noise-free D17 keying through the Channelizer and the EnvelopeSampler against S. The expected e_k
    // puts channel sample n at input time n / r (step_response's reference), so an offset in the channelizer's time
    // reference shows here.
    const Chain& c = chain192();
    ChannelizerConfig config;  // 192 kHz, 8 192-point FFT, 64 bins: 1 500 samples/s
    constexpr int kBin = 40;   // 937.5 Hz, on a bin's center
    const double freq_hz = kBin * c.fs / config.fft_size;
    constexpr double kDurationS = 0.5;
    double worst = 0.0;
    for (int i = 0; i < 8; ++i) {
        const double u = (i + 0.5) * kDelta / 8.0;  // s: the claimed sample lies u after the keyed edge
        const double mark_on = 30 * kDelta - u;     // a 48 ms mark
        const double dot_on = 80 * kDelta - u;      // a 12 ms dot
        const std::vector<std::pair<double, double>> intervals = {{mark_on, mark_on + 0.048}, {dot_on, dot_on + 0.012}};
        const auto x = signals::keyed_signal_d17(intervals, c.fs, kDurationS, freq_hz);
        Channelizer channelizer(config);
        channelizer.add_channel(1, kBin);
        EnvelopeSampler sampler(c.h, 4);
        std::vector<EnvelopeSample> out;
        channelizer.push(x, [&](std::uint32_t, std::uint64_t first, std::span<const Sample> samples) {
            sampler.push(samples, first, out);
        });
        ASSERT_GT(out.size(), 150u);
        // Each edge's response S(t - t_edge) at the channel samples from the stream's start.
        std::vector<std::pair<double, std::vector<double>>> edges;
        for (const auto& [on, off] : intervals) {
            edges.emplace_back(1.0, response(c, kRiseS, -on));
            edges.emplace_back(-1.0, response(c, kRiseS, -off));
        }
        for (std::size_t n = 0; n < out.size(); ++n) {
            ASSERT_EQ(out[n].k, static_cast<std::int64_t>(n));
            double s = 0.0;
            for (const auto& [sign, r] : edges) s += sign * at(r, 4 * out[n].k);
            const double diff = std::abs(out[n].e_fs - std::abs(s));
            worst = std::max(worst, diff);
            EXPECT_LE(diff, 1e-4) << "u = " << u * 1e3 << " ms, k = " << out[n].k;
        }
    }
    std::cout << "measured: largest |e_k - |s(t_k)|| " << worst << " of full over 8 edge positions\n";
}

TEST(MarkTemplate, OtherRisesMisfit) {
    const Chain& c = chain192();
    // The largest |difference| (of full) between a rise of rise_s aligned at half amplitude and the template, over a
    // mark of mark_s at every input-grid position.
    auto misfit = [&](double rise_s, double mark_s) {
        const double shift = alignment(rise_s);
        double worst = 0.0;
        for (int i = 0; i < c.decimation; ++i) {
            const double u = static_cast<double>(i) / c.fs;
            const auto t_on = response(c, kRiseS, u);
            const auto t_off = response(c, kRiseS, u - mark_s);
            const auto r_on = response(c, rise_s, u - shift);
            const auto r_off = response(c, rise_s, u - mark_s - shift);
            const auto n = static_cast<long long>(std::max({t_on.size(), t_off.size(), r_on.size(), r_off.size()}));
            for (long long j = 0; j < n; ++j) {
                const double d = (at(r_on, j) - at(r_off, j)) - (at(t_on, j) - at(t_off, j));
                worst = std::max(worst, std::abs(d));
            }
        }
        return worst;
    };
    const double fast_dot = misfit(0.0008, 0.012);
    const double fast_mark = misfit(0.0008, 0.048);
    const double slow_dot = misfit(0.0047, 0.012);
    const double slow_mark = misfit(0.0047, 0.048);
    std::cout << "measured: misfit at 0.8 ms " << fast_mark * 100 << "% (48 ms mark), " << fast_dot * 100
              << "% (12 ms dot); at 4.7 ms " << slow_mark * 100 << "%, " << slow_dot * 100 << "% of full\n";
    EXPECT_NEAR(fast_mark * 100, 0.78, 0.05);
    EXPECT_NEAR(fast_dot * 100, 1.56, 0.05);
    EXPECT_NEAR(slow_mark * 100, 1.92, 0.05);
    EXPECT_NEAR(slow_dot * 100, 3.83, 0.05);
}

TEST(MarkTemplate, SlowRisesEndWithinTheSpan) {
    // The aligned 4.7 ms rise runs past 16 samples; what the tables drop there (header, known limitations).
    const Chain& c = chain192();
    const double shift = alignment(kRiseMaxS);
    double worst = 0.0;
    for (int i = 0; i < 4 * c.decimation; ++i) {
        const auto r = response(c, kRiseMaxS, static_cast<double>(i) / c.fs - shift);
        ASSERT_GT(r.size(), 4u * kSpan) << i;
        for (std::size_t j = 4 * kSpan; j < r.size(); ++j) worst = std::max(worst, std::abs(r[j] - 1.0));
    }
    std::cout << "measured: aligned 4.7 ms rise, largest |S - 1| from 16 samples on " << worst << " of full\n";
    EXPECT_LT(worst, 1e-10);
}

namespace {

// One claimed edge: its sample and its sign (+1 key-down, -1 key-up).
struct Edge {
    long long sample;
    double sign;
};

// The template and edge term at sample k computed from every edge since the start (edges alternate from a key-down):
// an edge 16 or more samples back adds exactly its sign.
TemplateValue direct_value(const std::vector<Edge>& edges, long long k, const TemplateTables& t) {
    double s = 0.0;
    double v = 0.0;
    std::vector<Edge> recent;
    for (const Edge& e : edges) {
        const long long age = k - e.sample;
        if (age < 0) continue;
        if (age >= kSpan) {
            s += e.sign;
        } else {
            s += e.sign * t.s[static_cast<std::size_t>(age)];
            v += t.v[static_cast<std::size_t>(age)];
            recent.push_back(e);
        }
    }
    for (std::size_t a = 0; a < recent.size(); ++a) {
        for (std::size_t b = a + 1; b < recent.size(); ++b) {
            v += 2.0 * recent[a].sign * recent[b].sign *
                 t.c[static_cast<std::size_t>(k - recent[a].sample)][static_cast<std::size_t>(k - recent[b].sample)];
        }
    }
    return {s, v};
}

std::vector<Edge> alternating(const std::vector<long long>& samples) {
    std::vector<Edge> edges;
    for (std::size_t i = 0; i < samples.size(); ++i) edges.push_back({samples[i], i % 2 == 0 ? 1.0 : -1.0});
    return edges;
}

}  // namespace

TEST(MarkTemplate, SixteenEdgesInTheHistory) {
    const TemplateTables& t = tables192();
    for (const int count : {40, 41}) {  // ending with a key-up, and with a key-down
        SCOPED_TRACE(std::to_string(count) + " edges");
        std::vector<long long> samples;
        for (int k = 0; k < count; ++k) samples.push_back(k);  // alternating 1-sample marks and gaps
        const auto edges = alternating(samples);
        EdgeHistory h;
        for (long long k = 0; k < count + 30; ++k) {
            advance(h, k < count);
            const TemplateValue got = template_value(h, t);
            const TemplateValue want = direct_value(edges, k, t);
            EXPECT_NEAR(got.s, want.s, 1e-14) << k;
            EXPECT_NEAR(got.v, want.v, 1e-12 * std::abs(want.v) + 1e-30) << k;
            // key_down_before is the key state 16 samples back.
            const long long back = k - kSpan;
            const bool down_then = back >= 0 && back < count && back % 2 == 0;
            const bool down_long_before = back >= count && count % 2 == 1;
            EXPECT_EQ(h.key_down_before, down_then || down_long_before) << k;
        }
        // The last edge 16 samples back adds exactly 0 after a key-up and exactly 1 after a key-down.
        EdgeHistory g;
        for (long long k = 0; k <= count - 1 + kSpan; ++k) advance(g, k < count);
        EXPECT_EQ(g.recent, 0u);
        const TemplateValue end = template_value(g, t);
        EXPECT_EQ(end.s, count % 2 == 0 ? 0.0 : 1.0);
        EXPECT_EQ(end.v, 0.0);
    }
}

namespace {

// The responses of a rise of rise_s aligned at half amplitude, at `positions` midpoints within the sample (u = (p + 1/2)
// Delta / positions), from step_response (the direct chain); kept across cases.
const std::vector<std::vector<double>>& grid_responses(const Chain& c, double rise_s, int positions) {
    static std::map<std::pair<double, int>, std::vector<std::vector<double>>> cache;
    auto& responses = cache[{rise_s, positions}];
    if (responses.empty()) {
        for (int p = 0; p < positions; ++p) {
            responses.push_back(response(c, rise_s, (p + 0.5) * kDelta / positions - alignment(rise_s)));
        }
    }
    return responses;
}

// The mean of (constant + sum over e of f[e][p_e])^2 over every combination of the p_e (0, 1, ... 4 dimensions).
double grid_mean_square(double constant, const std::vector<std::vector<double>>& f) {
    if (f.empty()) return constant * constant;
    const std::size_t np = f[0].size();
    double sum = 0.0;
    auto innermost = [&](double partial, const std::vector<double>& last) {
        for (const double x : last) sum += (partial + x) * (partial + x);
    };
    if (f.size() == 1) {
        innermost(constant, f[0]);
    } else if (f.size() == 2) {
        for (const double a : f[0]) innermost(constant + a, f[1]);
    } else if (f.size() == 3) {
        for (const double a : f[0])
            for (const double b : f[1]) innermost(constant + a + b, f[2]);
    } else if (f.size() == 4) {
        for (const double a : f[0])
            for (const double b : f[1])
                for (const double q : f[2]) innermost(constant + a + b + q, f[3]);
    } else {
        ADD_FAILURE() << "more than 4 edges";
    }
    return sum / std::pow(static_cast<double>(np), static_cast<double>(f.size()));
}

// The mean square deviation of the combined template about the combined 2.5 ms template at the samples k = 0 ... last,
// computed directly: over every combination of the positions of the edges within the last 16 samples (64 grid
// positions per edge for up to 3 such edges, 16 for 4) and over the rise times, each edge's response from
// step_response. An edge 16 or more samples back is taken at its mean over the 64 positions (exactly 1 for rises up to
// 3.80 ms, within 3 x 10^-12 for slower ones: MarkTemplate.SlowRisesEndWithinTheSpan), an edge not yet keyed adds 0.
// combined: the 2.5 ms template at each sample (template_value's s).
std::vector<double> direct_edge_term(const Chain& c, const std::vector<Edge>& edges, long long last,
                                     const std::vector<double>& rises, const std::vector<double>& combined) {
    std::vector<double> out(static_cast<std::size_t>(last + 1), 0.0);
    for (const double rise : rises) {
        const auto& fine = grid_responses(c, rise, 64);
        for (long long k = 0; k <= last; ++k) {
            double constant = -combined[static_cast<std::size_t>(k)];
            std::vector<const Edge*> active;
            for (const Edge& e : edges) {
                const long long age = k - e.sample;
                if (age < 0) continue;
                if (age < kSpan) {
                    active.push_back(&e);
                    continue;
                }
                double mean = 0.0;
                for (const auto& r : fine) mean += at(r, 4 * age);
                constant += e.sign * mean / static_cast<double>(fine.size());
            }
            const auto& grid = active.size() <= 3 ? fine : grid_responses(c, rise, 16);
            std::vector<std::vector<double>> f;
            for (const Edge* e : active) {
                std::vector<double> row;
                for (const auto& r : grid) row.push_back(e->sign * at(r, 4 * (k - e->sample)));
                f.push_back(std::move(row));
            }
            out[static_cast<std::size_t>(k)] += grid_mean_square(constant, f);
        }
    }
    for (double& x : out) x /= static_cast<double>(rises.size());
    return out;
}

}  // namespace

TEST(MarkTemplate, OverlappingEdgesAreExact) {
    const Chain& c = chain192();
    const TemplateTables& t = tables192();
    const TemplateTables single = tables_of(c, kRiseS, kRiseS);  // every rise at 2.5 ms
    // With every rise at 2.5 ms, m(j, r) = s_j: the pair term is the rounding of a mean's deviation from itself
    // (below 10^-15 of full, so its square below 10^-30), and v_j is v_position_j.
    for (int a = 0; a < kSpan; ++a) {
        for (int b = 0; b < kSpan; ++b) EXPECT_LE(std::abs(single.c[a][b]), 1e-30) << a << ", " << b;
        EXPECT_NEAR(single.v[a], single.v_position[a], 1e-12 * single.v_position[a]) << a;
    }
    std::vector<double> rises;
    for (int i = 0; i < kTemplateRiseCount; ++i) rises.push_back(rise_time(i));
    struct Case {
        const char* name;
        std::vector<long long> samples;  // the claimed edges, alternating from a key-down
    };
    const std::vector<Case> cases = {{"a 100 WPM dot claimed as 4 samples", {2, 6}},
                                     {"a 100 WPM dot claimed as 5 samples", {2, 7}},
                                     {"two dots with a 1-sample gap", {2, 6, 7, 11}},
                                     {"three edges within 16 samples", {2, 9, 14}}};
    for (const Case& cs : cases) {
        SCOPED_TRACE(cs.name);
        const auto edges = alternating(cs.samples);
        const long long last = cs.samples.back() + kSpan + 1;
        for (const bool all_at_template : {false, true}) {
            SCOPED_TRACE(all_at_template ? "every rise at 2.5 ms" : "79 rises over 0.8 to 4.7 ms");
            const TemplateTables& tables = all_at_template ? single : t;
            std::vector<double> combined;
            std::vector<double> got;
            std::vector<double> without_pairs;
            EdgeHistory h;
            for (long long k = 0; k <= last; ++k) {
                advance(h, std::find(cs.samples.begin(), cs.samples.end(), k) != cs.samples.end());
                const TemplateValue value = template_value(h, tables);
                combined.push_back(value.s);
                got.push_back(value.v);
                double sum = 0.0;
                for (const long long e : cs.samples) {
                    if (k >= e && k - e < kSpan) sum += tables.v[static_cast<std::size_t>(k - e)];
                }
                without_pairs.push_back(sum);
            }
            const auto want =
                direct_edge_term(c, edges, last, all_at_template ? std::vector<double>{kRiseS} : rises, combined);
            double largest_pair_share = 0.0;
            double largest_error = 0.0;  // |got - want| / want where v > 10^-8
            for (long long k = 0; k <= last; ++k) {
                const auto i = static_cast<std::size_t>(k);
                if (want[i] > 1e-8 || got[i] > 1e-8) {
                    EXPECT_NEAR(got[i], want[i], 0.01 * want[i]) << "k = " << k;
                    largest_error = std::max(largest_error, std::abs(got[i] - want[i]) / want[i]);
                    largest_pair_share =
                        std::max(largest_pair_share, std::abs(got[i] - without_pairs[i]) / got[i]);
                } else {
                    EXPECT_NEAR(got[i], want[i], 1e-10) << "k = " << k;
                }
                if (all_at_template) EXPECT_NEAR(got[i], without_pairs[i], 1e-12 * got[i] + 1e-30) << "k = " << k;
            }
            std::cout << "measured: " << cs.name << (all_at_template ? ", every rise at 2.5 ms" : "")
                      << ": the pair terms' largest share of v " << largest_pair_share
                      << ", largest relative difference from the direct computation " << largest_error << "\n";
        }
    }
}

TEST(MarkTemplate, TheFallIsExactlyZeroFrom16Samples) {
    const TemplateTables& t = tables192();
    EdgeHistory h;
    constexpr int kMark = 100;  // samples: a long mark
    for (int k = 0; k <= kMark + 30; ++k) {
        advance(h, k == 0 || k == kMark);
        const TemplateValue value = template_value(h, t);
        if (k >= kSpan && k < kMark) {
            EXPECT_EQ(value.s, 1.0) << k;
            EXPECT_EQ(value.v, 0.0) << k;
        }
        if (k < kMark) continue;
        const int j = k - kMark;
        if (j <= 10) EXPECT_GE(value.s, 0.01) << j;
        if (j <= 15) EXPECT_NE(value.s, 0.0) << j;
        if (j >= 16) {
            EXPECT_EQ(value.s, 0.0) << j;
            EXPECT_EQ(value.v, 0.0) << j;
        }
    }
}

TEST(MarkTemplate, ClaimedEdgeIsTheFirstSampleAtOrAfterTheKeyedEdge) {
    for (const long long k : {0LL, 1LL, 7LL, 37LL, 1000LL, 123457LL, 1000000LL}) {
        const double kd = static_cast<double>(k);
        EXPECT_EQ(claimed_edge_sample(kd * kDelta, kDelta), k) << k;
        EXPECT_EQ(claimed_edge_sample(kd * kDelta + 1e-9, kDelta), k + 1) << k;
        EXPECT_EQ(claimed_edge_sample((kd + 1) * kDelta - 1e-9, kDelta), k + 1) << k;
    }
    EXPECT_THROW(claimed_edge_sample(0.1, 0.0), std::invalid_argument);
    EXPECT_THROW(claimed_edge_sample(std::nan(""), kDelta), std::invalid_argument);
}

TEST(MarkTemplate, At48kHz) {
    const Chain c48 = chain_at(48000, 2048);
    ASSERT_EQ(c48.g.size(), 1025u);
    const TemplateTables t48 = tables_of(c48);
    log_row("48 kHz s_j", t48.s, true);
    log_row("48 kHz v_j", t48.v, false);
    log_row("48 kHz v_position_j", t48.v_position, false);
    std::cout << "48 kHz half point " << t48.half_point_s * 1e3 << " ms\n";
    EXPECT_NEAR(t48.half_point_s, tables192().half_point_s, 0.0001);  // s
    EXPECT_NEAR(t48.s[kSpan - 1], 1.0, 1e-3);
    double worst = 0.0;
    for (int j = 0; j < kSpan; ++j) worst = std::max(worst, std::abs(t48.s[j] - tables192().s[j]));
    std::cout << "measured: largest |s_j(48 kHz) - s_j(192 kHz)| " << worst << " of full\n";
    EXPECT_LE(worst, 0.01);
}

TEST(MarkTemplate, RejectsBadArguments) {
    const Chain& c = chain192();
    const std::vector<double> none;
    EXPECT_THROW(step_response(none, c.fs, c.decimation, c.h, kRiseS, 0.0), std::invalid_argument);
    EXPECT_THROW(step_response(c.g, c.fs, c.decimation, none, kRiseS, 0.0), std::invalid_argument);
    EXPECT_THROW(step_response(c.g, 0.0, c.decimation, c.h, kRiseS, 0.0), std::invalid_argument);
    EXPECT_THROW(step_response(c.g, c.fs, 0, c.h, kRiseS, 0.0), std::invalid_argument);
    EXPECT_THROW(step_response(c.g, c.fs, c.decimation, c.h, 0.0, 0.0), std::invalid_argument);
    EXPECT_THROW(step_response(c.g, c.fs, c.decimation, c.h, kRiseS, std::nan("")), std::invalid_argument);
    EXPECT_THROW(make_template_tables(none, c.fs, c.decimation, c.h, 4, kRiseS, kRiseMinS, kRiseMaxS),
                 std::invalid_argument);
    EXPECT_THROW(make_template_tables(c.g, c.fs, c.decimation, c.h, 0, kRiseS, kRiseMinS, kRiseMaxS),
                 std::invalid_argument);
    EXPECT_THROW(make_template_tables(c.g, c.fs, c.decimation, c.h, 4, kRiseS, kRiseMaxS, kRiseMinS),
                 std::invalid_argument);
    EXPECT_THROW(make_template_tables(c.g, c.fs, c.decimation, c.h, 4, kRiseS, 0.0, kRiseMaxS), std::invalid_argument);
}

TEST(MarkTemplate, BuildsQuickly) {
    // Label measurement (the test MarkTemplate.Measurement of engine/CMakeLists.txt sets KZ4AP_MEASUREMENT=1; skipped
    // otherwise): make_template_tables at 192 kHz under 1 s on the desktop (heuristic bound).
    const char* flag = std::getenv("KZ4AP_MEASUREMENT");
    if (flag == nullptr || std::string(flag) != "1") GTEST_SKIP() << "runs under the measurement label";
    const Chain& c = chain192();
    const auto start = std::chrono::steady_clock::now();
    const TemplateTables t = tables_of(c);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "measured: make_template_tables at 192 kHz took " << seconds << " s (s_8 = " << t.s[8] << ")\n";
    EXPECT_LT(seconds, 1.0);
}
