#include "labels.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace kz4ap::bench;

TEST(Labels, ParsesGeneratorOutput) {
    const auto labels = parse_labels(R"({
      "sample_rate": 192000, "duration_s": 30.0, "snr_bandwidth_hz": 500.0,
      "signals": [{"text": "CQ K1ABC", "freq_offset_hz": -1234.5, "wpm": 25.0,
                   "snr_db": 15.0, "start_s": 0.5, "end_s": 6.25}]})");
    EXPECT_EQ(labels.sample_rate, 192000);
    EXPECT_DOUBLE_EQ(labels.duration_s, 30.0);
    ASSERT_EQ(labels.signals.size(), 1u);
    EXPECT_EQ(labels.signals[0].text, "CQ K1ABC");
    EXPECT_DOUBLE_EQ(labels.signals[0].freq_offset_hz, -1234.5);
    EXPECT_DOUBLE_EQ(labels.signals[0].end_s, 6.25);
}

TEST(Labels, RejectsMissingField) {
    EXPECT_THROW(parse_labels(R"({"sample_rate": 192000, "signals": []})"), std::runtime_error);
}

TEST(Labels, RejectsMalformedJson) {
    EXPECT_THROW(parse_labels("{not json"), std::runtime_error);
}
