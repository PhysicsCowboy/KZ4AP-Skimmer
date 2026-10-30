#include "waterfall.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::app;

TEST(DbScale, MapsTheFloorAndTheTopAndClampsOutside) {
    const DbScale scale{-80.0, 50.0};
    EXPECT_DOUBLE_EQ(scale.level(-80.0), 0.0);
    EXPECT_DOUBLE_EQ(scale.level(-30.0), 1.0);
    EXPECT_DOUBLE_EQ(scale.level(-55.0), 0.5);
    EXPECT_DOUBLE_EQ(scale.level(-200.0), 0.0);
    EXPECT_DOUBLE_EQ(scale.level(0.0), 1.0);
}

TEST(DbScale, ARangeBelowTheMinimumDoesNotDivideByZero) {
    const DbScale scale{-80.0, 0.0};
    EXPECT_DOUBLE_EQ(scale.level(-79.5), 0.5);  // kMinRangeDb = 1 dB
}

TEST(ReduceToColumns, KeepsANarrowCarrierAtAnyReduction) {
    std::vector<float> bins(2048, -70.0f);
    bins[1500] = -30.0f;
    std::vector<float> columns(300);
    reduce_to_columns(bins, columns);
    const auto expected_column = 1500 * columns.size() / bins.size();
    EXPECT_FLOAT_EQ(columns[expected_column], -30.0f);
    for (std::size_t c = 0; c < columns.size(); ++c) {
        if (c != expected_column) EXPECT_FLOAT_EQ(columns[c], -70.0f);
    }
}

TEST(ReduceToColumns, WithMoreColumnsThanBinsEachColumnTakesItsBin) {
    const std::vector<float> bins = {1.0f, 2.0f};
    std::vector<float> columns(4);
    reduce_to_columns(bins, columns);
    EXPECT_EQ(columns, (std::vector<float>{1.0f, 1.0f, 2.0f, 2.0f}));
}

TEST(Palette, GoesFromDarkToWhiteAndClamps) {
    const auto brightness = [](std::uint32_t c) { return ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255); };
    const auto& palette = waterfall_palette();
    EXPECT_EQ(palette.back(), 0xFFFFFFFFu);
    for (std::size_t i = 1; i < palette.size(); ++i) {
        EXPECT_EQ(palette[i] >> 24, 0xFFu);
        EXPECT_GE(brightness(palette[i]), brightness(palette[i - 1])) << i;
    }
    EXPECT_EQ(level_to_rgb(-1.0), palette.front());
    EXPECT_EQ(level_to_rgb(2.0), palette.back());
}

TEST(RenderRow, ColorsAcrossThePixelsAndKeepsTheCarrier) {
    std::vector<float> row(2048, -100.0f);
    row[1024] = -40.0f;
    std::vector<float> scratch;
    std::vector<std::uint32_t> pixels(512);
    render_row(row, DbScale{-80.0, 40.0}, scratch, pixels);
    EXPECT_EQ(pixels[256], 0xFFFFFFFFu);  // -40 dBFS, at the top of the scale
    EXPECT_EQ(pixels[0], waterfall_palette()[0]);
    EXPECT_EQ(pixels[511], waterfall_palette()[0]);
}

TEST(MedianDbfs, HoldsTheNoiseFloorUnderFewCarriers) {
    std::vector<float> row(2048, -70.0f);
    for (std::size_t i = 0; i < 200; i += 2) row[i] = -20.0f;  // about 5% of the bins
    EXPECT_FLOAT_EQ(median_dbfs(row), -70.0f);
}

TEST(NoiseFloorTracker, StartsAtTheFirstRowAndFollowsSlowly) {
    NoiseFloorTracker tracker;
    EXPECT_FALSE(tracker.display_floor_dbfs());
    std::vector<float> row(100, -70.0f);
    tracker.update(row);
    EXPECT_NEAR(*tracker.display_floor_dbfs(), -70.0 - NoiseFloorTracker::kFloorBelowMedianDb, 1e-9);
    std::vector<float> louder(100, -60.0f);
    tracker.update(louder);
    EXPECT_NEAR(*tracker.median_dbfs(), -70.0 + 10.0 * NoiseFloorTracker::kAlphaPerRow, 1e-9);
    for (int i = 0; i < 1000; ++i) tracker.update(louder);
    EXPECT_NEAR(*tracker.median_dbfs(), -60.0, 1e-3);
}

