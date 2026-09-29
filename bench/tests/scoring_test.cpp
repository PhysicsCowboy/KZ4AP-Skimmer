#include "scoring.hpp"

#include <gtest/gtest.h>

#include <kz4ap/morse.hpp>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace kz4ap::bench;

TEST(Scoring, NormalizeUppercasesAndCollapsesSpaces) {
    EXPECT_EQ(normalize_text("  cq   k1abc \t"), "CQ K1ABC");
    EXPECT_EQ(normalize_text(""), "");
}

TEST(Scoring, EditDistance) {
    EXPECT_EQ(edit_distance("", ""), 0u);
    EXPECT_EQ(edit_distance("ABC", "ABD"), 1u);
    EXPECT_EQ(edit_distance("K1ABC", "K1AB"), 1u);
    EXPECT_EQ(edit_distance("", "ABC"), 3u);
    EXPECT_EQ(edit_distance("KITTEN", "SITTING"), 3u);
}

TEST(Scoring, MatchesNearestTrackWithinTolerance) {
    const std::vector<LabeledSignal> labels{{"CQ K1ABC", 1000.0, 25, 20, 0, 5}};
    const std::vector<DecodedTrack> tracks{{1, 1010.0, "CQ K1ABC"}, {2, 3000.0, "TU"}};
    const auto s = score(labels, tracks);
    ASSERT_EQ(s.signals.size(), 1u);
    EXPECT_EQ(s.signals[0].track_id, 1u);
    EXPECT_DOUBLE_EQ(s.signals[0].cer, 0.0);
    EXPECT_EQ(s.detected, 1u);
    EXPECT_EQ(s.false_tracks, 1u);
}

TEST(Scoring, MissedSignalCountsEveryCharacter) {
    const std::vector<LabeledSignal> labels{{"CQ", 1000.0, 25, 20, 0, 5}};
    const auto s = score(labels, {});
    EXPECT_FALSE(s.signals[0].track_id.has_value());
    EXPECT_DOUBLE_EQ(s.signals[0].cer, 1.0);
    EXPECT_EQ(s.detected, 0u);
}

TEST(Scoring, AggregateCerWeightsByLength) {
    const std::vector<LabeledSignal> labels{{"ABCDEFGHIJ", 0.0, 25, 20, 0, 5}, {"AB", 5000.0, 25, 20, 0, 5}};
    const std::vector<DecodedTrack> tracks{{1, 0.0, "ABCDEFGHIJ"}, {2, 5000.0, "XY"}};
    EXPECT_DOUBLE_EQ(score(labels, tracks).cer, 2.0 / 12.0);
}

TEST(Scoring, SplitTrackUsesTheLongestText) {
    const std::vector<LabeledSignal> labels{{"CQ K1ABC", 1000.0, 25, 20, 0, 5}};
    const std::vector<DecodedTrack> tracks{{1, 990.0, "E"}, {2, 1020.0, "CQ K1ABC"}};
    const auto s = score(labels, tracks);
    EXPECT_EQ(s.signals[0].track_id, 2u);
    EXPECT_EQ(s.false_tracks, 1u);
}

TEST(Scoring, ProsignCountsAsOneSymbol) {
    // "CQ DE K1ABC <KN>" split into symbols: C Q _ D E _ K 1 A B C _ <KN>
    const auto ref_symbols = kz4ap::morse::symbols("CQ DE K1ABC <KN>");
    ASSERT_EQ(ref_symbols.size(), 13u);

    const std::vector<LabeledSignal> labels{{"CQ DE K1ABC <KN>", 1000.0, 25, 20, 0, 5}};
    const std::vector<DecodedTrack> tracks{{1, 1000.0, "CQ DE K1ABC"}};
    const auto s = score(labels, tracks);
    ASSERT_EQ(s.signals.size(), 1u);
    // Decoded text is missing the trailing space and the <KN> prosign: 2 edits.
    EXPECT_EQ(s.signals[0].edits, 2u);
    EXPECT_DOUBLE_EQ(s.signals[0].cer, 2.0 / 13.0);
}

TEST(Scoring, WrongProsignIsOneSubstitution) {
    const std::vector<LabeledSignal> labels{{"<SK>", 1000.0, 25, 20, 0, 5}};
    const std::vector<DecodedTrack> tracks{{1, 1000.0, "<AR>"}};
    const auto s = score(labels, tracks);
    ASSERT_EQ(s.signals.size(), 1u);
    EXPECT_EQ(s.signals[0].edits, 1u);
    EXPECT_DOUBLE_EQ(s.signals[0].cer, 1.0);
}

