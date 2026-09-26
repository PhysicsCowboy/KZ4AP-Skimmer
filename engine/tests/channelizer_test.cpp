#include "kz4ap/channelizer.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace kz4ap;

namespace {

constexpr int kRate = 192000;
constexpr int kN = 8192;
constexpr double kBinHz = double(kRate) / kN;
constexpr std::size_t kWarmup = 96;  // output samples to skip while the filter fills

std::vector<Sample> tone(double freq_hz, double amplitude, std::size_t n) {
    std::vector<Sample> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double ph = 2 * std::numbers::pi * freq_hz * static_cast<double>(i) / kRate;
        x[i] = Sample(static_cast<float>(amplitude * std::cos(ph)), static_cast<float>(amplitude * std::sin(ph)));
    }
    return x;
}

std::map<std::uint32_t, std::vector<Sample>> run(Channelizer& ch, const std::vector<Sample>& x,
                                                 std::vector<std::uint64_t>* first_indices = nullptr) {
    std::map<std::uint32_t, std::vector<Sample>> out;
    ch.push(x, [&](std::uint32_t id, std::uint64_t first, std::span<const Sample> s) {
        out[id].insert(out[id].end(), s.begin(), s.end());
        if (first_indices && id == 1) first_indices->push_back(first);
    });
    return out;
}

}  // namespace

TEST(Channelizer, OutputRateAndBinConversions) {
    Channelizer ch({});
    EXPECT_DOUBLE_EQ(ch.output_rate(), 1500.0);
    EXPECT_EQ(ch.hz_to_bin(1000.0), 43);
    EXPECT_EQ(ch.hz_to_bin(-96000.0), -4096);
    EXPECT_EQ(ch.hz_to_bin(96000.0), 4095);
    EXPECT_DOUBLE_EQ(ch.bin_to_hz(43), 43 * kBinHz);
}

TEST(Channelizer, ToneOnCenterComesOutSteadyAtItsAmplitude) {
    Channelizer ch({});
    ch.add_channel(1, 201);
    std::vector<std::uint64_t> firsts;
    const auto out = run(ch, tone(201 * kBinHz, 0.3, kRate), &firsts);
    const auto& y = out.at(1);
    ASSERT_EQ(y.size(), 1500u - 1500u % 32u);
    for (std::size_t i = kWarmup; i < y.size(); ++i) {
        EXPECT_NEAR(std::abs(y[i]), 0.3, 0.3 * 0.005) << i;
        if (i + 1 < y.size()) EXPECT_NEAR(std::arg(y[i + 1] / y[i]), 0.0, 1e-3) << i;
    }
    ASSERT_GE(firsts.size(), 2u);
    EXPECT_EQ(firsts[0], 0u);
    EXPECT_EQ(firsts[1], 32u);
}

TEST(Channelizer, OffsetToneRotatesContinuouslyAcrossBlocks) {
    Channelizer ch({});
    ch.add_channel(1, 201);  // odd bin: exercises the per-block phase correction
    const auto y = run(ch, tone(201 * kBinHz + 5.0, 0.3, kRate)).at(1);
    const double step = 2 * std::numbers::pi * 5.0 / 1500.0;
    for (std::size_t i = kWarmup; i + 1 < y.size(); ++i) {
        EXPECT_NEAR(std::arg(y[i + 1] / y[i]), step, 1e-3) << i;
    }
}

TEST(Channelizer, PassbandIsFlatWithinHalfABin) {
    Channelizer ch({});
    ch.add_channel(1, 201);
    const auto y = run(ch, tone(201 * kBinHz + 11.0, 0.3, kRate)).at(1);
    for (std::size_t i = kWarmup; i < y.size(); ++i) EXPECT_NEAR(std::abs(y[i]), 0.3, 0.3 * 0.012) << i;
}

TEST(Channelizer, RejectsToneOutsidePassband) {
    Channelizer ch({});
    ch.add_channel(1, 201);
    const auto y = run(ch, tone(201 * kBinHz + 400.0, 0.3, kRate)).at(1);
    for (std::size_t i = kWarmup; i < y.size(); ++i) EXPECT_LT(std::abs(y[i]), 0.3 * 1e-3) << i;
}

