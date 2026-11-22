#include "decoded_file.hpp"

#include "kz4ap/utc_time.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::app;

namespace {

TrackEvent born(std::uint32_t id, double freq_hz, double start_s) {
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

// Characters of `dit_s` dits each with a dit between them, the first starting at t0_s.
DecodedTextEvent timed(std::uint32_t id, double freq_hz, const std::vector<std::string>& texts, double t0_s, double dit_s = 0.06) {
    DecodedTextEvent e{id, freq_hz, {}, 25.0f, 1.0f};
    double t = t0_s;
    for (const auto& s : texts) {
        e.chars.push_back(DecodedSymbol{s, 1.0f, t, t + 3 * dit_s});
        t += 4 * dit_s;
    }
    return e;
}

// The checks every file must pass: its version, its start, and for each track the old fields, a span, and characters that spell
// its text, each timed one inside the track's span, in order (each starts no earlier than the one before ends).
void expect_consistent(const nlohmann::json& j) {
    ASSERT_EQ(j.at("format_version"), kDecodedFormatVersion);
    for (const char* key : {"front_end", "recording", "stream_start_utc", "stream_s", "written_utc", "lost_samples", "radio_resets"})
        ASSERT_TRUE(j.contains(key)) << key;
    for (const auto& t : j.at("tracks")) {
        for (const char* key : {"id", "freq_hz", "last_freq_hz", "text", "track_ids", "born_s", "died_s", "start_s", "end_s", "live",
                                "trimmed", "symbols"})
            ASSERT_TRUE(t.contains(key)) << key;
        const double start = t.at("start_s"), end = t.at("end_s");
        EXPECT_LE(start, end);
        EXPECT_LE(start, t.at("born_s").get<double>());
        if (!t.at("died_s").is_null()) EXPECT_GE(end, t.at("died_s").get<double>());
        std::string spelled;
        double last_end = -1e300;
        for (const auto& c : t.at("symbols")) {
            ASSERT_EQ(c.size(), 3u);
            spelled += c[0].get<std::string>();
            if (c[1].is_null()) {
                EXPECT_TRUE(c[2].is_null());
                EXPECT_EQ(c[0], " ");  // only the table's own word spaces have no times
                continue;
            }
            const double s = c[1], e = c[2];
            EXPECT_LE(s, e);
            EXPECT_GE(s, start - 1e-9);
            EXPECT_LE(e, end + 1e-9);
            EXPECT_GE(s, last_end - 1e-9) << t.at("id") << " " << c.dump();
            last_end = e;
        }
        EXPECT_EQ(spelled, t.at("text").get<std::string>());
    }
}

}  // namespace

TEST(DecodedFile, CarriesTheStreamsStartAndEachTracksSpanAndCharacterTimes) {
    TrackTable table;
    table.set_keep_retired(true);
    table.apply(born(1, 500.0, 1.5));
    table.apply(born(2, -800.0, 2.0));
    table.apply(timed(1, 501.0, {"C", "Q", " ", "D", "E"}, 2.0));
    table.apply(timed(2, -800.0, {"T", "U"}, 1.2));  // decoded from the kept past, before its birth at 2 s
    // A correction: from index 3 on, "DE" becomes "K1A".
    DecodedTextEvent fix = timed(1, 502.0, {}, 0.0);
    fix.corrections.push_back(TextCorrection{});
    fix.corrections.back().from_index = 3;
    for (const auto& c : timed(1, 502.0, {"K", "1", "A"}, 2.96).chars) fix.corrections.back().chars.push_back(c);
    table.apply(fix);
    table.advance(5.0);
    table.apply(died(2));

    DecodedFileInfo info{"segmental", "live.wav", unix_ms_from_civil(2026, 11, 17, 12, 30, 5, 46), 6.25};
    info.written_unix_ms = unix_ms_from_civil(2026, 11, 17, 12, 30, 11, 321);
    info.lost_samples = 4096;
    info.radio_resets = 1;
    const nlohmann::json j = nlohmann::json::parse(decoded_json(info, table.rows(), table.retired()));
    expect_consistent(j);
    EXPECT_EQ(j.at("format_version"), 3);
    EXPECT_EQ(j.at("stream_start_utc"), "2026-11-17T12:30:05.046Z");
    EXPECT_EQ(j.at("written_utc"), "2026-11-17T12:30:11.321Z");  // 6.275 s after the start: 25 ms behind stream_s
    EXPECT_EQ(j.at("lost_samples"), 4096);
    EXPECT_EQ(j.at("radio_resets"), 1);
    EXPECT_EQ(j.at("tracks")[0].at("trimmed"), 0);
    EXPECT_DOUBLE_EQ(j.at("stream_s").get<double>(), 6.25);
    ASSERT_EQ(j.at("tracks").size(), 2u);
    // Rows in order of frequency: track 2 first, ended at 5 s; track 1 still live, its end the stream's time.
    const auto& t2 = j.at("tracks")[0];
    EXPECT_EQ(t2.at("id"), 2);
    EXPECT_EQ(t2.at("text"), "TU");
    EXPECT_FALSE(t2.at("live").get<bool>());
    EXPECT_DOUBLE_EQ(t2.at("born_s").get<double>(), 2.0);
    EXPECT_DOUBLE_EQ(t2.at("died_s").get<double>(), 5.0);
    EXPECT_DOUBLE_EQ(t2.at("start_s").get<double>(), 1.2);  // its first character's start
    EXPECT_DOUBLE_EQ(t2.at("end_s").get<double>(), 5.0);
    const auto& t1 = j.at("tracks")[1];
    EXPECT_EQ(t1.at("text"), "CQ K1A");
    EXPECT_DOUBLE_EQ(t1.at("freq_hz").get<double>(), 502.0);
    EXPECT_TRUE(t1.at("live").get<bool>());
    EXPECT_DOUBLE_EQ(t1.at("end_s").get<double>(), 6.25);
    EXPECT_TRUE(t1.at("died_s").is_null());
    EXPECT_DOUBLE_EQ(t1.at("born_s").get<double>(), 1.5);
    EXPECT_DOUBLE_EQ(t1.at("start_s").get<double>(), 1.5);
    EXPECT_EQ(t1.at("track_ids"), nlohmann::json::array({1}));
    // The decoder's own times, to 1 ms: "K" from the correction at 2.96 s to 2.96 + 0.18 s.
    EXPECT_EQ(t1.at("symbols")[3], nlohmann::json::array({"K", 2.96, 3.14}));
    EXPECT_EQ(t1.at("symbols")[0], nlohmann::json::array({"C", 2.0, 2.18}));
}

TEST(DecodedFile, WithoutAKnownStartTheStartIsNull) {
    const nlohmann::json j = nlohmann::json::parse(decoded_json(DecodedFileInfo{"matched", "radio", std::nullopt, 0.0}, {}, {}));
    expect_consistent(j);
    EXPECT_TRUE(j.at("stream_start_utc").is_null());
    EXPECT_TRUE(j.at("radio_resets").is_null());  // not a radio
    EXPECT_EQ(j.at("lost_samples"), 0);
    EXPECT_TRUE(j.at("tracks").empty());
}

TEST(DecodedFile, TheTablesOwnWordSpacesHaveNoTimes) {
    // A row whose middle stretch is left out (heard from a listed neighbor), and a word space put in between two tracks at one
    // place (stored times 0 and 0).
    TrackRow row;
    row.id = 7;
    row.track_ids = {6, 7};
    row.start_s = 1.0;
    row.ended_at_s = 9.0;
    row.symbols = {"A", "B", "X", "Y", "C", " ", "D"};
    row.left_out = {0, 0, 1, 1, 0, 0, 0};
    row.heard_from = {0, 0, 3, 3, 0, 0, 0};
    row.starts = {1.0, 1.5, 2.0, 2.5, 3.0, 0.0, 5.0};
    row.ends = {1.2, 1.7, 2.2, 2.7, 3.2, 0.0, 5.2};
    row.trimmed = 256;  // the symbols before these were dropped
    const nlohmann::json j = nlohmann::json::parse(decoded_json(DecodedFileInfo{"segmental", "x", std::nullopt, 10.0}, {row}, {}));
    expect_consistent(j);
    const auto& t = j.at("tracks")[0];
    EXPECT_EQ(t.at("text"), "AB C D");
    EXPECT_EQ(t.at("trimmed"), 256);
    const auto& s = t.at("symbols");
    ASSERT_EQ(s.size(), 6u);
    EXPECT_EQ(s[2], nlohmann::json::array({" ", nullptr, nullptr}));  // in place of "XY"
    EXPECT_EQ(s[3], nlohmann::json::array({"C", 3.0, 3.2}));
    EXPECT_EQ(s[4], nlohmann::json::array({" ", nullptr, nullptr}));  // between tracks 6 and 7
}
