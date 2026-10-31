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

TEST(Labels, ParsesTransmissionsAndScoreFlag) {
    const auto labels = parse_labels(R"({
      "sample_rate": 48000, "duration_s": 30.0,
      "signals": [{"text": "CQ K1ABC CQ K1ABC", "freq_offset_hz": 100.0, "wpm": 25.0, "snr_db": 15.0,
                   "start_s": 0.5, "end_s": 12.0, "score": false,
                   "transmissions": [{"text": "CQ K1ABC", "start_s": 0.5, "end_s": 3.0},
                                     {"text": "CQ K1ABC", "start_s": 9.5, "end_s": 12.0}]}]})");
    const auto& s = labels.signals.at(0);
    EXPECT_FALSE(s.score);
    ASSERT_EQ(s.transmissions.size(), 2u);
    EXPECT_EQ(s.transmissions[1].text, "CQ K1ABC");
    EXPECT_DOUBLE_EQ(s.transmissions[1].start_s, 9.5);
}

TEST(Labels, OlderFilesDefaultToOneScoredTransmission) {
    const auto labels = parse_labels(R"({"sample_rate": 48000, "duration_s": 30.0,
      "signals": [{"text": "CQ", "freq_offset_hz": 0.0, "wpm": 25.0, "snr_db": 15.0,
                   "start_s": 0.5, "end_s": 2.0}]})");
    EXPECT_TRUE(labels.signals.at(0).score);
    EXPECT_TRUE(labels.signals.at(0).transmissions.empty());
}

// The generator's other fields, as training/kz4ap_synth writes them (SignalSpec through dataclasses.asdict), which
// the genie runs read.
TEST(Labels, ParsesTheFieldsTheGenieReads) {
    const auto labels = parse_labels(R"({
      "sample_rate": 48000, "duration_s": 30.0,
      "signals": [{"text": "CQ K1ABC", "freq_offset_hz": -1234.5, "wpm": 22.5, "snr_db": 15.0,
                   "start_s": 0.75, "end_s": 12.0, "drift_hz_per_s": -0.5, "fading_hz": 0.6,
                   "keying": "hand", "imbalance_dits": 0.0625, "wpm_end": 30.0, "edge_s": 0.005,
                   "senders": [{"call": "K1ABC", "wpm": 22.5}, {"call": "W9XYZ", "wpm": 25.0}]}]})");
    const auto& s = labels.signals.at(0);
    EXPECT_EQ(s.drift_hz_per_s, -0.5);
    EXPECT_EQ(s.fading_hz, 0.6);
    EXPECT_EQ(s.keying, "hand");
    EXPECT_EQ(s.imbalance_dits, 0.0625);
    ASSERT_TRUE(s.wpm_end.has_value());
    EXPECT_EQ(*s.wpm_end, 30.0);
    EXPECT_EQ(s.sender_count, 2);
}

TEST(Labels, TheFieldsTheGenieReadsDefaultToTheGeneratorsDefaults) {
    // an older file, and a per-station label of a QSO (no fading_hz, no senders, no wpm_end)
    for (const char* signal : {R"({"text": "CQ", "freq_offset_hz": 0.0, "wpm": 25.0, "snr_db": 15.0,
                                   "start_s": 0.5, "end_s": 2.0})",
                               R"({"text": "CQ", "freq_offset_hz": 0.0, "wpm": 25.0, "snr_db": 15.0,
                                   "start_s": 0.5, "end_s": 2.0, "keying": "paddle", "imbalance_dits": -0.03,
                                   "drift_hz_per_s": 0.0, "score": true, "tag": "x", "qso_index": 3})"}) {
        const auto labels = parse_labels(std::string(R"({"sample_rate": 48000, "duration_s": 30.0, "signals": [)") +
                                         signal + "]}");
        const auto& s = labels.signals.at(0);
        EXPECT_EQ(s.drift_hz_per_s, 0.0);
        EXPECT_EQ(s.fading_hz, 0.0);
        EXPECT_FALSE(s.wpm_end.has_value());
        EXPECT_EQ(s.sender_count, 0);
    }
    const auto older = parse_labels(R"({"sample_rate": 48000, "duration_s": 30.0,
      "signals": [{"text": "CQ", "freq_offset_hz": 0.0, "wpm": 25.0, "snr_db": 15.0,
                   "start_s": 0.5, "end_s": 2.0}]})");
    EXPECT_EQ(older.signals.at(0).keying, "machine");
    EXPECT_EQ(older.signals.at(0).imbalance_dits, 0.0);
    const auto station = parse_labels(R"({"sample_rate": 48000, "duration_s": 30.0,
      "signals": [{"text": "CQ", "freq_offset_hz": 0.0, "wpm": 25.0, "snr_db": 15.0,
                   "start_s": 0.5, "end_s": 2.0, "keying": "paddle", "imbalance_dits": -0.03}]})");
    EXPECT_EQ(station.signals.at(0).keying, "paddle");
    EXPECT_EQ(station.signals.at(0).imbalance_dits, -0.03);
}

