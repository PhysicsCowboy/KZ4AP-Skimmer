#include "cpu_meter.hpp"

#include <gtest/gtest.h>

using namespace kz4ap;
using namespace kz4ap::app;

namespace {

StreamStatus status(double wall_s, double stream_s, double channel_s, double cpu_s, double decoder_s) {
    StreamStatus s;
    s.state = StreamStatus::State::Running;
    s.wall_s = wall_s;
    s.stream_s = stream_s;
    s.channel_seconds = channel_s;
    s.thread_cpu_s = cpu_s;
    s.decoder_seconds = decoder_s;
    return s;
}

}  // namespace

TEST(CpuMeter, GivesMsPerChannelSecondOverTheWholeRun) {
    CpuMeter meter;
    meter.push(status(1.0, 1.0, 10.0, 0.05, 0.03));
    meter.push(status(2.0, 2.0, 20.0, 0.10, 0.06));
    const CpuRates r = meter.whole_run();
    ASSERT_TRUE(r.pipeline_ms_per_channel_s);
    EXPECT_DOUBLE_EQ(*r.pipeline_ms_per_channel_s, 5.0);  // 100 ms over 20 channel-seconds
    EXPECT_DOUBLE_EQ(*r.decoder_ms_per_channel_s, 3.0);
    EXPECT_DOUBLE_EQ(*r.speed, 1.0);
}

TEST(CpuMeter, RecentCoversOnlyTheWindow) {
    CpuMeter meter;
    meter.push(status(0.0, 0.0, 0.0, 0.0, 0.0));
    meter.push(status(5.0, 5.0, 50.0, 5.0, 5.0));  // an expensive start
    const double later = 5.0 + CpuMeter::kWindowS + 1.0;
    meter.push(status(later - 1.0, 15.0, 150.0, 5.1, 5.1));
    meter.push(status(later, 16.0, 160.0, 5.11, 5.11));
    const CpuRates r = meter.recent();
    ASSERT_TRUE(r.pipeline_ms_per_channel_s);
    EXPECT_NEAR(*r.pipeline_ms_per_channel_s, 1000.0 * 0.01 / 10.0, 1e-9);
    EXPECT_NEAR(*r.speed, 1.0, 1e-9);
}

TEST(CpuMeter, HasNoRateWithoutChannelOutput) {
    CpuMeter meter;
    EXPECT_FALSE(meter.recent().speed);
    meter.push(status(1.0, 1.0, 0.0, 0.1, 0.0));
    meter.push(status(2.0, 2.0, 0.0, 0.2, 0.0));
    EXPECT_FALSE(meter.recent().pipeline_ms_per_channel_s);
    EXPECT_FALSE(meter.whole_run().pipeline_ms_per_channel_s);
    EXPECT_DOUBLE_EQ(*meter.recent().speed, 1.0);
}