TEST(Scoring, UnclosedAngleBracketIsPlainCharacters) {
    const auto ref_symbols = kz4ap::morse::symbols("A<B");
    ASSERT_EQ(ref_symbols.size(), 3u);

    const std::vector<LabeledSignal> labels{{"A<B", 1000.0, 25, 20, 0, 5}};
    const std::vector<DecodedTrack> tracks{{1, 1000.0, "A<B"}};
    const auto s = score(labels, tracks);
    ASSERT_EQ(s.signals.size(), 1u);
    EXPECT_EQ(s.signals[0].edits, 0u);
    EXPECT_DOUBLE_EQ(s.signals[0].cer, 0.0);
}

namespace {

LabeledSignal make_label(std::string text, double freq, std::vector<Transmission> transmissions = {},
                         bool scored = true) {
    LabeledSignal l{std::move(text), freq, 25, 20, 0, 5};
    l.transmissions = std::move(transmissions);
    l.score = scored;
    return l;
}

Alignment align_text(std::string_view reference, std::string_view decoded) {
    return align(kz4ap::morse::symbols(reference), kz4ap::morse::symbols(decoded));
}

}  // namespace

TEST(Scoring, AlignmentTotalEqualsEditDistance) {
    for (const auto& [a, b] : std::vector<std::pair<std::string, std::string>>{
             {"KITTEN", "SITTING"}, {"CQ TEST K1ABC", "CQTEST K1AB"}, {"", "ABC"}, {"ABC", ""}, {"TU", "TU"}}) {
        EXPECT_EQ(align_text(a, b).counts.total(), edit_distance(a, b)) << a << " / " << b;
    }
}

TEST(Scoring, MissingWordSpaceIsASpaceEdit) {
    const auto a = align_text("CQ TEST", "CQTEST");
    EXPECT_EQ(a.counts.space_edits, 1u);
    EXPECT_EQ(a.counts.char_edits, 0u);
}

TEST(Scoring, WrongLetterIsACharacterEdit) {
    const auto a = align_text("CQ", "RQ");
    EXPECT_EQ(a.counts.char_edits, 1u);
    EXPECT_EQ(a.counts.space_edits, 0u);
    EXPECT_EQ(a.charged, (std::vector<std::size_t>{1, 0}));
}

TEST(Scoring, InsertionIsChargedToTheFollowingReferenceSymbol) {
    EXPECT_EQ(align_text("AB", "AXB").charged, (std::vector<std::size_t>{0, 1}));
    EXPECT_EQ(align_text("AB", "ABX").charged, (std::vector<std::size_t>{0, 1}));
    EXPECT_EQ(align_text("AB", "XAB").charged, (std::vector<std::size_t>{1, 0}));
}

TEST(Scoring, FirstWordOfEachTransmissionIsScored) {
    const auto label = make_label("CQ K1ABC CQ K1ABC", 1000.0, {{"CQ K1ABC", 0.5, 3.0}, {"CQ K1ABC", 9.0, 12.0}});
    EXPECT_EQ(first_word_ranges(label),
              (std::vector<std::pair<std::size_t, std::size_t>>{{0, 2}, {9, 11}}));
    const auto s = score({label}, {{1, 1000.0, "RQ K1ABC CQ K1ABC"}});
    EXPECT_EQ(s.signals[0].first_word_symbols, 4u);
    EXPECT_EQ(s.signals[0].first_word_edits, 1u);
    EXPECT_DOUBLE_EQ(s.first_word_cer, 0.25);
}

TEST(Scoring, WithoutTransmissionsTheWholeTextIsOneTransmission) {
    EXPECT_EQ(first_word_ranges(make_label("CQ TEST", 0.0)),
              (std::vector<std::pair<std::size_t, std::size_t>>{{0, 2}}));
}

TEST(Scoring, TransmissionsThatDoNotAddUpThrow) {
    EXPECT_THROW(first_word_ranges(make_label("CQ TEST", 0.0, {{"CQ", 0.0, 1.0}})), std::runtime_error);
}

