#include "scoring.hpp"

#include <gtest/gtest.h>

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
