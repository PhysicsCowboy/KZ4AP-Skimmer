#include "recording_name.hpp"

#include <gtest/gtest.h>

using namespace kz4ap::app;

TEST(RecordingName, SaysWhereTheRadioWasTuned) {
    EXPECT_EQ(recording_file_name(7030000.0, 192000, "20261030-213000"), "kz4ap_7030.000kHz_192kHz_20261030-213000.wav");
    EXPECT_EQ(recording_file_name(14025500.0, 96000, "x"), "kz4ap_14025.500kHz_96kHz_x.wav");
}