TEST(Scoring, CharacterAndSpaceRatesAreSeparate) {
    const auto s = score({make_label("CQ TEST K1ABC", 1000.0)}, {{1, 1000.0, "CQTEST K1ABD"}});
    EXPECT_EQ(s.signals[0].chars, 11u);
    EXPECT_EQ(s.signals[0].spaces, 2u);
    EXPECT_DOUBLE_EQ(s.char_cer, 1.0 / 11.0);
    EXPECT_DOUBLE_EQ(s.space_error_rate, 0.5);
    EXPECT_DOUBLE_EQ(s.cer, 2.0 / 13.0);
}

TEST(Scoring, UnscoredSignalIsMatchedButNotCounted) {
    const auto s = score({make_label("CQ", 1000.0), make_label("TU", 1100.0, {}, false)},
                         {{1, 1000.0, "CQ"}, {2, 1100.0, "EEE"}});
    EXPECT_EQ(s.scored, 1u);
    EXPECT_EQ(s.detected, 1u);
    EXPECT_EQ(s.false_tracks, 0u);
    EXPECT_DOUBLE_EQ(s.cer, 0.0);
    EXPECT_EQ(s.signals[1].track_id, 2u);
}

TEST(Scoring, TransmissionsAreScoredSeparately) {
    const auto label = make_label("CQ K1ABC CQ K1ABC", 1000.0, {{"CQ K1ABC", 0.5, 3.0}, {"CQ K1ABC", 9.0, 12.0}});
    EXPECT_EQ(transmission_ranges(label),
              (std::vector<std::pair<std::size_t, std::size_t>>{{0, 8}, {9, 17}}));
    const auto s = score({label}, {{1, 1000.0, "CQ K1ABC CQ K1AEC"}});
    ASSERT_EQ(s.signals[0].transmissions.size(), 2u);
    EXPECT_EQ(s.signals[0].transmissions[0].symbols, 8u);
    EXPECT_EQ(s.signals[0].transmissions[0].edits, 0u);
    EXPECT_EQ(s.signals[0].transmissions[1].edits, 1u);
    EXPECT_EQ(s.signals[0].transmissions[1].first_word_symbols, 2u);
    EXPECT_EQ(s.signals[0].transmissions[1].first_word_edits, 0u);
}

TEST(Scoring, NoSpaceCerIsVe3neasMetric) {
    // A word space inserted inside a word: a space edit, but nothing without spaces.
    const auto s = score({make_label("CQ TEST", 1000.0)}, {{1, 1000.0, "CQ T EST"}});
    EXPECT_EQ(s.signals[0].space_edits, 1u);
    EXPECT_EQ(s.signals[0].nospace_symbols, 6u);
    EXPECT_EQ(s.signals[0].nospace_edits, 0u);
    // A character decoded as a word space: no character edit, but one edit without spaces.
    const auto t = score({make_label("CQ TEST", 1000.0)}, {{1, 1000.0, "CQ TE T"}});
    EXPECT_EQ(t.signals[0].char_edits, 0u);
    EXPECT_EQ(t.signals[0].nospace_edits, 1u);
    EXPECT_DOUBLE_EQ(t.nospace_cer, 1.0 / 6.0);
}

TEST(Scoring, AmbiguousEditsAreChargedToTheEarliestPosition) {
    // The decoder lost the *second* "CQ", not the first, but the traceback (ties broken from
    // the end toward match/substitution before deletion/insertion) charges all 3 deletions --
    // C, Q, and the word space between the two words -- to the earliest reference symbols: the
    // first word itself, then the word space. Nothing is charged to the second "CQ".
    const auto a = align_text("CQ CQ", "CQ");
    EXPECT_EQ(a.counts.total(), 3u);
    EXPECT_EQ(a.counts.char_edits, 2u);   // the first C and Q
    EXPECT_EQ(a.counts.space_edits, 1u);  // the word space between the two words
    EXPECT_EQ(a.charged, (std::vector<std::size_t>{1, 1, 1, 0, 0}));

    const auto label = make_label("CQ CQ", 1000.0);
    const auto s = score({label}, {{1, 1000.0, "CQ"}});
    EXPECT_EQ(s.signals[0].first_word_symbols, 2u);
    EXPECT_EQ(s.signals[0].first_word_edits, 2u);  // both edits charged to "CQ", though it decoded correctly
    EXPECT_DOUBLE_EQ(s.first_word_cer, 1.0);
}
