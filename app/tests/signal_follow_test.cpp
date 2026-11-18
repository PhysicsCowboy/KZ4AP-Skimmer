#include "signal_follow.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::app;

namespace {

TrackEvent born(std::uint32_t id, double freq_hz, double start_s = 1.0) {
    Track t;
    t.id = id;
    t.freq_hz = freq_hz;
    t.snr_db = 12.0f;
    t.start_time_s = start_s;
    return TrackEvent{TrackEvent::Kind::Born, t};
}

TrackEvent died(std::uint32_t id) {
    Track t;
    t.id = id;
    return TrackEvent{TrackEvent::Kind::Died, t};
}

// The symbols at 0 s, or one each 0.1 s from at_s, each 0.08 s long.
DecodedTextEvent text(std::uint32_t id, double freq_hz, const std::vector<std::string>& chars, float wpm = 0, double at_s = -1) {
    std::vector<DecodedSymbol> out;
    for (const auto& s : chars) {
        const double start_s = at_s < 0 ? 0.0 : at_s + 0.1 * static_cast<double>(out.size());
        out.push_back(DecodedSymbol{s, 1.0f, start_s, at_s < 0 ? 0.0 : start_s + 0.08});
    }
    return DecodedTextEvent{id, freq_hz, out, wpm, 1.0f};
}

// Text that reads as plausible to a table that keeps to it (text_evidence; as in track_table_test.cpp).
const std::vector<std::string> kCall = {"K", "Q", "2", "R", "P"};

}  // namespace

TEST(SignalFollow, ShowsTheChosenRowsTextFrequencyAndSpeedAsTheyCome) {
    TrackTable table;
    table.apply(born(1, 500.0));
    table.apply(born(2, 900.0));
    SignalFollow follow;
    ASSERT_TRUE(follow.follow(table, 2));
    table.apply(text(2, 901.0, {"C", "Q"}, 24.0f));
    EXPECT_TRUE(follow.update(table));
    EXPECT_EQ(follow.text(), "CQ");
    EXPECT_DOUBLE_EQ(follow.freq_hz(), 901.0);
    EXPECT_FLOAT_EQ(follow.wpm(), 24.0f);
    EXPECT_TRUE(follow.live());
    EXPECT_EQ(follow.row_id(), 2u);
    EXPECT_FALSE(follow.update(table));  // nothing new
    table.apply(text(1, 500.0, {"T", "E", "S", "T"}));
    EXPECT_FALSE(follow.update(table));  // another row's text
    EXPECT_EQ(follow.text(), "CQ");
}

TEST(SignalFollow, RefusesATrackNoRowHolds) {
    TrackTable table;
    table.apply(born(1, 500.0));
    SignalFollow follow;
    EXPECT_FALSE(follow.follow(table, 7));
    EXPECT_FALSE(follow.active());
}

TEST(SignalFollow, StaysOnTheRowWhenThePlausibleTableGoesOnWithANewTrack) {
    TrackTable table;
    table.set_plausible_only(true);
    table.apply(born(1, 500.0));
    table.apply(text(1, 500.0, kCall, 22.0f));
    ASSERT_EQ(table.rows().size(), 1u);
    SignalFollow follow;
    ASSERT_TRUE(follow.follow(table, 1));
    table.advance(10.0);
    table.apply(died(1));
    EXPECT_TRUE(follow.update(table));
    EXPECT_FALSE(follow.live());
    // A new track 12 Hz away: the table's row takes it (TrackTable::kSamePlaceHz), and the pane stays with that row.
    table.advance(12.0);
    table.apply(born(5, 512.0));
    table.apply(text(5, 512.0, {"T", "U"}));
    EXPECT_TRUE(follow.update(table));
    EXPECT_TRUE(follow.live());
    EXPECT_EQ(follow.row_id(), 5u);
    EXPECT_EQ(follow.rows_followed(), 1u);
    EXPECT_EQ(follow.text(), "KQ2RP TU");
}

TEST(SignalFollow, MovesOnToANewTrackAtThePlaceInATableThatListsItApart) {
    TrackTable table;
    table.apply(born(1, 500.0));
    table.apply(born(2, 560.0));  // a neighbor 60 Hz away, live all along
    table.apply(text(1, 500.0, {"C", "Q"}, 20.0f));
    SignalFollow follow;
    ASSERT_TRUE(follow.follow(table, 1));
    table.advance(5.0);
    table.apply(died(1));
    follow.update(table);
    EXPECT_FALSE(follow.live());
    EXPECT_EQ(follow.row_id(), 1u);  // the neighbor is too far, and older
    // A new track 20 Hz away: the pane goes on with it, after the text it had.
    table.apply(born(3, 520.0));
    table.apply(text(3, 520.0, {"D", "E"}, 21.0f));
    EXPECT_TRUE(follow.update(table));
    EXPECT_EQ(follow.row_id(), 3u);
    EXPECT_TRUE(follow.live());
    EXPECT_EQ(follow.text(), "CQ DE");
    EXPECT_FLOAT_EQ(follow.wpm(), 21.0f);
    EXPECT_EQ(follow.rows_followed(), 2u);
}