TEST(Labels, APerStationLabelNamesItsQsoAndSaysWhetherItWroteItsFadingSpread) {
    // training/kz4ap_synth.generate.station_labels writes an entry for each station of a QSO, which names the QSO
    // (qso_index); since Task 8b it also writes the QSO's fading_hz. A label with no fading_hz, or a null one, did not
    // say (the generator's default, 0, is then a guess, not a truth).
    const std::string base = R"("text": "CQ", "freq_offset_hz": 0.0, "wpm": 25.0, "snr_db": 15.0, "start_s": 0.5,
                                "end_s": 2.0)";
    const auto labels = parse_labels(R"({"sample_rate": 48000, "duration_s": 30.0, "signals": [)" +
                                     std::string("{") + base + "}, {" + base + R"(, "fading_hz": 0.6}, {)" + base +
                                     R"(, "fading_hz": null}, {)" + base + R"(, "qso_index": 3, "fading_hz": 0.0}, {)" +
                                     base + R"(, "qso_index": 3}]})");
    ASSERT_EQ(labels.signals.size(), 5u);
    const bool per_station[] = {false, false, false, true, true};
    const bool written[] = {false, true, false, true, false};
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(labels.signals[i].per_station, per_station[i]) << i;
        EXPECT_EQ(labels.signals[i].fading_hz_written, written[i]) << i;
    }
    EXPECT_EQ(labels.signals[1].fading_hz, 0.6);
    EXPECT_EQ(labels.signals[3].fading_hz, 0.0);
}

TEST(Labels, ANullSpeedEndAndAnEmptyListOfSendersAreAbsent) {
    // dataclasses.asdict writes wpm_end: null for a constant speed, senders: [] for one station, farnsworth_wpm
    // only when set
    const auto labels = parse_labels(R"({"sample_rate": 48000, "duration_s": 30.0,
      "signals": [{"text": "CQ", "freq_offset_hz": 0.0, "wpm": 25.0, "snr_db": 15.0, "start_s": 0.5, "end_s": 2.0,
                   "wpm_end": null, "senders": [], "overs": [], "turn_s": [0.5, 2.0], "fading_hz": 0.0}]})");
    EXPECT_FALSE(labels.signals.at(0).wpm_end.has_value());
    EXPECT_EQ(labels.signals.at(0).sender_count, 0);
}

TEST(Labels, RejectsAFieldOfTheWrongKind) {
    EXPECT_THROW(parse_labels(R"({"sample_rate": 48000, "duration_s": 30.0,
      "signals": [{"text": "CQ", "freq_offset_hz": 0.0, "wpm": 25.0, "snr_db": 15.0, "start_s": 0.5, "end_s": 2.0,
                   "fading_hz": "slow"}]})"),
                 std::runtime_error);
    EXPECT_THROW(parse_labels(R"({"sample_rate": 48000, "duration_s": 30.0,
      "signals": [{"text": "CQ", "freq_offset_hz": 0.0, "wpm": 25.0, "snr_db": 15.0, "start_s": 0.5, "end_s": 2.0,
                   "keying": 3}]})"),
                 std::runtime_error);
}

TEST(Labels, TransmissionsCarryTheirOffset) {
    // A QSO label's transmissions name their sender's carrier offset from the label's freq_offset_hz (offset_hz, Hz;
    // training/kz4ap_synth.generate.labels), which the oracle's frequency reads (kz4ap-bench --oracle --decoder viterbi);
    // a transmission without one is at the label's own frequency, offset 0.
    const auto labels = parse_labels(R"({
      "sample_rate": 48000, "duration_s": 30.0,
      "signals": [{"text": "CQ DE K1ABC K1ABC DE W9XYZ", "freq_offset_hz": 700.0, "wpm": 25.0, "snr_db": 15.0,
                   "start_s": 0.5, "end_s": 12.0,
                   "transmissions": [{"text": "CQ DE K1ABC", "start_s": 0.5, "end_s": 4.0, "sender_index": 0,
                                      "offset_hz": 0.0},
                                     {"text": "K1ABC DE W9XYZ", "start_s": 5.0, "end_s": 12.0, "sender_index": 1,
                                      "offset_hz": -37.25},
                                     {"text": "73", "start_s": 13.0, "end_s": 14.0}]}]})");
    const auto& t = labels.signals.at(0).transmissions;
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(t[0].offset_hz, 0.0);
    EXPECT_EQ(t[1].offset_hz, -37.25);
    EXPECT_EQ(t[2].offset_hz, 0.0);
    EXPECT_THROW(parse_labels(R"({"sample_rate": 48000, "duration_s": 30.0,
      "signals": [{"text": "CQ", "freq_offset_hz": 0.0, "wpm": 25.0, "snr_db": 15.0, "start_s": 0.5, "end_s": 2.0,
                   "transmissions": [{"text": "CQ", "start_s": 0.5, "end_s": 2.0, "offset_hz": "high"}]}]})"),
                 std::runtime_error);
}