TEST(Channelizer, SeparatesTwoChannels) {
    Channelizer ch({});
    ch.add_channel(1, 100);
    ch.add_channel(2, -300);
    auto x = tone(100 * kBinHz, 0.2, kRate);
    const auto x2 = tone(-300 * kBinHz, 0.05, kRate);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += x2[i];
    const auto out = run(ch, x);
    for (std::size_t i = kWarmup; i < out.at(1).size(); ++i) {
        EXPECT_NEAR(std::abs(out.at(1)[i]), 0.2, 0.2 * 0.005);
        EXPECT_NEAR(std::abs(out.at(2)[i]), 0.05, 0.05 * 0.005);
    }
}

TEST(Channelizer, ChannelNearBandEdgeWrapsSafely) {
    Channelizer ch({});
    const int bin = kN / 2 - 5;
    ch.add_channel(1, bin);
    const auto y = run(ch, tone(bin * kBinHz, 0.3, kRate)).at(1);
    for (std::size_t i = kWarmup; i < y.size(); ++i) EXPECT_NEAR(std::abs(y[i]), 0.3, 0.3 * 0.005) << i;
}

TEST(Channelizer, RemovedChannelStopsReceiving) {
    Channelizer ch({});
    ch.add_channel(1, 10);
    ch.add_channel(2, 20);
    ch.remove_channel(1);
    EXPECT_EQ(ch.channel_count(), 1u);
    const auto out = run(ch, tone(10 * kBinHz, 0.1, kRate / 10));
    EXPECT_EQ(out.count(1), 0u);
    EXPECT_EQ(out.count(2), 1u);
}

TEST(Channelizer, RejectsInvalidParameters) {
    EXPECT_THROW(Channelizer({.fft_size = 0}), std::invalid_argument);
    EXPECT_THROW(Channelizer({.fft_size = 8191}), std::invalid_argument);
    EXPECT_THROW(Channelizer({.channel_bins = 0}), std::invalid_argument);
    EXPECT_THROW(Channelizer({.fft_size = 8192, .channel_bins = 100}), std::invalid_argument);
    EXPECT_THROW(Channelizer({.sample_rate = 0}), std::invalid_argument);
    EXPECT_THROW(Channelizer({.cutoff_hz = 0.0}), std::invalid_argument);
    EXPECT_THROW(Channelizer({.cutoff_hz = -10.0}), std::invalid_argument);
    EXPECT_THROW(Channelizer({.cutoff_hz = 750.0}), std::invalid_argument);
    EXPECT_THROW(Channelizer({.cutoff_hz = std::numeric_limits<double>::quiet_NaN()}), std::invalid_argument);
    EXPECT_THROW(Channelizer({.cutoff_hz = std::numeric_limits<double>::infinity()}), std::invalid_argument);
}

TEST(Channelizer, TransitionBandMustFitInsideTheOutputBand) {
    // Default output rate is 1500 Hz (half: 750 Hz). The 4097-tap Blackman filter's
    // transition width is about 5.5 * 192000 / 4097 = 258 Hz, so the cutoff may be
    // at most about 750 - 129 = 621 Hz. 700 Hz is below 750 Hz (the old, -6 dB check)
    // but its stopband edge, about 829 Hz, is not.
    EXPECT_THROW(Channelizer({.cutoff_hz = 700.0}), std::invalid_argument);
    EXPECT_THROW(Channelizer({.cutoff_hz = 622.0}), std::invalid_argument);
    EXPECT_NO_THROW(Channelizer({.cutoff_hz = 620.0}));
    EXPECT_NO_THROW(Channelizer({}));
    // The engine's defaults at other rates: 48 kHz (N = 2048) and 44.1 kHz (N = 2048).
    EXPECT_NO_THROW(Channelizer({.sample_rate = 48000, .fft_size = 2048}));
    EXPECT_NO_THROW(Channelizer({.sample_rate = 44100, .fft_size = 2048}));
}
