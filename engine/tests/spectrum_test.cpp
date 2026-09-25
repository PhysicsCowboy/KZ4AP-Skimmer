#include "kz4ap/spectrum.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace kz4ap;

namespace {

std::vector<Sample> tone(double freq_hz, double amplitude, int rate, std::size_t n) {
    std::vector<Sample> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double ph = 2 * std::numbers::pi * freq_hz * static_cast<double>(i) / rate;
        x[i] = Sample(static_cast<float>(amplitude * std::cos(ph)), static_cast<float>(amplitude * std::sin(ph)));
    }
    return x;
}

constexpr int kRate = 48000;
constexpr int kN = 1024;

}  // namespace

TEST(SpectrumAnalyzer, TonePeaksAtItsBinWithItsPower) {
    SpectrumAnalyzer sa(kRate, kN, kN / 2);
    const double f = 100.0 * kRate / kN;
    const auto frames = sa.push(tone(f, 0.5, kRate, kN));
    ASSERT_EQ(frames.size(), 1u);
    const auto& p = frames[0].power_db;
    const auto peak = static_cast<int>(std::max_element(p.begin(), p.end()) - p.begin());
    EXPECT_EQ(peak, kN / 2 + 100);
    EXPECT_NEAR(p[peak], 20 * std::log10(0.5), 0.05);
    EXPECT_NEAR(sa.bin_to_hz(peak), f, 1e-9);
}

TEST(SpectrumAnalyzer, NegativeFrequencyIsBelowCenter) {
    SpectrumAnalyzer sa(kRate, kN, kN / 2);
    const auto frames = sa.push(tone(-37.0 * kRate / kN, 0.1, kRate, kN));
    const auto& p = frames.at(0).power_db;
    EXPECT_EQ(std::max_element(p.begin(), p.end()) - p.begin(), kN / 2 - 37);
}

TEST(SpectrumAnalyzer, FrameCountAndTimes) {
    SpectrumAnalyzer sa(kRate, kN, kN / 2);
    const auto frames = sa.push(tone(1000, 0.1, kRate, 3 * kN));
    ASSERT_EQ(frames.size(), 5u);
    EXPECT_DOUBLE_EQ(frames[0].time_s, double(kN) / kRate);
    EXPECT_DOUBLE_EQ(frames[1].time_s, double(kN + kN / 2) / kRate);
}

TEST(SpectrumAnalyzer, ChunkingDoesNotChangeFrames) {
    const auto x = tone(3000, 0.2, kRate, 5 * kN);
    SpectrumAnalyzer whole(kRate, kN, kN / 2);
    const auto expected = whole.push(x);

    SpectrumAnalyzer pieces(kRate, kN, kN / 2);
    std::vector<SpectrumFrame> got;
    for (std::size_t i = 0; i < x.size(); i += 100) {
        const auto n = std::min<std::size_t>(100, x.size() - i);
        for (auto& f : pieces.push(std::span(x).subspan(i, n))) got.push_back(std::move(f));
    }
    ASSERT_EQ(got.size(), expected.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        EXPECT_EQ(got[i].time_s, expected[i].time_s);
        EXPECT_EQ(got[i].power_db, expected[i].power_db);
    }
}
