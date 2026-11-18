#include "app_settings.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace kz4ap::app;

namespace {

UserChoices saved_choices() {
    UserChoices c;
    c.decoder_only = true;
    c.signed_only = true;
    c.known = {KnownKind::File, "C:/lists/MASTER.SCP"};
    c.speed = 5.0;
    c.radio_serial = "1234ABCD";
    c.center_khz = 14025.0;
    c.sample_rate_hz = 384000;
    c.if_gain_reduction_db = 50;
    c.lna_state = 3;
    c.auto_floor = false;
    c.floor_dbfs = -95.0;
    c.range_db = 40.0;
    c.recording_folder = "C:/recordings";
    return c;
}

}  // namespace

TEST(AppSettings, WithNothingGivenTheRunStartsFromTheSavedChoices) {
    const UserChoices saved = saved_choices();
    EXPECT_EQ(start_choices(saved, {}), saved);
}

TEST(AppSettings, AnOptionGivenOverridesItsChoiceOnly) {
    const UserChoices saved = saved_choices();
    GivenChoices given;
    given.decoder_only = false;
    given.known = KnownChoice{KnownKind::File, "D:/other.scp"};
    given.center_khz = 7030.0;
    const UserChoices start = start_choices(saved, given);
    EXPECT_FALSE(start.decoder_only);
    EXPECT_EQ(start.known, (KnownChoice{KnownKind::File, "D:/other.scp"}));
    EXPECT_DOUBLE_EQ(start.center_khz, 7030.0);
    // The rest as saved.
    EXPECT_TRUE(start.signed_only);
    EXPECT_DOUBLE_EQ(start.speed, 5.0);
    EXPECT_EQ(start.sample_rate_hz, 384000);
    EXPECT_EQ(start.radio_serial, "1234ABCD");
    EXPECT_FALSE(start.auto_floor);
}

TEST(AppSettings, AGivenChoiceIsNotSavedUnlessTheUserChangesIt) {
    const UserChoices saved = saved_choices();
    GivenChoices given;
    given.center_khz = 7030.0;
    given.speed = std::numeric_limits<double>::infinity();
    given.radio_serial = std::string("ZZZZ");
    const UserChoices start = start_choices(saved, given);
    UserChoices now = start;
    // The user changes the speed and the rate in the run, and leaves the center and the serial as the command line set them.
    now.speed = 2.0;
    now.sample_rate_hz = 768000;
    const UserChoices out = choices_to_save(saved, start, now, given);
    EXPECT_DOUBLE_EQ(out.center_khz, 14025.0);   // given, unchanged: the saved value stays
    EXPECT_EQ(out.radio_serial, "1234ABCD");     // the same
    EXPECT_DOUBLE_EQ(out.speed, 2.0);            // given, then changed in the controls: saved
    EXPECT_EQ(out.sample_rate_hz, 768000);       // not given: saved as it is now
}

TEST(AppSettings, ChoicesNotGivenAreSavedAsTheyAreAtTheEnd) {
    const UserChoices saved;
    UserChoices now = saved;
    now.decoder_only = true;
    now.known = {KnownKind::Downloaded, {}};
    now.recording_folder = "E:/iq";
    EXPECT_EQ(choices_to_save(saved, saved, now, {}), now);
}

TEST(AppSettings, ValuesTheControlsDoNotTakeGoBackToTheirDefaults) {
    UserChoices c;
    c.speed = 3.0;
    c.sample_rate_hz = 250000;
    c.if_gain_reduction_db = 70;
    c.lna_state = -1;
    c.center_khz = 0.0;
    c.floor_dbfs = 20.0;
    c.range_db = 5.0;
    c.known = {KnownKind::File, ""};
    const UserChoices d;
    const UserChoices s = sanitized(c);
    EXPECT_DOUBLE_EQ(s.speed, d.speed);
    EXPECT_EQ(s.sample_rate_hz, d.sample_rate_hz);
    EXPECT_EQ(s.if_gain_reduction_db, d.if_gain_reduction_db);
    EXPECT_EQ(s.lna_state, d.lna_state);
    EXPECT_DOUBLE_EQ(s.center_khz, d.center_khz);
    EXPECT_DOUBLE_EQ(s.floor_dbfs, d.floor_dbfs);
    EXPECT_DOUBLE_EQ(s.range_db, d.range_db);
    EXPECT_EQ(s.known.kind, KnownKind::None);
    // Max is a speed the box takes.
    c = UserChoices{};
    c.speed = std::numeric_limits<double>::infinity();
    EXPECT_TRUE(std::isinf(sanitized(c).speed));
    // A downloaded choice carries no path.
    c.known = {KnownKind::Downloaded, "x"};
    EXPECT_EQ(sanitized(c).known, (KnownChoice{KnownKind::Downloaded, {}}));
}
