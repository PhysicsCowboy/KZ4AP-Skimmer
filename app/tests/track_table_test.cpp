#include "track_table.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::app;

namespace {

TrackEvent born(std::uint32_t id, double freq_hz, float snr_db = 12.0f) {
    Track t;
    t.id = id;
    t.freq_hz = freq_hz;
    t.snr_db = snr_db;
    t.start_time_s = 1.5;
    return TrackEvent{TrackEvent::Kind::Born, t};
}

TrackEvent died(std::uint32_t id, double freq_hz = 0) {
    Track t;
    t.id = id;
    t.freq_hz = freq_hz;
    return TrackEvent{TrackEvent::Kind::Died, t};
}

std::vector<DecodedSymbol> symbols(const std::vector<std::string>& texts) {
    std::vector<DecodedSymbol> out;
    for (const auto& s : texts) out.push_back(DecodedSymbol{s, 1.0f, 0.0, 0.0});
    return out;
}

DecodedTextEvent text(std::uint32_t id, double freq_hz, const std::vector<std::string>& chars, float wpm = 0) {
    return DecodedTextEvent{id, freq_hz, symbols(chars), wpm, 1.0f};
}

}  // namespace

TEST(TrackTable, KeepsRowsInOrderOfFrequency) {
    TrackTable table;
    table.apply(born(1, 500.0));
    table.apply(born(2, -3000.0));
    table.apply(born(3, 12000.0));
    ASSERT_EQ(table.rows().size(), 3u);
    EXPECT_EQ(table.rows()[0].id, 2u);
    EXPECT_EQ(table.rows()[1].id, 1u);
    EXPECT_EQ(table.rows()[2].id, 3u);
    EXPECT_EQ(table.live_count(), 3u);
    EXPECT_EQ(table.row_of(3), 2u);
    EXPECT_FALSE(table.row_of(9));
}

TEST(TrackTable, AppendsTextAndTakesTheLatestSpeedAndFrequency) {
    TrackTable table;
    table.apply(born(1, 500.0, 17.0f));
    table.apply(text(1, 500.0, {"C", "Q"}, 25.5f));
    table.apply(text(1, 503.0, {" ", "<SK>"}, 0.0f));  // 0 WPM: no estimate, the last one stays
    const TrackRow& row = table.rows()[0];
    EXPECT_EQ(row.text(), "CQ <SK>");
    EXPECT_FLOAT_EQ(row.wpm, 25.5f);
    EXPECT_DOUBLE_EQ(row.freq_hz, 503.0);
    EXPECT_FLOAT_EQ(row.level_db, 17.0f);
}

TEST(TrackTable, TextTailCountsSymbolsNotBytes) {
    TrackRow row;
    row.symbols = {"A", "<SK>", "B"};
    EXPECT_EQ(row.text_tail(2), "<SK>B");
    EXPECT_EQ(row.text_tail(10), "A<SK>B");
    EXPECT_EQ(row.text_tail(0), "");
}

TEST(TrackTable, AppliesCharsFirstAndThenCorrectionsInOrder) {
    TrackTable table;
    table.apply(born(1, 0.0));
    table.apply(text(1, 0.0, {"C", "Q", "X"}));
    // The consumer's rule: this update appends "Y" and then replaces from index 2 ("XY") by "DE".
    DecodedTextEvent update = text(1, 0.0, {"Y"});
    update.corrections.push_back(TextCorrection{2, symbols({"D", "E"}), 0.0, "switch", 1.0});
    table.apply(update);
    EXPECT_EQ(table.rows()[0].text(), "CQDE");
}

TEST(TrackTable, ClipsACorrectionPastTheEndOfTheList) {
    TrackTable table;
    table.apply(born(1, 0.0));
    table.apply(text(1, 0.0, {"A"}));
    DecodedTextEvent update = text(1, 0.0, {});
    update.corrections.push_back(TextCorrection{5, symbols({"B"}), 0.0, "rekey", 1.0});
    table.apply(update);
    EXPECT_EQ(table.rows()[0].text(), "AB");
}

TEST(TrackTable, IgnoresTextForAnUnknownTrack) {
    TrackTable table;
    table.apply(text(7, 0.0, {"A"}));
    EXPECT_TRUE(table.rows().empty());
}

TEST(TrackTable, ADeadTrackWithoutTextLeavesAtOnce) {
    TrackTable table;
    table.apply(born(1, 0.0));
    table.apply(died(1));
    EXPECT_TRUE(table.rows().empty());
}

TEST(TrackTable, ADeadTrackWithTextStaysForItsRetention) {
    TrackTable table;
    table.apply(born(1, 0.0));
    table.apply(text(1, 0.0, {"A"}));
    table.advance(100.0);
    table.apply(died(1));
    ASSERT_EQ(table.rows().size(), 1u);
    EXPECT_FALSE(table.rows()[0].live());
    EXPECT_EQ(table.live_count(), 0u);
    table.advance(100.0 + TrackTable::kRetainEndedS);
    EXPECT_EQ(table.rows().size(), 1u);
    table.advance(100.0 + TrackTable::kRetainEndedS + 0.1);
    EXPECT_TRUE(table.rows().empty());
}

TEST(TrackTable, KeepsRetiredTracksOnlyWhenAsked) {
    TrackTable table;
    table.set_keep_retired(true);
    table.apply(born(1, 0.0));
    table.apply(text(1, 0.0, {"A"}));
    table.apply(died(1));
    table.apply(born(2, 0.0));
    table.apply(died(2));
    table.advance(1000.0);
    ASSERT_EQ(table.retired().size(), 2u);
    EXPECT_EQ(table.retired()[0].id, 2u);  // no text: it left first
    EXPECT_EQ(table.retired()[1].text(), "A");
}

TEST(TrackTable, EndOfStreamEndsEveryTrackAndRemovesNone) {
    TrackTable table;
    table.apply(born(1, 0.0));
    table.apply(born(2, 100.0));
    table.apply(text(1, 0.0, {"A"}));
    table.end_of_stream();
    EXPECT_EQ(table.rows().size(), 2u);
    EXPECT_EQ(table.live_count(), 0u);
}

TEST(TrackTable, StructureVersionMovesOnlyWhenALayoutMust) {
    TrackTable table;
    const auto start = table.structure_version();
    table.apply(born(1, 0.0));
    table.apply(born(2, 100.0));
    const auto two = table.structure_version();
    EXPECT_NE(two, start);
    table.apply(text(1, 50.0, {"A"}, 20.0f));  // moves, but the order holds
    EXPECT_EQ(table.structure_version(), two);
    table.apply(text(1, 200.0, {"B"}));  // crosses track 2
    EXPECT_NE(table.structure_version(), two);
    EXPECT_EQ(table.rows()[0].id, 2u);
    EXPECT_EQ(table.rows()[1].id, 1u);
}
