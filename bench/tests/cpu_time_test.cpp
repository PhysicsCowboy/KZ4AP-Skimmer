#include "cpu_time.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>

TEST(CpuTime, BusyLoopUsesCpuTime) {
    const double before = kz4ap::bench::process_cpu_seconds();
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    volatile double sink = 0;
    while (std::chrono::steady_clock::now() < until) sink = sink + std::sqrt(static_cast<double>(sink) + 1.0);
    const double used = kz4ap::bench::process_cpu_seconds() - before;
    EXPECT_GT(used, 0.1);
    EXPECT_LT(used, 5.0);
}
