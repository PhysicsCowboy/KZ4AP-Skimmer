#include "delivery_watch.hpp"

#include <gtest/gtest.h>

using namespace kz4ap::app;

TEST(DeliveryWatch, ARadioThatDeliversIsNeverStalled) {
    DeliveryWatch watch;
    for (int i = 0; i < 100; ++i) {
        watch.update(static_cast<std::uint64_t>(i) * 1000, i * 0.2);
        EXPECT_FALSE(watch.stalled());
        EXPECT_DOUBLE_EQ(watch.silent_s(), 0.0);
    }
}

TEST(DeliveryWatch, FiveSecondsWithoutASampleIsStalled) {
    DeliveryWatch watch;
    watch.update(1000, 10.0);
    watch.update(1000, 14.9);
    EXPECT_FALSE(watch.stalled());
    watch.update(1000, 15.0);
    EXPECT_TRUE(watch.stalled());
    EXPECT_DOUBLE_EQ(watch.silent_s(), 5.0);
}

TEST(DeliveryWatch, ANewSampleEndsIt) {
    DeliveryWatch watch;
    watch.update(1000, 10.0);
    watch.update(1000, 20.0);
    ASSERT_TRUE(watch.stalled());
    watch.update(1001, 20.1);
    EXPECT_FALSE(watch.stalled());
}

TEST(DeliveryWatch, ARadioThatNeverStartsIsStalledToo) {
    DeliveryWatch watch;
    watch.update(0, 0.0);
    watch.update(0, 6.0);
    EXPECT_TRUE(watch.stalled());
}
