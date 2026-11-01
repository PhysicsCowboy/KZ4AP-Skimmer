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

TEST(TrackTablePlausibleOnly, ATrackIsListedOnlyOnceItsTextReadsAsPlausible) {
    TrackTable table;
    table.set_plausible_only(true);
    table.apply(born(1, 1000.0));
    EXPECT_TRUE(table.rows().empty());
    EXPECT_EQ(table.live_count(), 1u);
    EXPECT_EQ(table.opened(), 1u);
    table.apply(text(1, 1000.0, {"E", " ", "T", " ", "E"}));
    EXPECT_TRUE(table.rows().empty());
    const std::uint64_t before = table.structure_version();
    table.apply(text(1, 1000.0, {"K", "Q", "2", "R", "P"}));
    ASSERT_EQ(table.rows().size(), 1u);
    EXPECT_NE(table.structure_version(), before);
    EXPECT_EQ(table.rows()[0].text(), "E T EKQ2RP");
    EXPECT_EQ(table.live_count(), 1u);
}

TEST(TrackTablePlausibleOnly, JunkNeverAppears) {
    TrackTable table;
    table.set_keep_retired(true);
    table.set_plausible_only(true);
    table.apply(born(1, 1000.0));
    table.apply(text(1, 1000.0, {"E", " ", "T", " ", "T", " ", "E", "I", " ", "E"}));
    table.apply(died(1));
    EXPECT_TRUE(table.rows().empty());
    EXPECT_EQ(table.live_count(), 0u);
    table.advance(100.0);
    EXPECT_TRUE(table.rows().empty());
    EXPECT_TRUE(table.retired().empty());
    ASSERT_EQ(table.retired_hidden().size(), 1u);
    EXPECT_EQ(table.retired_hidden()[0].text(), "E T T EI E");
}

TEST(TrackTablePlausibleOnly, ANewTrackAtAnEndedTracksPlaceGoesOnWithItsText) {
    TrackTable table;
    table.set_plausible_only(true);
    table.apply(born(1, 1000.0));
    table.apply(text(1, 1000.0, {"K", "Q", "2", "R", "P"}));
    ASSERT_EQ(table.rows().size(), 1u);
    table.advance(10.0);
    table.apply(died(1));
    EXPECT_FALSE(table.rows()[0].live());
    table.advance(20.0);
    // 20 Hz away: the same place; the row goes on, under the new track's id.
    table.apply(born(2, 1020.0));
    ASSERT_EQ(table.rows().size(), 1u);
    EXPECT_TRUE(table.rows()[0].live());
    EXPECT_EQ(table.rows()[0].id, 2u);
    EXPECT_EQ(table.rows()[0].tracks, 2u);
    table.apply(text(2, 1020.0, {"C", "Q"}));
    EXPECT_EQ(table.rows()[0].text(), "KQ2RP CQ");
    // The new track's corrections count from its own first symbol.
    DecodedTextEvent corrected = text(2, 1020.0, {});
    TextCorrection k;
    k.from_index = 1;
    k.chars = symbols({"S"});
    corrected.corrections.push_back(k);
    table.apply(corrected);
    EXPECT_EQ(table.rows()[0].text(), "KQ2RP CS");
    // Another place keeps its own row.
    table.apply(born(3, 2000.0));
    table.apply(text(3, 2000.0, {"W", "1", "A", "W"}));
    EXPECT_EQ(table.rows().size(), 2u);
}

TEST(TrackTablePlausibleOnly, HiddenTextAddsUpAcrossATracksRetirement) {
    TrackTable table;
    table.set_plausible_only(true);
    table.apply(born(1, 1000.0));
    table.apply(text(1, 1000.0, {"E", "K"}));
    table.advance(10.0);
    table.apply(died(1));
    EXPECT_TRUE(table.rows().empty());
    table.apply(born(2, 1000.0));
    table.apply(text(2, 1000.0, {"Q", "2", "R", "P"}));
    ASSERT_EQ(table.rows().size(), 1u);
    EXPECT_EQ(table.rows()[0].text(), "EK Q2RP");
}

TEST(TrackTablePlausibleOnly, ADuplicatesHiddenRowGoesAtOnce) {
    TrackTable table;
    table.set_plausible_only(true);
    table.apply(born(1, 1000.0));
    table.apply(text(1, 1000.0, {"E"}));
    TrackEvent merged = died(1);
    merged.end = TrackEnd::Merged;
    table.apply(merged);
    EXPECT_TRUE(table.hidden().empty());
}

TEST(TrackTable, ARowKeepsOnlyItsLatestSymbolsAndCorrectionsStillLand) {
    TrackTable table;
    Track t;
    t.id = 1;
    table.apply(TrackEvent{TrackEvent::Kind::Born, t});
    const auto add = [&](const std::string& symbol, std::size_t count, std::vector<TextCorrection> corrections = {}) {
        DecodedTextEvent event;
        event.track_id = 1;
        for (std::size_t i = 0; i < count; ++i) event.chars.push_back(DecodedSymbol{symbol, 1.0f, 0.0, 0.0});
        event.corrections = std::move(corrections);
        table.apply(event);
    };
    std::size_t total = 0;
    for (; total < 3 * TrackTable::kMaxRowSymbols; total += 10) {
        add("E", 10);
        ASSERT_LE(table.rows().at(0).symbols.size(), TrackTable::kMaxRowSymbols + 256);
    }
    const TrackRow& row = table.rows().at(0);
    EXPECT_GE(row.symbols.size(), TrackTable::kMaxRowSymbols);
    EXPECT_EQ(row.trimmed + row.symbols.size(), total);
    // A correction counts from the track's first symbol, however much has been trimmed.
    TextCorrection correction;
    correction.from_index = total - 3;
    correction.chars = {DecodedSymbol{"X", 1.0f, 0.0, 0.0}};
    add("E", 0, {correction});
    EXPECT_EQ(table.rows().at(0).trimmed + table.rows().at(0).symbols.size(), total - 2);
    EXPECT_EQ(table.rows().at(0).symbols.back(), "X");
    EXPECT_EQ(table.rows().at(0).symbols[table.rows().at(0).symbols.size() - 2], "E");
    // One that reaches into what has been trimmed keeps nothing before it, and goes on.
    correction.from_index = 0;
    correction.chars = {DecodedSymbol{"Q", 1.0f, 0.0, 0.0}};
    add("E", 0, {correction});
    EXPECT_EQ(table.rows().at(0).text(), "Q");
}

TEST(TrackTable, ARowsRevisionMovesWithWhatItShows) {
    using namespace kz4ap;
    using namespace kz4ap::app;
    TrackTable table;
    TrackEvent born;
    born.kind = TrackEvent::Kind::Born;
    born.track.id = 7;
    born.track.freq_hz = 1000.0;
    table.apply(born);
    ASSERT_EQ(table.rows().size(), 1u);
    const std::uint64_t at_birth = table.rows()[0].revision;
    DecodedTextEvent text;
    text.track_id = 7;
    text.freq_hz = 1000.0;
    text.chars.push_back(DecodedSymbol{"E", 1.0f, 0.0, 0.1});
    table.apply(text);
    const std::uint64_t with_text = table.rows()[0].revision;
    EXPECT_NE(with_text, at_birth);
    table.advance(1.0);
    EXPECT_EQ(table.rows()[0].revision, with_text);  // the clock alone changes nothing a row shows
    TrackEvent died = born;
    died.kind = TrackEvent::Kind::Died;
    table.apply(died);
    EXPECT_NE(table.rows()[0].revision, with_text);  // an ended row reads gray
}
