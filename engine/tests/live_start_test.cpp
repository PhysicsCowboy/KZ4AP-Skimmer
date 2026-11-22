#include "kz4ap/live_start.hpp"
#include "kz4ap/utc_time.hpp"

#include <gtest/gtest.h>

#include <cstdint>

using namespace kz4ap;

TEST(UtcTime, ALiveSourcesFirstSampleIsItsFirstBlocksArrivalLessTheBlocksLength) {
    // 2016 samples at 192 kHz last 10.5 ms: 11 ms (llround of 10.5).
    EXPECT_EQ(first_sample_unix_ms(1'000'000, 2016, 192000), 1'000'000 - 11);
    EXPECT_EQ(first_sample_unix_ms(1'000'000, 0, 192000), 1'000'000);
    EXPECT_EQ(first_sample_unix_ms(1'000'000, 2016, 0), 1'000'000);
    // The system clock is after 2026-01-01.
    EXPECT_GT(system_unix_ms(), unix_ms_from_civil(2026, 1, 1, 0, 0, 0, 0));
}

TEST(LiveStart, StampsOnceAtTheFirstBlockWithSamples) {
    LiveStart start;
    int clock_reads = 0;
    std::int64_t now = 5'000'000;
    const auto clock = [&] {
        ++clock_reads;
        return now;
    };
    EXPECT_FALSE(start.unix_ms());
    // An empty first block stamps nothing and does not read the clock.
    EXPECT_FALSE(start.on_block(0, 192000, clock));
    EXPECT_FALSE(start.unix_ms());
    EXPECT_EQ(clock_reads, 0);
    // The first block with samples: 1920 samples at 192 kHz, 10 ms before its arrival.
    EXPECT_TRUE(start.on_block(1920, 192000, clock));
    ASSERT_TRUE(start.unix_ms());
    EXPECT_EQ(*start.unix_ms(), 5'000'000 - 10);
    EXPECT_EQ(clock_reads, 1);
    // Later blocks, a reset of the radio's stream among them (the source does not tell this apart: it is the stamp's rule), never
    // stamp again, and do not read the clock.
    now = 9'000'000;
    EXPECT_FALSE(start.on_block(1920, 192000, clock));
    EXPECT_FALSE(start.on_block(0, 192000, clock));
    EXPECT_FALSE(start.on_block(7, 192000, clock));
    EXPECT_EQ(*start.unix_ms(), 5'000'000 - 10);
    EXPECT_EQ(clock_reads, 1);
}