TEST(FrequencyAxis, PutsABinsFrequencyAtTheCenterOfItsPixels) {
    const FrequencyAxis axis{48000.0, 2048};
    const double width = 2048.0;  // one pixel per bin
    const double bin_hz = 48000.0 / 2048;
    EXPECT_DOUBLE_EQ(axis.x_for_hz(0.0, width), 1024.5);                 // bin 1024
    EXPECT_DOUBLE_EQ(axis.x_for_hz(-24000.0, width), 0.5);               // bin 0
    EXPECT_DOUBLE_EQ(axis.x_for_hz(1023 * bin_hz, width), 2047.5);       // the top bin
    EXPECT_DOUBLE_EQ(axis.lowest_hz(), -24000.0 - 0.5 * bin_hz);
    EXPECT_DOUBLE_EQ(axis.highest_hz(), 24000.0 - 0.5 * bin_hz);
}

TEST(FrequencyAxis, RoundTrips) {
    const FrequencyAxis axis{192000.0, 8192};
    for (const double hz : {-90000.0, -123.4, 0.0, 77777.0})
        EXPECT_NEAR(axis.hz_for_x(axis.x_for_hz(hz, 1234.0), 1234.0), hz, 1e-6);
}

TEST(RoundTicks, UsesOneTwoFiveStepsWithinTheRange) {
    // +/-24 kHz over 1200 pixels, at least 100 pixels apart: 4 kHz is the smallest spacing, so 5 kHz.
    const std::vector<double> ticks = round_ticks(-24.0, 24.0, 1200.0, 100.0);
    EXPECT_EQ(ticks, (std::vector<double>{-20.0, -15.0, -10.0, -5.0, 0.0, 5.0, 10.0, 15.0, 20.0}));
    EXPECT_TRUE(round_ticks(1.0, 1.0, 100.0, 10.0).empty());
    EXPECT_TRUE(round_ticks(0.0, 1.0, 0.0, 10.0).empty());
}

TEST(RowBuilder, AveragesInPowerOverItsFrames) {
    RowBuilder builder(2, 4096);
    SpectrumFrame a;
    a.time_s = 1.0;
    a.power_db = {-30.0f, -60.0f};
    SpectrumFrame b;
    b.time_s = 2.0;
    b.power_db = {-300.0f, -60.0f};
    EXPECT_FALSE(builder.push(a));
    const auto row = builder.push(b);
    ASSERT_TRUE(row);
    EXPECT_DOUBLE_EQ(row->time_s, 2.0);
    ASSERT_EQ(row->dbfs.size(), 2u);
    EXPECT_NEAR(row->dbfs[0], -30.0 - 10.0 * std::log10(2.0), 1e-4);  // half the power in one of two frames
    EXPECT_NEAR(row->dbfs[1], -60.0, 1e-4);
    EXPECT_FALSE(builder.push(a));  // and it starts again
}

TEST(RowBuilder, ReducesWideRowsByAWholeFactorKeepingTheCarrier) {
    RowBuilder builder(1, 4096);
    SpectrumFrame frame;
    frame.power_db.assign(32768, -80.0f);
    frame.power_db[16000] = -20.0f;
    const auto row = builder.push(frame);
    ASSERT_TRUE(row);
    ASSERT_EQ(row->dbfs.size(), 4096u);  // a factor of 8
    EXPECT_NEAR(row->dbfs[2000], -20.0, 1e-3);
    EXPECT_NEAR(row->dbfs[2001], -80.0, 1e-3);
}

TEST(RowBuilder, ExactZerosReadFarBelowAnyFloorNotInfinity) {
    RowBuilder builder(1, 4096);
    SpectrumFrame frame;
    frame.power_db.assign(4, -std::numeric_limits<float>::infinity());
    const auto row = builder.push(frame);
    ASSERT_TRUE(row);
    EXPECT_FLOAT_EQ(row->dbfs[0], -300.0f);
}

TEST(FramesPerRow, RoundsToAWholeNumberOfHops) {
    EXPECT_EQ(frames_per_row_for(0.04, 1024.0 / 48000.0), 2);  // 42.67 ms
    EXPECT_EQ(frames_per_row_for(0.04, 0.5), 1);
}
