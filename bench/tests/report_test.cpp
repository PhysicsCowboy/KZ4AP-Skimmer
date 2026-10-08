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

TEST(Report, DetectorTracksAreScoredByFrequencyWithFalseTracks) {
    const auto d = parse_decoded_texts(R"({"front_end": "bank-proto", "recording": "x.wav", "tracks": [
        {"id": 3, "freq_hz": 1010.0, "text": "CQ K1ABC", "last_freq_hz": 1001.0},
        {"id": 4, "freq_hz": 2600.0, "text": "EE"},
        {"id": 5, "freq_hz": 2000.0, "text": "TU"}]})");
    ASSERT_TRUE(d.detector);
    ASSERT_EQ(d.tracks.size(), 3u);
    EXPECT_DOUBLE_EQ(d.tracks[0].last_freq_hz, 1001.0);
    EXPECT_DOUBLE_EQ(d.tracks[1].last_freq_hz, 2600.0);  // defaults to the birth frequency
    Labels labels;
    labels.signals = {{"CQ K1ABC", 1000.0, 25, 10, 0, 1}, {"TU", 2000.0, 25, 10, 0, 1}};
    const Score s = score(labels.signals, d.tracks, 50.0, false);  // the engine's detector-path rule
    EXPECT_EQ(s.signals[0].track_id, 3u);
    EXPECT_EQ(s.signals[1].track_id, 5u);
    EXPECT_EQ(s.false_tracks, 1u);                                 // track 4 decoded text and matched nothing
    EXPECT_THROW(parse_decoded_texts(R"({"front_end": "x", "texts": [], "tracks": []})"), std::runtime_error);
    EXPECT_TRUE(parse_decoded_texts(R"({"front_end": "x", "tracks": []})").detector);  // no track opened
}

namespace {

std::vector<kz4ap::DecodedSymbol> symbols(const std::vector<std::string>& texts) {
    std::vector<kz4ap::DecodedSymbol> out;
    for (const auto& t : texts) out.push_back({t, 1.0f, 0.0, 0.0});
    return out;
}

}  // namespace

TEST(Report, TrackTextAppliesCorrectionsByIndexToTheFinalTextOnly) {
    TrackText t;
    t.apply(symbols({"C", "Q", " ", "D"}), {});
    t.apply({}, {kz4ap::TextCorrection{1, symbols({"Q", "E"}), 2.0, "switch"}});
    EXPECT_EQ(t.final_text(), "CQE");
    EXPECT_EQ(t.immediate_text(), "CQ D");
    // Chars first, then corrections; a correction's index is clipped to the characters published.
    t.apply(symbols({"X"}), {kz4ap::TextCorrection{99, symbols({"Y"}), 3.0, "rekey"}});
    EXPECT_EQ(t.final_text(), "CQEXY");
    EXPECT_EQ(t.immediate_text(), "CQ DX");
    t.apply({}, {kz4ap::TextCorrection{0, {}, 4.0, "timeout"}});
    EXPECT_EQ(t.final_text(), "");
    EXPECT_EQ(t.immediate_text(), "CQ DX");
}

TEST(Report, TrackTextKeepsEveryCorrectionsTimeReachAndReason) {
    TrackText t;
    t.apply(symbols({"C", "Q", " ", "D"}), {});
    EXPECT_TRUE(t.corrections().empty());
    kz4ap::TextCorrection a{1, symbols({"Q", "E"}), 2.0, "switch", 0.5};
    kz4ap::TextCorrection b{2, symbols({"X"}), 3.5, "rekey", 1.25};
    t.apply(symbols({"Z"}), {a, b});
    ASSERT_EQ(t.corrections().size(), 2u);
    EXPECT_EQ(t.corrections()[0].t_s, 2.0);
    EXPECT_EQ(t.corrections()[0].reach_s, 0.5);
    EXPECT_EQ(t.corrections()[0].reason, "switch");
    EXPECT_EQ(t.corrections()[1].reach_s, 1.25);
    const auto j = corrections_json(t.corrections());
    ASSERT_EQ(j.size(), 2u);
    EXPECT_EQ(j[1].at("reason"), "rekey");
    EXPECT_DOUBLE_EQ(j[1].at("reach_s").get<double>(), 1.25);
    EXPECT_DOUBLE_EQ(j[1].at("t_s").get<double>(), 3.5);
    EXPECT_TRUE(corrections_json({}).is_array());
    EXPECT_TRUE(corrections_json({}).empty());
}

TEST(Report, TrackTextCountsTheCharactersEachCorrectionRemovedAndInserted) {
    TrackText t;
    t.apply(symbols({"A", "B", "C", "D", "E"}), {});
    // From index 1: B C D E becomes B X D E; the shared start (B) and end (D E) are not changes.
    t.apply({}, {kz4ap::TextCorrection{1, symbols({"B", "X", "D", "E"}), 1.0, "switch"}});
    // Everything deleted.
    t.apply({}, {kz4ap::TextCorrection{0, {}, 2.0, "timeout"}});
    // Two appended, then an index past them (clipped): one inserted, none removed.
    t.apply(symbols({"F", "G"}), {kz4ap::TextCorrection{99, symbols({"H"}), 3.0, "rekey"}});
    // The same characters sent again (a correction whose index is before its first change): nothing changed.
    t.apply({}, {kz4ap::TextCorrection{0, symbols({"F", "G", "H"}), 4.0, "resync"}});
    // A shared start and end that would overlap: "A A" against "A" is one removed, not a negative count.
    t.apply(symbols({"A", "A"}), {kz4ap::TextCorrection{3, symbols({"A"}), 5.0, "switch"}});
    EXPECT_EQ(t.final_text(), "FGHA");
    const auto& c = t.corrections();
    ASSERT_EQ(c.size(), 5u);
    const std::size_t removed[] = {1, 5, 0, 0, 1}, inserted[] = {1, 0, 1, 0, 0};
    for (std::size_t i = 0; i < c.size(); ++i) {
        EXPECT_EQ(c[i].removed, removed[i]) << i;
        EXPECT_EQ(c[i].inserted, inserted[i]) << i;
    }
    const auto j = corrections_json(c);
    EXPECT_EQ(j[0].at("removed").get<std::size_t>(), 1u);
    EXPECT_EQ(j[1].at("removed").get<std::size_t>(), 5u);
    EXPECT_EQ(j[2].at("inserted").get<std::size_t>(), 1u);
}

TEST(Report, ImmediateScoreIsAddedBesideTheFinalOne) {
    const Labels labels = two_labels();
    const Score final_score = score(labels.signals, {{1, 1000.0, "CQ K1ABC"}, {2, 2000.0, "TU"}}, 50.0, true);
    const Score immediate = score(labels.signals, {{1, 1000.0, "CQ K1ABD"}, {2, 2000.0, "TU"}}, 50.0, true);
    auto j = score_json(labels, final_score, {});
    add_immediate_score(j, immediate);
    EXPECT_DOUBLE_EQ(j.at("cer").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(j.at("cer_immediate").get<double>(), immediate.cer);
    EXPECT_GT(immediate.cer, 0.0);
    EXPECT_EQ(j.at("signals")[0].at("decoded_immediate"), "CQ K1ABD");
    EXPECT_EQ(j.at("signals")[0].at("edits_immediate"), 1);
    EXPECT_DOUBLE_EQ(j.at("signals")[0].at("cer_immediate").get<double>(), immediate.signals[0].cer);
}