TEST(SignalFollow, DoesNotJumpToANeighborBornBeforeIt) {
    TrackTable table;
    table.apply(born(1, 500.0));
    table.apply(born(2, 515.0));  // older than the followed track, 15 Hz away
    table.apply(text(2, 515.0, {"A"}));
    table.apply(born(3, 1000.0));
    SignalFollow follow;
    ASSERT_TRUE(follow.follow(table, 3));
    table.apply(text(3, 1000.0, {"B"}));
    table.apply(died(3));
    follow.update(table);
    EXPECT_EQ(follow.row_id(), 3u);
    // Following track 2, which ends: track 1 is older, so it is not its successor.
    ASSERT_TRUE(follow.follow(table, 2));
    table.apply(died(2));
    follow.update(table);
    EXPECT_EQ(follow.row_id(), 2u);
    EXPECT_FALSE(follow.live());
}

TEST(SignalFollow, KeepsTheTextWhenTheRowLeavesTheTable) {
    TrackTable table;
    table.apply(born(1, 500.0));
    table.apply(text(1, 500.0, {"7", "3"}, 18.0f));
    SignalFollow follow;
    ASSERT_TRUE(follow.follow(table, 1));
    table.advance(1.0);
    table.apply(died(1));
    follow.update(table);
    table.advance(1.0 + TrackTable::kRetainEndedS + 1.0);
    ASSERT_TRUE(table.rows().empty());
    EXPECT_TRUE(follow.update(table));
    EXPECT_FALSE(follow.row_id());
    EXPECT_EQ(follow.text(), "73");
    EXPECT_DOUBLE_EQ(follow.freq_hz(), 500.0);
    // A new track at the place, after the row has gone, still goes on with it.
    table.apply(born(4, 495.0));
    EXPECT_TRUE(follow.update(table));
    EXPECT_EQ(follow.row_id(), 4u);
}

TEST(SignalFollow, RowNearPicksTheNearestLiveRowWithinTheDistance) {
    TrackTable table;
    table.apply(born(1, 500.0));
    table.apply(born(2, 520.0));
    table.apply(text(2, 520.0, {"E"}));
    table.apply(born(3, 2000.0));
    EXPECT_EQ(row_near(table, 505.0, 30.0), 1u);
    EXPECT_EQ(row_near(table, 515.0, 30.0), 2u);
    EXPECT_FALSE(row_near(table, 1500.0, 30.0));
    table.apply(died(2));
    EXPECT_EQ(row_near(table, 519.0, 30.0), 1u);  // live first
    EXPECT_EQ(row_near(table, 519.0, 10.0), 2u);  // the ended one when it alone is near
}

TEST(SignalFollow, DoesNotMoveToANeighborThatStartedWhileItWasStillKeyed) {
    TrackTable table;
    table.apply(born(1, 500.0, 1.0));
    table.apply(text(1, 500.0, {"C", "Q"}, 20.0f, 9.8));  // last heard at 9.98 s
    // A neighbor 20 Hz away, born after track 1 but started at 5 s, while track 1 was still keyed.
    table.apply(born(2, 520.0, 5.0));
    table.apply(text(2, 520.0, {"T", "U"}, 25.0f, 6.0));
    SignalFollow follow;
    ASSERT_TRUE(follow.follow(table, 1));
    table.advance(12.0);
    table.apply(died(1));
    follow.update(table);
    EXPECT_EQ(follow.row_id(), 1u);
    EXPECT_FALSE(follow.live());
    EXPECT_EQ(follow.text(), "CQ");
    // A track that starts 1 s after it fell silent, 10 Hz away: within kSuccessorOverlapS, it goes on with it.
    table.apply(born(3, 510.0, 11.0));
    table.apply(text(3, 510.0, {"D", "E"}, 21.0f, 11.5));
    EXPECT_TRUE(follow.update(table));
    EXPECT_EQ(follow.row_id(), 3u);
    EXPECT_EQ(follow.text(), "CQ DE");
}

TEST(SignalFollow, AFailedFollowLeavesTheSignalFollowedAsItWas) {
    TrackTable table;
    table.apply(born(1, 500.0));
    table.apply(text(1, 500.0, {"7", "3"}, 18.0f));
    SignalFollow follow;
    ASSERT_TRUE(follow.follow(table, 1));
    EXPECT_FALSE(follow.follow(table, 99));
    EXPECT_TRUE(follow.active());
    EXPECT_EQ(follow.row_id(), 1u);
    EXPECT_EQ(follow.text(), "73");
    EXPECT_TRUE(follow.live());
}
