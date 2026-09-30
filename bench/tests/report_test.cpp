#include "report.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

using namespace kz4ap::bench;

namespace {

Labels two_labels() {
    Labels labels;
    labels.sample_rate = 8000;
    labels.duration_s = 3.0;
    labels.signals = {{"CQ K1ABC", 1000.0, 25, 10, 0.5, 2.5}, {"TU", 2000.0, 20, 5, 0.5, 1.5}};
    return labels;
}

}  // namespace

TEST(Report, ScoreJsonCarriesTotalsAndOneEntryPerLabel) {
    const Labels labels = two_labels();
    const std::vector<DecodedTrack> tracks{{1, 1000.0, "CQ K1ABD"}, {2, 2000.0, "TU"}};
    const Score s = score(labels.signals, tracks, 50.0, true);
    const auto j = score_json(labels, s, {{1u, 1001.5}});
    EXPECT_EQ(j.at("labels"), 2);
    EXPECT_EQ(j.at("scored"), 2);
    EXPECT_EQ(j.at("detected"), 2);
    EXPECT_DOUBLE_EQ(j.at("cer").get<double>(), s.cer);
    EXPECT_DOUBLE_EQ(j.at("detection_recall").get<double>(), 1.0);
    const auto& sig = j.at("signals");
    ASSERT_EQ(sig.size(), 2u);
    EXPECT_EQ(sig[0].at("decoded"), "CQ K1ABD");
    EXPECT_EQ(sig[0].at("edits"), 1);
    EXPECT_EQ(sig[0].at("reference"), "CQ K1ABC");
    EXPECT_DOUBLE_EQ(sig[0].at("tracked_freq_hz").get<double>(), 1001.5);
    EXPECT_TRUE(sig[1].at("tracked_freq_hz").is_null());
    EXPECT_EQ(sig[1].at("track_id"), 2);
}

TEST(Report, ParsesDecodedTextsAndBuildsOracleTracks) {
    const auto d = parse_decoded_texts(
        R"({"front_end": "bank-proto", "recording": "x.wav", "texts": ["CQ", "TU"], "channels": []})");
    EXPECT_EQ(d.front_end, "bank-proto");
    EXPECT_EQ(d.recording, "x.wav");
    ASSERT_EQ(d.texts.size(), 2u);
    Labels labels;
    labels.signals = {{"CQ", 1000.0, 25, 10, 0, 1}, {"TU", -500.0, 25, 10, 0, 1}};
    const auto t = tracks_for(labels, d);
    ASSERT_EQ(t.size(), 2u);
    EXPECT_EQ(t[1].id, 2u);
    EXPECT_DOUBLE_EQ(t[1].freq_hz, -500.0);
    EXPECT_EQ(t[1].text, "TU");
}

TEST(Report, RejectsMalformedOrMismatchedDecodedText) {
    EXPECT_THROW(parse_decoded_texts("{"), std::runtime_error);
    EXPECT_THROW(parse_decoded_texts(R"({"front_end": "x"})"), std::runtime_error);
    Labels labels;
    labels.signals = {{"CQ", 0.0, 25, 10, 0, 1}};
    EXPECT_THROW(tracks_for(labels, DecodedTexts{"x", "", {}}), std::runtime_error);
}
