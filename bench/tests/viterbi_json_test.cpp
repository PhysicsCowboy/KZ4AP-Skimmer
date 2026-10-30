// The new decoder in the bench (bench/src/viterbi_json.cpp): its configuration as JSON and as --set, the genie values
// from the labels, the rules among the options, and the statistics file with its round trip.
#include "viterbi_json.hpp"

#include "kz4ap/engine.hpp"
#include "kz4ap/event_bus.hpp"
#include "kz4ap/viterbi/config.hpp"
#include "kz4ap/viterbi/search.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using kz4ap::EngineConfig;
using kz4ap::bench::apply_setting;
using kz4ap::bench::ChannelStats;
using kz4ap::bench::check_engine_accepts;
using kz4ap::bench::check_viterbi_options;
using kz4ap::bench::genie_from_label;
using kz4ap::bench::GenieKind;
using kz4ap::bench::LabeledSignal;
using kz4ap::bench::parse_genie_kind;
using kz4ap::bench::parse_labels;
using kz4ap::bench::StatsFile;
using kz4ap::bench::StatsSummaryFile;
using kz4ap::bench::to_json;
using kz4ap::bench::ViterbiOptions;
using kz4ap::viterbi::LineEnd;
using kz4ap::viterbi::LineKind;
using kz4ap::viterbi::NewbornLine;
using kz4ap::viterbi::SearchStats;
using kz4ap::viterbi::ViterbiConfig;
using kz4ap::viterbi::ViterbiGenie;
using nlohmann::ordered_json;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// What a call refuses with (std::invalid_argument's message), or "(accepted)".
template <typename F>
std::string refusal_of(F&& call) {
    try {
        call();
    } catch (const std::invalid_argument& e) {
        return e.what();
    }
    return "(accepted)";
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// What --set NAME=VALUE refuses with.
std::string setting_refusal(ViterbiConfig& config, const std::string& setting) {
    return refusal_of([&] { apply_setting(config, setting); });
}

// The compile-time guard of the field table: a structured binding names all the members of an aggregate, so adding or
// removing a ViterbiConfig field stops this file from compiling until this list, the table in viterbi_json.cpp and the
// expected names below are changed together.
void every_field_is_named(const ViterbiConfig& c) {
    [[maybe_unused]] const auto& [sample_interval_s,
                                  dot_min_ms,
                                  speed_ratio,
                                  speed_count,
                                  fist_medians_dots,
                                  fist_sigmas,
                                  fist_shifts_dots,
                                  beam_nats,
                                  ceiling,
                                  front_dot_s,
                                  edge_rise_s,
                                  edge_rise_min_s,
                                  edge_rise_max_s,
                                  fading_spread_hz,
                                  systematic_floor,
                                  nu_bin_s,
                                  nu_memory_s,
                                  over_rate_per_s,
                                  rescue_epsilon,
                                  word_end_p,
                                  over_end_q,
                                  level_min_db,
                                  level_max_db,
                                  quadrature_order,
                                  newborn_switch_sigma_u,
                                  newborn_grid_points,
                                  newborn_live_range_nats,
                                  newborn_fade_interval_s,
                                  hiss_half_width_hz,
                                  hiss_percentile,
                                  hiss_average_s,
                                  genie_fading_spread_hz,
                                  stats] = c;
}

const std::vector<std::string> kExpectedNames = {
    "sample_interval_s",
    "dot_min_ms",
    "speed_ratio",
    "speed_count",
    "fist_medians_dots",
    "fist_sigmas",
    "fist_shifts_dots",
    "beam_nats",
    "ceiling",
    "front_dot_s",
    "edge_rise_s",
    "edge_rise_min_s",
    "edge_rise_max_s",
    "fading_spread_hz",
    "systematic_floor",
    "nu_bin_s",
    "nu_memory_s",
    "over_rate_per_s",
    "rescue_epsilon",
    "word_end_p",
    "over_end_q",
    "level_min_db",
    "level_max_db",
    "quadrature_order",
    "newborn_switch_sigma_u",
    "newborn_grid_points",
    "newborn_live_range_nats",
    "newborn_fade_interval_s",
    "hiss_half_width_hz",
    "hiss_percentile",
    "hiss_average_s",
    "genie_fading_spread_hz",
    "stats"};

// The fields that the decoder's frequency tracker and the frequency genie had (removed: design Parts 10 and 12).
const std::vector<std::string> kRemovedNames = {
    "piece_s", "block_loss_db", "keying_rise_s", "systematic_memory_s", "systematic_lookahead_s",
    "tracker_window_s",          "tracker_hop_s",   "tracker_search_hz",         "tracker_gate_db",
    "tracker_drift_sd_hz_per_s", "tracker_drift_noise", "genie_frequency_offset_hz", "genie_frequency_drift_hz_per_s"};

// ---- The configuration and --set ----

TEST(ViterbiJson, TheConfigurationHasEveryFieldInDeclarationOrder) {
    every_field_is_named(ViterbiConfig{});
    const ordered_json j = to_json(ViterbiConfig{});
    std::vector<std::string> names;
    for (const auto& item : j.items()) names.push_back(item.key());
    EXPECT_EQ(names, kExpectedNames);
    EXPECT_EQ(kz4ap::bench::viterbi_config_field_names(), kExpectedNames);
    // the defaults, each in its own kind and unit
    EXPECT_EQ(j.at("sample_interval_s"), 4.0 / 1500.0);
    EXPECT_EQ(j.at("dot_min_ms"), 12.0);
    EXPECT_TRUE(j.at("speed_count").is_number_integer());
    EXPECT_EQ(j.at("speed_count"), 32);
    EXPECT_FALSE(j.contains("factor_count"));  // no Farnsworth factor (the owner's decision D14 of 2026-10-21)
    EXPECT_FALSE(j.contains("factor_ratio"));
    EXPECT_EQ(j.at("fist_medians_dots"), ordered_json::array({1.0, 3.0, 1.0, 3.0, 7.0}));
    EXPECT_EQ(j.at("fist_sigmas"), ordered_json::array({0.3, 0.3, 0.3, 0.3, 0.3}));
    EXPECT_EQ(j.at("fist_shifts_dots"), ordered_json::array({0.0, 0.0, 0.0, 0.0, 0.0}));
    EXPECT_EQ(j.at("beam_nats"), 40.0);
    EXPECT_EQ(j.at("ceiling"), 1700);
    EXPECT_EQ(j.at("fading_spread_hz"), 2.0);
    EXPECT_EQ(j.at("newborn_live_range_nats"), 160.0);
    EXPECT_EQ(j.at("systematic_floor"), 0.02);
    EXPECT_EQ(j.at("level_max_db"), 85.0);
    EXPECT_EQ(j.at("quadrature_order"), 5);
    EXPECT_TRUE(j.at("genie_fading_spread_hz").is_null());
    for (const std::string& removed : kRemovedNames) EXPECT_FALSE(j.contains(removed)) << removed;
    EXPECT_TRUE(j.at("stats").is_boolean());
    EXPECT_EQ(j.at("stats"), false);
}

TEST(ViterbiJson, ASettingChangesOneFieldAndTheConfigurationShowsIt) {
    // --set beam_nats=30
    ViterbiConfig config;
    apply_setting(config, "beam_nats=30");
    EXPECT_EQ(config.beam_nats, 30.0);
    ordered_json changed = to_json(config);
    EXPECT_EQ(changed.at("beam_nats"), 30.0);
    ordered_json defaults = to_json(ViterbiConfig{});
    EXPECT_NE(changed, defaults);
    changed.erase("beam_nats");
    defaults.erase("beam_nats");
    EXPECT_EQ(changed, defaults);  // nothing else moved
}

TEST(ViterbiJson, SettingsOfEveryKind) {
    ViterbiConfig c;
    // doubles: integers, decimals, exponents
    apply_setting(c, "fading_spread_hz=0.3");
    apply_setting(c, "over_rate_per_s=1e-3");
    apply_setting(c, "level_min_db=-20");
    EXPECT_EQ(c.fading_spread_hz, 0.3);
    EXPECT_EQ(c.over_rate_per_s, 1e-3);
    EXPECT_EQ(c.level_min_db, -20.0);
    // ints: integers, and floats that are whole
    apply_setting(c, "ceiling=600");
    EXPECT_EQ(c.ceiling, 600);
    apply_setting(c, "ceiling=17000.0");
    EXPECT_EQ(c.ceiling, 17000);
    apply_setting(c, "speed_count=61");
    EXPECT_EQ(c.speed_count, 61);
    EXPECT_TRUE(contains(setting_refusal(c, "ceiling=600.5"), "ceiling"));
    EXPECT_TRUE(contains(setting_refusal(c, "speed_count=1e10"), "speed_count"));
    EXPECT_TRUE(contains(setting_refusal(c, "speed_count=abc"), "speed_count"));
    EXPECT_EQ(c.ceiling, 17000);  // a refused setting changes nothing
    // the fist's arrays: exactly five numbers
    apply_setting(c, "fist_sigmas=[0.1, 0.2, 0.3, 0.4, 0.5]");
    EXPECT_EQ(c.fist_sigmas, (std::array<double, 5>{{0.1, 0.2, 0.3, 0.4, 0.5}}));
    apply_setting(c, "fist_shifts_dots=[0.05,0.05,-0.05,-0.05,-0.05]");
    EXPECT_EQ(c.fist_shifts_dots[2], -0.05);
    EXPECT_TRUE(contains(setting_refusal(c, "fist_sigmas=[0.1, 0.2, 0.3, 0.4]"), "fist_sigmas"));
    EXPECT_TRUE(contains(setting_refusal(c, "fist_sigmas=0.3"), "fist_sigmas"));
    EXPECT_TRUE(contains(setting_refusal(c, R"(fist_sigmas=[0.1, 0.2, 0.3, 0.4, "x"])"), "fist_sigmas"));
    EXPECT_EQ(c.fist_sigmas, (std::array<double, 5>{{0.1, 0.2, 0.3, 0.4, 0.5}}));
    // the genie's optional values: a number, or null for none
    apply_setting(c, "genie_fading_spread_hz=0.6");
    ASSERT_TRUE(c.genie_fading_spread_hz.has_value());
    EXPECT_EQ(*c.genie_fading_spread_hz, 0.6);
    apply_setting(c, "genie_fading_spread_hz=null");
    EXPECT_FALSE(c.genie_fading_spread_hz.has_value());
    // the removed fields are unknown names now
    for (const std::string& removed : kRemovedNames)
        EXPECT_TRUE(contains(setting_refusal(c, removed + "=1"), "unknown parameter")) << removed;
    // a double's field refuses null, text and booleans
    EXPECT_TRUE(contains(setting_refusal(c, "beam_nats=null"), "beam_nats"));
    EXPECT_TRUE(contains(setting_refusal(c, "beam_nats=true"), "beam_nats"));
    EXPECT_TRUE(contains(setting_refusal(c, "beam_nats=\"30\""), "beam_nats"));
    // the switch: true or false
    apply_setting(c, "stats=true");
    EXPECT_TRUE(c.stats);
    apply_setting(c, "stats=false");
    EXPECT_FALSE(c.stats);
    EXPECT_TRUE(contains(setting_refusal(c, "stats=1"), "stats"));
}

TEST(ViterbiJson, AnUnknownNameFails) {
    ViterbiConfig c;
    const std::string why = setting_refusal(c, "no_such_field=1");
    EXPECT_TRUE(contains(why, "unknown parameter")) << why;
    EXPECT_TRUE(contains(why, "no_such_field")) << why;
    EXPECT_TRUE(contains(setting_refusal(c, "beam_nat=30"), "beam_nat"));    // a typo
    EXPECT_TRUE(contains(setting_refusal(c, "Beam_nats=30"), "Beam_nats"));  // names are exact
    EXPECT_TRUE(contains(setting_refusal(c, "min_wpm=5"), "min_wpm"));       // the bank's
    EXPECT_TRUE(contains(refusal_of([&] { kz4ap::bench::set_config_value(c, "nope", ordered_json(1)); }), "nope"));
    // not a setting at all
    EXPECT_TRUE(contains(setting_refusal(c, "beam_nats"), "NAME=VALUE"));
    EXPECT_TRUE(contains(setting_refusal(c, "=30"), "NAME=VALUE"));
    EXPECT_EQ(to_json(c), to_json(ViterbiConfig{}));
}

TEST(ViterbiJson, TheConfigurationRoundTripsThroughItsJson) {
    ViterbiConfig c;
    c.sample_interval_s = 0.004;
    c.dot_min_ms = 10.0;
    c.speed_ratio = 1.05;
    c.speed_count = 61;
    c.fist_medians_dots = {{1.0, 3.2, 1.0, 3.4, 7.5}};
    c.fist_sigmas = {{0.1, 0.12, 0.15, 0.2, 0.25}};
    c.fist_shifts_dots = {{0.05, 0.05, -0.05, -0.05, -0.05}};
    c.beam_nats = 80.0;
    c.ceiling = 17000;
    c.fading_spread_hz = 0.3;
    c.front_dot_s = 0.015;
    c.edge_rise_s = 0.003;
    c.edge_rise_min_s = 0.001;
    c.edge_rise_max_s = 0.005;
    c.nu_bin_s = 0.08;
    c.nu_memory_s = 0.8;
    c.newborn_switch_sigma_u = 0.2;
    c.newborn_grid_points = 64;
    c.newborn_live_range_nats = 100.0;
    c.newborn_fade_interval_s = 0.04;
    c.systematic_floor = 0.05;
    c.over_rate_per_s = 0.5;
    c.rescue_epsilon = 1e-2;
    c.word_end_p = 0.25;
    c.over_end_q = 0.125;
    c.level_min_db = -10.0;
    c.level_max_db = 60.0;
    c.quadrature_order = 7;
    c.hiss_half_width_hz = 1500.0;
    c.hiss_percentile = 10.0;
    c.hiss_average_s = 2.0;
    c.genie_fading_spread_hz = 0.6;
    c.stats = true;
    const ordered_json j = to_json(c);
    const ordered_json defaults = to_json(ViterbiConfig{});
    for (const auto& item : j.items()) EXPECT_NE(item.value(), defaults.at(item.key())) << item.key();
    const ViterbiConfig back = kz4ap::bench::viterbi_config_from_json(ordered_json::parse(j.dump()));
    EXPECT_EQ(to_json(back), j);
    EXPECT_EQ(back.speed_count, 61);
    EXPECT_EQ(back.fist_sigmas, c.fist_sigmas);
    EXPECT_EQ(back.genie_fading_spread_hz, c.genie_fading_spread_hz);
    EXPECT_TRUE(back.stats);
    for (const std::string& removed : kRemovedNames) EXPECT_FALSE(j.contains(removed)) << removed;
    // an object with fewer fields is the defaults with those set; what is not an object, or has a field that does not
    // exist, is refused
    EXPECT_EQ(to_json(kz4ap::bench::viterbi_config_from_json(ordered_json::object())), defaults);
    EXPECT_EQ(kz4ap::bench::viterbi_config_from_json(ordered_json{{"ceiling", 600}}).ceiling, 600);
    ordered_json extra = j;
    extra["no_such_field"] = 1;
    EXPECT_TRUE(contains(refusal_of([&] { kz4ap::bench::viterbi_config_from_json(extra); }), "no_such_field"));
    EXPECT_NE(refusal_of([&] { kz4ap::bench::viterbi_config_from_json(ordered_json::array()); }), "(accepted)");
}

TEST(ViterbiJson, TheEngineRefusesWhatTheLibraryRefuses) {
    // A configuration the library cannot use is refused when an engine is built, with the library's own message. The
    // bench builds one engine of the configuration up front, so that a run on the detector path (where no decoder is
    // built until a track is born) fails before it reads the recording.
    EngineConfig engine;
    engine.sample_rate = 48000;  // DEV2's rate
    EXPECT_EQ(refusal_of([&] { check_engine_accepts(engine); }), "(accepted)");
    engine.viterbi.beam_nats = -1.0;
    const std::string why = refusal_of([&] { check_engine_accepts(engine); });
    EXPECT_TRUE(contains(why, "beam_nats")) << why;
    engine.viterbi.beam_nats = 40.0;
    engine.viterbi.fading_spread_hz = -2.0;
    EXPECT_NE(refusal_of([&] { check_engine_accepts(engine); }), "(accepted)");
    engine.viterbi.fading_spread_hz = 1.0;
    engine.viterbi.systematic_floor = 1.5;  // a relative amplitude outside [0, 1]
    const std::string floor = refusal_of([&] { check_engine_accepts(engine); });
    EXPECT_TRUE(contains(floor, "systematic_floor")) << floor;
}

// ---- The rules among the options ----

TEST(ViterbiJson, GenieWithoutOracleFails) {
    ViterbiOptions options;
    options.genie = {GenieKind::Fist};
    const std::string why = refusal_of([&] { check_viterbi_options(true, false, options); });
    EXPECT_TRUE(contains(why, "--genie")) << why;
    EXPECT_TRUE(contains(why, "--oracle")) << why;
    EXPECT_EQ(refusal_of([&] { check_viterbi_options(true, true, options); }), "(accepted)");
    // --genie and --set belong to the new decoder
    EXPECT_TRUE(contains(refusal_of([&] { check_viterbi_options(false, true, options); }), "--decoder viterbi"));
    ViterbiOptions with_set;
    with_set.settings = {"beam_nats=30"};
    EXPECT_TRUE(contains(refusal_of([&] { check_viterbi_options(false, false, with_set); }), "--set"));
    EXPECT_EQ(refusal_of([&] { check_viterbi_options(true, false, with_set); }), "(accepted)");  // no oracle needed
    EXPECT_EQ(refusal_of([&] { check_viterbi_options(false, false, ViterbiOptions{}); }), "(accepted)");
}

TEST(ViterbiJson, OmegaBarDefaultTableBelongsToTheNewDecoder) {
    // --omega-bar-default-table FILE: the new decoder's table for other level bounds (plan 1b, Task 7); no oracle needed.
    ViterbiOptions options;
    options.omega_bar_default_table = "table.bin";
    const std::string why = refusal_of([&] { check_viterbi_options(false, true, options); });
    EXPECT_TRUE(contains(why, "--omega-bar-default-table")) << why;
    EXPECT_TRUE(contains(why, "--decoder viterbi")) << why;
    EXPECT_EQ(refusal_of([&] { check_viterbi_options(true, false, options); }), "(accepted)");
}

TEST(ViterbiJson, StatsNeedsOracleAndTheNewDecoder) {
    ViterbiOptions options;
    options.stats = true;
    const std::string why = refusal_of([&] { check_viterbi_options(true, false, options); });
    EXPECT_TRUE(contains(why, "--stats")) << why;
    EXPECT_TRUE(contains(why, "--oracle")) << why;
    EXPECT_EQ(refusal_of([&] { check_viterbi_options(true, true, options); }), "(accepted)");
    EXPECT_TRUE(contains(refusal_of([&] { check_viterbi_options(false, true, options); }), "--decoder viterbi"));
    // with --genie and --set as well
    options.genie = {GenieKind::Fading};
    options.settings = {"ceiling=600"};
    EXPECT_EQ(refusal_of([&] { check_viterbi_options(true, true, options); }), "(accepted)");
    EXPECT_NE(refusal_of([&] { check_viterbi_options(true, false, options); }), "(accepted)");
}

TEST(ViterbiJson, GenieKindsAreNamedAsTheCommandLineNamesThem) {
    EXPECT_EQ(parse_genie_kind("fist"), GenieKind::Fist);
    EXPECT_EQ(parse_genie_kind("fading"), GenieKind::Fading);
    for (const GenieKind kind : {GenieKind::Fist, GenieKind::Fading})
        EXPECT_EQ(parse_genie_kind(kz4ap::bench::genie_kind_name(kind)), kind);
    const std::string why = refusal_of([&] { parse_genie_kind("loud"); });
    EXPECT_TRUE(contains(why, "loud")) << why;
    EXPECT_TRUE(contains(why, "fist or fading")) << why;
    EXPECT_NE(refusal_of([&] { parse_genie_kind("Fist"); }), "(accepted)");
}

TEST(ViterbiJson, TheFrequencyGenieIsRemoved) {
    // Oracle channels follow the label's frequency (design Part 1), so no genie gives the decoder a frequency.
    EXPECT_EQ(refusal_of([&] { parse_genie_kind("frequency"); }),
              "the frequency genie is removed: oracle channels follow the label's frequency (design Part 1)");
}

// ---- The genie from the labels ----

// The label of one station at `wpm` WPM, 1234.5 Hz below the span's center at its start, 0.75 s: the required fields,
// then `extra` (more fields of the generator's labels).
LabeledSignal signal_with(const std::string& extra = "", double wpm = 25.0) {
    const std::string json = R"({"sample_rate": 48000, "duration_s": 30.0, "signals": [{"text": "CQ",
            "freq_offset_hz": -1234.5, "wpm": )" +
                             std::to_string(wpm) + R"(, "snr_db": 15.0, "start_s": 0.75, "end_s": 12.0)" +
                             (extra.empty() ? std::string() : ", " + extra) + "}]}";
    return parse_labels(json).signals.at(0);
}

// The genie of `kinds` from a label, as the bench gives it: the label the index-th of its file, the sample 2 ms.
ViterbiGenie genie_of(const LabeledSignal& label, std::initializer_list<GenieKind> kinds, std::size_t index = 0,
                      double sample_interval_s = 0.002) {
    const std::vector<GenieKind> list(kinds);
    return genie_from_label(label, index, list, sample_interval_s);
}

std::string genie_refusal(const LabeledSignal& label, std::initializer_list<GenieKind> kinds, std::size_t index = 0,
                          double sample_interval_s = 0.002) {
    return refusal_of([&] { genie_of(label, kinds, index, sample_interval_s); });
}

// ---- The oracle's frequency from the labels ----

std::string track_refusal(const LabeledSignal& label, std::size_t index) {
    return refusal_of([&] { kz4ap::bench::oracle_frequency_track(label, index); });
}

TEST(ViterbiJsonOracle, ASingleStationIsOneSegmentWithItsDrift) {
    // the label's frequency at its start_s and its drift from there: the generator's angle 2 pi f t + pi drift
    // (t - start_s)^2, so the carrier is at f + drift (t - start_s) at stream time t (design Part 1, plan O3)
    const auto track = kz4ap::bench::oracle_frequency_track(signal_with(R"("drift_hz_per_s": 0.5)"), 3);
    ASSERT_EQ(track.size(), 1u);
    EXPECT_EQ(track[0].start_s, 0.0);
    EXPECT_EQ(track[0].frequency_hz, -1234.5);
    EXPECT_EQ(track[0].drift_hz_per_s, 0.5);
    EXPECT_EQ(track[0].drift_origin_s, 0.75);
    for (const double t : {0.0, 0.75, 4.0, 11.0})
        EXPECT_NEAR(kz4ap::oracle_frequency_hz(track, t), -1234.5 + 0.5 * (t - 0.75), 1e-12) << "t = " << t << " s";
    // a label of older files, with no drift, and a per-station label of a QSO that does not drift: one constant segment
    for (const std::string& extra : {std::string(), std::string(R"("qso_index": 3, "drift_hz_per_s": 0.0)")}) {
        const auto constant = kz4ap::bench::oracle_frequency_track(signal_with(extra), 0);
        ASSERT_EQ(constant.size(), 1u);
        EXPECT_EQ(constant[0].frequency_hz, -1234.5);
        EXPECT_EQ(constant[0].drift_hz_per_s, 0.0);
    }
}

TEST(ViterbiJsonOracle, AQsoIsOneSegmentPerTransmissionAtItsSendersOffset) {
    // a QSO label of two stations on one track: the over in progress, freq_offset_hz + the sender's offset_hz, with the
    // QSO's drift from the QSO's start_s, switching at each transmission's start_s (the first from 0 s) and held
    // through the silence after it (plan O3)
    const LabeledSignal qso = signal_with(
        R"("drift_hz_per_s": 0.25, "senders": [{"call": "K1ABC"}, {"call": "W9XYZ"}],
           "transmissions": [{"text": "CQ", "start_s": 0.75, "end_s": 3.0, "sender_index": 0, "offset_hz": 0.0},
                             {"text": "K1ABC", "start_s": 4.5, "end_s": 7.0, "sender_index": 1, "offset_hz": 18.5},
                             {"text": "R", "start_s": 8.25, "end_s": 9.0, "sender_index": 0, "offset_hz": 0.0}])");
    const auto track = kz4ap::bench::oracle_frequency_track(qso, 2);
    ASSERT_EQ(track.size(), 3u);
    const double starts[3] = {0.0, 4.5, 8.25};
    const double frequencies[3] = {-1234.5, -1234.5 + 18.5, -1234.5};
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(track[i].start_s, starts[i]) << i;
        EXPECT_EQ(track[i].frequency_hz, frequencies[i]) << i;
        EXPECT_EQ(track[i].drift_hz_per_s, 0.25) << i;
        EXPECT_EQ(track[i].drift_origin_s, 0.75) << i;
    }
    EXPECT_NEAR(kz4ap::oracle_frequency_hz(track, 4.0), -1234.5 + 0.25 * 3.25, 1e-12);  // A's over and its silence
    EXPECT_NEAR(kz4ap::oracle_frequency_hz(track, 4.5), -1216.0 + 0.25 * 3.75, 1e-12);  // B's from its start
    EXPECT_NEAR(kz4ap::oracle_frequency_hz(track, 8.0), -1216.0 + 0.25 * 7.25, 1e-12);
    EXPECT_NEAR(kz4ap::oracle_frequency_hz(track, 20.0), -1234.5 + 0.25 * 19.25, 1e-12);
}

TEST(ViterbiJsonOracle, APerStationLabelOfADriftingQsoIsRefusedByNumber) {
    // its start_s is the station's first over, not the QSO's start from which the drift is measured, so its carrier is
    // not known
    const LabeledSignal drifting = signal_with(R"("qso_index": 3, "fading_hz": 0.0, "drift_hz_per_s": 0.5)");
    const std::string why = track_refusal(drifting, 6);
    EXPECT_TRUE(contains(why, "signal 6")) << why;
    EXPECT_TRUE(contains(why, "drift")) << why;
    EXPECT_TRUE(contains(why, "per-station")) << why;
    EXPECT_EQ(track_refusal(signal_with(R"("drift_hz_per_s": 0.5)"), 7), "(accepted)");  // one station that drifts
}

TEST(ViterbiJsonGenie, TheFadingSpreadIsTheLabelsAndZeroForASignalThatDoesNotFade) {
    const ViterbiGenie fading = genie_of(signal_with(R"("fading_hz": 0.6)"), {GenieKind::Fading});
    ASSERT_TRUE(fading.fading_spread_hz.has_value());
    EXPECT_EQ(*fading.fading_spread_hz, 0.6);
    EXPECT_FALSE(fading.fist.has_value());
    // no fading_hz in the label of a single station (older files), or 0: the truth is no fading (a per-station label
    // of a QSO must say: APerStationLabelOfAQsoNeedsItsFadingSpreadAndNoDrift)
    for (const std::string& extra : {std::string(), std::string(R"("fading_hz": 0.0)")}) {
        const ViterbiGenie none = genie_of(signal_with(extra), {GenieKind::Fading});
        ASSERT_TRUE(none.fading_spread_hz.has_value());
        EXPECT_EQ(*none.fading_spread_hz, 0.0);
    }
}

TEST(ViterbiJsonGenie, TheFistOfAMachineIsExactToASample) {
    // machine keying at 25 WPM: a dot of 48 ms, medians 1, 3, 1, 3, 7 dots, no shifts; sigma 0 does not exist for the
    // fist, so each sigma is one sample over the kind's median length (heuristic): 0.002 s / (median x 0.048 s)
    const ViterbiGenie g = genie_of(signal_with(), {GenieKind::Fist});
    ASSERT_TRUE(g.fist.has_value());
    EXPECT_NEAR(g.fist->dot_ms, 48.0, 1e-12);
    EXPECT_EQ(g.fist->medians_dots, (std::array<double, 5>{{1.0, 3.0, 1.0, 3.0, 7.0}}));
    EXPECT_EQ(g.fist->shifts_dots, (std::array<double, 5>{{0.0, 0.0, 0.0, 0.0, 0.0}}));
    for (const double shift : g.fist->shifts_dots) EXPECT_FALSE(std::signbit(shift));  // 0, not -0
    const double medians[5] = {1.0, 3.0, 1.0, 3.0, 7.0};
    for (std::size_t kind = 0; kind < 5; ++kind)
        EXPECT_NEAR(g.fist->sigmas[kind], 0.002 / (medians[kind] * 0.048), 1e-12) << kind;
    EXPECT_FALSE(g.fading_spread_hz.has_value());
    // a coarser sample, a larger floor
    const ViterbiGenie coarse = genie_of(signal_with(), {GenieKind::Fist}, 0, 0.004);
    EXPECT_NEAR(coarse.fist->sigmas[0], 0.004 / 0.048, 1e-12);
}

TEST(ViterbiJsonGenie, TheFistOfAHandKeyHasItsStylesMediansSigmasAndTheImbalanceAsShifts) {
    // kz4ap_synth.keying.STYLES["hand"]: mu (0, 1.5, 0, 1.5, 2.0), sigma (0.15, 0.3, 0.2, 0.3, 0.2); the imbalance
    // lengthens every mark and shortens every space by 0.05 dot
    const ViterbiGenie g = genie_of(signal_with(R"("keying": "hand", "imbalance_dits": 0.05)"), {GenieKind::Fist});
    ASSERT_TRUE(g.fist.has_value());
    EXPECT_NEAR(g.fist->dot_ms, 48.0, 1e-12);
    const double medians[5] = {1.0, std::exp(1.5), 1.0, std::exp(1.5), std::exp(2.0)};
    const double sigmas[5] = {0.15, 0.3, 0.2, 0.3, 0.2};  // all above the floor at 25 WPM
    const double shifts[5] = {0.05, 0.05, -0.05, -0.05, -0.05};
    for (std::size_t kind = 0; kind < 5; ++kind) {
        EXPECT_NEAR(g.fist->medians_dots[kind], medians[kind], 1e-12) << kind;
        EXPECT_EQ(g.fist->sigmas[kind], sigmas[kind]) << kind;
        EXPECT_EQ(g.fist->shifts_dots[kind], shifts[kind]) << kind;
    }
}

TEST(ViterbiJsonGenie, AStylesSmallSigmasAreRaisedToASampleAtAFastSpeed) {
    // "computer": mu (0, 1.10, 0, 1.10, 1.94), sigma (0.05, 0.016, 0.05, 0.016, 0.008). At 100 WPM (a dot of 12 ms) one
    // sample (2 ms) is 1/6 of a dot, and the floors 0.002 / (median x 0.012) lie above every one of the style's sigmas.
    const ViterbiGenie g = genie_of(signal_with(R"("keying": "computer")", 100.0), {GenieKind::Fist});
    ASSERT_TRUE(g.fist.has_value());
    EXPECT_NEAR(g.fist->dot_ms, 12.0, 1e-12);
    const double medians[5] = {1.0, std::exp(1.10), 1.0, std::exp(1.10), std::exp(1.94)};
    for (std::size_t kind = 0; kind < 5; ++kind) {
        EXPECT_NEAR(g.fist->medians_dots[kind], medians[kind], 1e-12) << kind;
        EXPECT_NEAR(g.fist->sigmas[kind], 0.002 / (medians[kind] * 0.012), 1e-12) << kind;
    }
    // and at 25 WPM the style's own sigmas stand
    const ViterbiGenie slow = genie_of(signal_with(R"("keying": "computer")"), {GenieKind::Fist});
    const double own[5] = {0.05, 0.016, 0.05, 0.016, 0.008};
    for (std::size_t kind = 0; kind < 5; ++kind) EXPECT_EQ(slow.fist->sigmas[kind], own[kind]) << kind;
}

TEST(ViterbiJsonGenie, SeveralKindsAtOnceAndNoKindAtAll) {
    const ViterbiGenie all = genie_of(signal_with(R"("fading_hz": 2.0, "drift_hz_per_s": -1.5)"),
                                      {GenieKind::Fist, GenieKind::Fading});
    EXPECT_TRUE(all.fist.has_value());
    ASSERT_TRUE(all.fading_spread_hz.has_value());
    EXPECT_EQ(*all.fading_spread_hz, 2.0);
    const ViterbiGenie none = genie_of(signal_with(), {});
    EXPECT_FALSE(none.fist.has_value());
    EXPECT_FALSE(none.fading_spread_hz.has_value());
}

TEST(ViterbiJsonGenie, ALabelWithNoSingleTruthIsRefusedByNumber) {
    // a speed change: one dot cannot give both speeds
    const LabeledSignal changes = signal_with(R"("wpm_end": 35.0)");
    const std::string speed = genie_refusal(changes, {GenieKind::Fist}, 7);
    EXPECT_TRUE(contains(speed, "signal 7")) << speed;
    EXPECT_TRUE(contains(speed, "speed")) << speed;
    EXPECT_EQ(genie_refusal(changes, {GenieKind::Fading}, 7), "(accepted)");
    // a speed "change" to the same speed is no change
    EXPECT_EQ(genie_refusal(signal_with(R"("wpm_end": 25.0)"), {GenieKind::Fist}), "(accepted)");
    // a QSO label of two stations: each has its own fist, the fading is the signal's
    const LabeledSignal qso = signal_with(R"("senders": [{"call": "K1ABC"}, {"call": "W9XYZ"}])");
    const std::string why = genie_refusal(qso, {GenieKind::Fist}, 2);
    EXPECT_TRUE(contains(why, "signal 2")) << why;
    EXPECT_TRUE(contains(why, "stations")) << why;
    EXPECT_EQ(genie_refusal(qso, {GenieKind::Fading}, 2), "(accepted)");
    // a style the table does not have, a speed that is not a speed, a sample that is not a length
    const LabeledSignal fiddle = signal_with(R"("keying": "fiddle")");
    const std::string style = genie_refusal(fiddle, {GenieKind::Fist}, 4);
    EXPECT_TRUE(contains(style, "fiddle")) << style;
    EXPECT_EQ(genie_refusal(fiddle, {GenieKind::Fading}, 4), "(accepted)");
    EXPECT_NE(genie_refusal(signal_with("", 0.0), {GenieKind::Fist}, 5), "(accepted)");
    EXPECT_NE(genie_refusal(signal_with(), {GenieKind::Fist}, 5, 0.0), "(accepted)");
}

TEST(ViterbiJsonGenie, APerStationLabelOfAQsoNeedsItsFadingSpread) {
    // training/kz4ap_synth.generate.station_labels writes, for each station of a QSO, an entry that names its QSO
    // (qso_index) and, since Task 8b, its fading spread (the QSO's: each station fades through its own path of that
    // spread). (Its start_s is the station's first over, not the QSO's start, from which the QSO's drift is measured:
    // the oracle's frequency refuses such a label of a drifting QSO, APerStationLabelOfADriftingQsoIsRefusedByNumber.)
    const ViterbiGenie fading = genie_of(signal_with(R"("qso_index": 3, "fading_hz": 0.6)"), {GenieKind::Fading});
    ASSERT_TRUE(fading.fading_spread_hz.has_value());
    EXPECT_EQ(*fading.fading_spread_hz, 0.6);
    const ViterbiGenie none = genie_of(signal_with(R"("qso_index": 3, "fading_hz": 0.0)"), {GenieKind::Fading});
    ASSERT_TRUE(none.fading_spread_hz.has_value());
    EXPECT_EQ(*none.fading_spread_hz, 0.0);
    // written by the generator of before: no fading_hz, so whether the station fades is not known, and 0 would be a
    // guess that looks like a truth
    const LabeledSignal old = signal_with(R"("qso_index": 3)");
    const std::string why = genie_refusal(old, {GenieKind::Fading}, 5);
    EXPECT_TRUE(contains(why, "signal 5")) << why;
    EXPECT_TRUE(contains(why, "fading_hz")) << why;
    EXPECT_TRUE(contains(why, "per-station")) << why;
    EXPECT_EQ(genie_refusal(old, {GenieKind::Fist}, 5), "(accepted)");  // not what it reads
    // a drifting QSO's per-station label gives its fading spread and its fist (the oracle's frequency refuses it)
    const LabeledSignal drifting = signal_with(R"("qso_index": 3, "fading_hz": 0.0, "drift_hz_per_s": 0.5)");
    EXPECT_EQ(genie_refusal(drifting, {GenieKind::Fading, GenieKind::Fist}, 6), "(accepted)");
}

TEST(ViterbiJsonGenie, TheResultFileRecordsEachChannelsValues) {
    const ordered_json empty = to_json(ViterbiGenie{});
    EXPECT_TRUE(empty.at("fading_spread_hz").is_null());
    EXPECT_TRUE(empty.at("fist").is_null());
    for (const char* removed : {"frequency_hz", "frequency_time_s", "drift_hz_per_s"})
        EXPECT_FALSE(empty.contains(removed)) << removed;
    const ViterbiGenie g = genie_of(signal_with(R"("fading_hz": 0.6, "drift_hz_per_s": 0.5, "keying": "paddle")"),
                                    {GenieKind::Fist, GenieKind::Fading});
    const ordered_json j = to_json(g);
    EXPECT_EQ(j.at("fading_spread_hz"), 0.6);
    EXPECT_NEAR(j.at("fist").at("dot_ms").get<double>(), 48.0, 1e-12);
    EXPECT_EQ(j.at("fist").at("medians_dots").size(), 5u);
    EXPECT_EQ(j.at("fist").at("sigmas").size(), 5u);
    EXPECT_EQ(j.at("fist").at("shifts_dots").size(), 5u);
}

// ---- The statistics file ----

// Statistics with every member set to something that exercises the format: a count beyond 32 bits, floats that are not
// short in decimal, a double that is not (0.1 + 0.2), lines of every kind and end, and optional times set and absent.
SearchStats sample_stats() {
    SearchStats s;
    s.samples = 123456789012LL;
    s.ceiling_samples = 7;
    s.live_histogram = {5, 0, 170, 3, 1};
    s.live_mean_100ms = {1.0f, 0.1f, 1699.5f, 1.0f / 3.0f, 16777216.0f};
    s.live_max_100ms = {1.0f, 3.0f, 1700.0f, 2.0f, 1e-3f};
    NewbornLine first;  // line 0: the channel's first story, alive and leading
    first.birth_s = 0.002;
    first.kind = LineKind::Start;
    first.first_led_s = 0.002;
    first.largest_deficit_nats = 0.0;
    first.end = LineEnd::Alive;
    NewbornLine silence;  // a newborn from silence that never led and was beaten in its first element
    silence.birth_s = 12.345678901234567;
    silence.kind = LineKind::Silence;
    silence.largest_deficit_nats = 0.1 + 0.2;
    silence.end = LineEnd::Beam;
    silence.end_s = 12.350000000000001;
    silence.ended_in_first_element = true;
    NewbornLine rescue;  // a rescue newborn that led and later merged
    rescue.birth_s = 100.0;
    rescue.kind = LineKind::Rescue;
    rescue.first_led_s = 100.5;
    rescue.largest_deficit_nats = 17.347501234567891;
    rescue.end = LineEnd::Merge;
    rescue.end_s = 101.25;
    NewbornLine capped;  // one the ceiling removed
    capped.birth_s = 359.998;
    capped.kind = LineKind::Silence;
    capped.largest_deficit_nats = 39.999999999999993;
    capped.end = LineEnd::Ceiling;
    capped.end_s = 359.998;
    capped.ended_in_first_element = true;
    s.lines = {first, silence, rescue, capped};
    return s;
}

StatsFile sample_file() {
    StatsFile file;
    file.recording = "C2-fists-c05-s1.wav";
    file.config.sample_interval_s = 0.002;  // the statistics below hold windows of 50 samples
    file.config.beam_nats = 80.0;
    file.config.ceiling = 17000;
    file.config.stats = true;
    ChannelStats a;
    a.track_id = 1;
    a.label_index = 0;
    a.stats = sample_stats();
    ChannelStats b;
    b.track_id = 2;
    b.label_index = 1;  // statistics of a channel that searched nothing
    ChannelStats c;
    c.track_id = 9;  // a channel with no label
    c.stats = sample_stats();
    c.stats.lines.resize(2);
    file.channels = {a, b, c};
    return file;
}

std::string temp_file_name(const char* test) {
    const auto name = std::string("kz4ap_viterbi_json_") + test + ".stats.json";
    return (std::filesystem::temp_directory_path() / name).string();
}

TEST(ViterbiJsonStats, TheStatisticsRoundTripExactly) {
    const SearchStats stats = sample_stats();
    ChannelStats channel;
    channel.track_id = 4;
    channel.label_index = 3;
    channel.stats = stats;
    // in memory, and through the text a file holds
    const ordered_json j = to_json(channel);
    EXPECT_EQ(kz4ap::bench::channel_stats_from_json(j), channel);
    const ChannelStats back = kz4ap::bench::channel_stats_from_json(ordered_json::parse(j.dump()));
    EXPECT_EQ(back, channel);
    EXPECT_EQ(back.stats, stats);  // SearchStats' own comparison, every member exact
    EXPECT_EQ(back.stats.live_mean_100ms[1], 0.1f);
    EXPECT_EQ(back.stats.lines[1].largest_deficit_nats, 0.1 + 0.2);
    EXPECT_FALSE(back.stats.lines[1].first_led_s.has_value());
    EXPECT_EQ(*back.stats.lines[2].first_led_s, 100.5);
    // statistics with nothing in them
    ChannelStats empty;
    empty.track_id = 1;
    EXPECT_EQ(kz4ap::bench::channel_stats_from_json(ordered_json::parse(to_json(empty).dump())), empty);
}

TEST(ViterbiJsonStats, TheFileNamesItsKeysUnitsAndCodes) {
    const ordered_json j = to_json(sample_file());
    EXPECT_EQ(j.at("format"), "kz4ap-viterbi-search-stats");
    EXPECT_EQ(j.at("version"), 1);
    EXPECT_EQ(j.at("recording"), "C2-fists-c05-s1.wav");
    EXPECT_EQ(j.at("config").at("beam_nats"), 80.0);  // the configuration the run used
    EXPECT_EQ(j.at("config").at("stats"), true);
    EXPECT_EQ(j.at("line_kinds"), ordered_json::array({"Start", "Silence", "Rescue"}));
    EXPECT_EQ(j.at("line_ends"), ordered_json::array({"Alive", "Merge", "Beam", "Ceiling"}));
    // every member of SearchStats and of NewbornLine, by its C++ name, with its unit first
    const ordered_json& stats_fields = j.at("fields").at("SearchStats");
    for (const char* name : {"samples", "ceiling_samples", "live_histogram", "live_mean_100ms", "live_max_100ms",
                             "lines"})
        EXPECT_TRUE(stats_fields.contains(name)) << name;
    const ordered_json& line_fields = j.at("fields").at("NewbornLine");
    for (const char* name : {"birth_s", "kind", "first_led_s", "largest_deficit_nats", "end", "end_s",
                             "ended_in_first_element"})
        EXPECT_TRUE(line_fields.contains(name)) << name;
    EXPECT_EQ(line_fields.at("birth_s").get<std::string>().substr(0, 2), "s:");
    EXPECT_EQ(line_fields.at("largest_deficit_nats").get<std::string>().substr(0, 5), "nats:");
    EXPECT_EQ(stats_fields.at("live_mean_100ms").get<std::string>().substr(0, 8), "stories:");
    EXPECT_EQ(stats_fields.at("live_histogram").get<std::string>().substr(0, 7), "counts:");  // a reader splitting at the
    EXPECT_EQ(stats_fields.at("lines").get<std::string>().substr(0, 8), "columns:");          // first colon gets a kind
    // a channel: its place, the members of SearchStats, and the lines as columns named by NewbornLine's members
    const ordered_json& channel = j.at("channels").at(0);
    EXPECT_EQ(channel.at("track_id"), 1);
    EXPECT_EQ(channel.at("label_index"), 0);
    EXPECT_TRUE(j.at("channels").at(2).at("label_index").is_null());
    EXPECT_EQ(channel.at("samples"), 123456789012LL);
    EXPECT_EQ(channel.at("ceiling_samples"), 7);
    EXPECT_EQ(channel.at("live_histogram"), ordered_json::array({5, 0, 170, 3, 1}));
    const ordered_json& lines = channel.at("lines");
    EXPECT_EQ(lines.at("kind"), ordered_json::array({0, 1, 2, 1}));
    EXPECT_EQ(lines.at("end"), ordered_json::array({0, 2, 1, 3}));
    EXPECT_EQ(lines.at("first_led_s"), ordered_json::array({0.002, nullptr, 100.5, nullptr}));
    EXPECT_EQ(lines.at("ended_in_first_element"), ordered_json::array({false, true, false, true}));
    EXPECT_EQ(lines.at("birth_s").size(), 4u);
}

TEST(ViterbiJsonStats, TheStatisticsFileRoundTrips) {
    const StatsFile file = sample_file();
    const std::string path = temp_file_name("round_trip");
    kz4ap::bench::write_stats_file(path, file);
    // the file holds the same object as to_json gives (it is written channel by channel), as valid JSON
    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    EXPECT_EQ(ordered_json::parse(text), to_json(file));
    EXPECT_EQ(text.back(), '\n');
    const StatsFile back = kz4ap::bench::read_stats_file(path);
    std::filesystem::remove(path);
    EXPECT_EQ(back.recording, file.recording);
    EXPECT_EQ(to_json(back.config), to_json(file.config));
    ASSERT_EQ(back.channels.size(), file.channels.size());
    for (std::size_t i = 0; i < file.channels.size(); ++i) EXPECT_EQ(back.channels[i], file.channels[i]) << i;
}

// 2 s of white noise at 48 kHz (sigma 0.02 FS per complex sample, DEV2's): the statistics below do not depend on its
// values, only that the search runs.
std::vector<kz4ap::Sample> noise_of(double seconds, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> gauss(0.0f, 0.02f / std::sqrt(2.0f));
    std::vector<kz4ap::Sample> x(static_cast<std::size_t>(seconds * 48000.0));
    for (auto& sample : x) sample = {gauss(rng), gauss(rng)};
    return x;
}

TEST(ViterbiJsonStats, TheFileOfARunHoldsEachOracleChannelsStatistics) {
    // What --stats writes at the end of a run: oracle channel i is track i + 1 and label i, its statistics are the
    // engine's for that track, the configuration is the run's, and the file reads back the same.
    EngineConfig config;
    config.sample_rate = 48000;
    config.decoder.front_end = kz4ap::FrontEnd::Viterbi;
    config.oracle_frequencies_hz = {1203.0, -2501.0};
    config.viterbi.stats = true;
    config.viterbi.beam_nats = 35.0;
    kz4ap::EventBus bus;
    kz4ap::Engine engine(config, bus);
    engine.process(noise_of(2.0, 7));
    engine.finish();
    const StatsFile file = kz4ap::bench::collect_stats_file(engine, "noise.wav", config.viterbi, 2);
    EXPECT_EQ(file.recording, "noise.wav");
    EXPECT_EQ(file.config.beam_nats, 35.0);
    EXPECT_TRUE(file.config.stats);
    ASSERT_EQ(file.channels.size(), 2u);
    for (std::uint32_t track = 1; track <= 2; ++track) {
        const ChannelStats& channel = file.channels[track - 1];
        EXPECT_EQ(channel.track_id, track);
        ASSERT_TRUE(channel.label_index.has_value());
        EXPECT_EQ(*channel.label_index, static_cast<int>(track - 1));
        ASSERT_NE(engine.viterbi_stats(track), nullptr);
        EXPECT_EQ(channel.stats, *engine.viterbi_stats(track));
        EXPECT_GT(channel.stats.samples, 0);
        EXPECT_FALSE(channel.stats.lines.empty());
    }
    const std::string path = temp_file_name("collected");
    kz4ap::bench::write_stats_file(path, file);
    const StatsFile back = kz4ap::bench::read_stats_file(path);
    std::filesystem::remove(path);
    ASSERT_EQ(back.channels.size(), 2u);
    for (std::size_t i = 0; i < 2; ++i) EXPECT_EQ(back.channels[i], file.channels[i]) << i;
    // a channel the engine does not have, and one that is not the new decoder's
    EXPECT_THROW(kz4ap::bench::collect_stats_file(engine, "noise.wav", config.viterbi, 3), std::runtime_error);
    EngineConfig matched;
    matched.sample_rate = 48000;
    matched.decoder.front_end = kz4ap::FrontEnd::Matched;
    matched.oracle_frequencies_hz = {1203.0};
    kz4ap::EventBus other_bus;
    kz4ap::Engine other(matched, other_bus);
    other.finish();
    EXPECT_THROW(kz4ap::bench::collect_stats_file(other, "x.wav", config.viterbi, 1), std::runtime_error);
}

TEST(ViterbiJsonStats, AReaderRefusesWhatItCannotRead) {
    const ordered_json good = to_json(sample_file());
    ASSERT_EQ(refusal_of([&] { kz4ap::bench::stats_file_from_json(good); }), "(accepted)");
    const auto refused_after = [&](auto edit) {
        ordered_json j = good;
        edit(j);
        return refusal_of([&] { kz4ap::bench::stats_file_from_json(j); });
    };
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["format"] = "something-else"; }), "format"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["version"] = 2; }), "version"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j.erase("channels"); }), "channels"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0].erase("samples"); }), "samples"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["samples"] = "many"; }), "samples"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["live_histogram"][1] = 0.5; }),
                         "live_histogram"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["lines"].erase("end_s"); }), "end_s"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["lines"]["end"].push_back(0); }),
                         "lines"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["lines"]["kind"][0] = 3; }), "kind"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["lines"]["end"][0] = -1; }), "end"));
    EXPECT_TRUE(contains(
        refused_after([](ordered_json& j) { j["channels"][0]["lines"]["ended_in_first_element"][0] = 1; }),
        "ended_in_first_element"));
    EXPECT_NE(refusal_of([&] { kz4ap::bench::stats_file_from_json(ordered_json::array()); }), "(accepted)");
    EXPECT_THROW(kz4ap::bench::read_stats_file(temp_file_name("missing")), std::runtime_error);
}

TEST(ViterbiJsonStats, AStatisticThatIsNotFiniteIsNotWritten) {
    ChannelStats channel;
    channel.track_id = 1;
    channel.stats = sample_stats();
    channel.stats.lines[1].largest_deficit_nats = kNaN;
    EXPECT_TRUE(contains(refusal_of([&] { to_json(channel); }), "largest_deficit_nats"));
    channel.stats = sample_stats();
    channel.stats.live_mean_100ms[2] = std::numeric_limits<float>::infinity();
    EXPECT_TRUE(contains(refusal_of([&] { to_json(channel); }), "live_mean_100ms"));
    channel.stats = sample_stats();
    channel.stats.lines[2].end_s = -kInf;
    EXPECT_TRUE(contains(refusal_of([&] { to_json(channel); }), "end_s"));
    // and the file is not left behind, begun or finished
    const std::string path = temp_file_name("not_finite");
    StatsFile file = sample_file();
    file.channels[2].stats.lines[0].birth_s = kInf;  // the third channel: the file is begun when it is reached
    EXPECT_NE(refusal_of([&] { kz4ap::bench::write_stats_file(path, file); }), "(accepted)");
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_FALSE(std::filesystem::exists(path + ".partial"));
}

// ---- The statistics file, version 2: the summary ----

// Statistics that follow the search's own rules, which the summary checks: 120 samples are windows of 50, 50 and 20
// (round(0.1 s / 2 ms) = 50 samples), the histogram adds up to the samples. Five lines: the first story (led, merged), a
// newborn from silence the beam dropped in its first element (never led), a rescue newborn that led and is alive, a
// newborn from silence that led and was dropped by the beam after its first element, and a rescue newborn the ceiling
// removed in its first element (never led).
SearchStats counted_stats() {
    SearchStats s;
    s.samples = 120;
    s.ceiling_samples = 7;
    s.live_histogram = {60, 40, 20};
    s.live_mean_100ms = {10.0f, 20.0f, 40.0f};
    s.live_max_100ms = {12.0f, 25.0f, 41.0f};
    NewbornLine first;
    first.birth_s = 0.002;
    first.kind = LineKind::Start;
    first.first_led_s = 0.002;
    first.end = LineEnd::Merge;
    first.end_s = 0.1;
    NewbornLine dropped;
    dropped.birth_s = 0.2;
    dropped.kind = LineKind::Silence;
    dropped.largest_deficit_nats = 41.25;
    dropped.end = LineEnd::Beam;
    dropped.end_s = 0.21;
    dropped.ended_in_first_element = true;
    NewbornLine rescued;
    rescued.birth_s = 0.1;
    rescued.kind = LineKind::Rescue;
    rescued.first_led_s = 0.12;
    rescued.largest_deficit_nats = 12.5;
    NewbornLine overtaken;
    overtaken.birth_s = 0.05;
    overtaken.kind = LineKind::Silence;
    overtaken.first_led_s = 0.07;
    overtaken.largest_deficit_nats = 7.0 / 3.0;
    overtaken.end = LineEnd::Beam;
    overtaken.end_s = 0.15;
    NewbornLine capped;
    capped.birth_s = 0.3;
    capped.kind = LineKind::Rescue;
    capped.largest_deficit_nats = 3.0;
    capped.end = LineEnd::Ceiling;
    capped.end_s = 0.3;
    capped.ended_in_first_element = true;
    s.lines = {first, dropped, rescued, overtaken, capped};
    return s;
}

StatsSummaryFile counted_summary_file() {
    StatsSummaryFile file;
    file.recording = "A2-awgn-c05-0-s1.wav";
    file.config.sample_interval_s = 0.002;  // the statistics below hold windows of 50 samples
    file.config.beam_nats = 35.0;
    file.config.stats = true;
    file.channels.push_back(kz4ap::bench::summarize_channel(1, 0, counted_stats(), file.config.sample_interval_s));
    file.channels.push_back(kz4ap::bench::summarize_channel(2, 1, SearchStats{}, file.config.sample_interval_s));
    file.channels.push_back(kz4ap::bench::summarize_channel(9, std::nullopt, counted_stats(), file.config.sample_interval_s));
    return file;
}

TEST(ViterbiJsonSummary, TheSummaryCountsTheLinesByFateAndKeepsTheLinesThatLed) {
    const SearchStats stats = counted_stats();
    const kz4ap::bench::ChannelSummary c = kz4ap::bench::summarize_channel(4, 3, stats, 0.002);
    EXPECT_EQ(c.track_id, 4u);
    ASSERT_TRUE(c.label_index.has_value());
    EXPECT_EQ(*c.label_index, 3);
    EXPECT_EQ(c.samples, 120);
    EXPECT_EQ(c.ceiling_samples, 7);
    EXPECT_EQ(c.live_histogram, stats.live_histogram);
    EXPECT_DOUBLE_EQ(c.live_mean_stories, (50 * 10.0 + 50 * 20.0 + 20 * 40.0) / 120.0);
    EXPECT_EQ(c.live_max_stories, 41.0);
    EXPECT_EQ(c.lines, 5);
    // fates[kind][end][ended_in_first_element][led]; LineKind: Start 0, Silence 1, Rescue 2; LineEnd: Alive 0, Merge 1,
    // Beam 2, Ceiling 3
    EXPECT_EQ(c.fates[0][1][0][1], 1);  // the first story: merged, led
    EXPECT_EQ(c.fates[1][2][1][0], 1);  // dropped by the beam in its first element, never led
    EXPECT_EQ(c.fates[2][0][0][1], 1);  // the rescue newborn: alive, led
    EXPECT_EQ(c.fates[1][2][0][1], 1);  // the overtaken newborn: beam, after its first element, led
    EXPECT_EQ(c.fates[2][3][1][0], 1);  // removed by the ceiling in its first element, never led
    std::int64_t total = 0;
    for (const auto& by_end : c.fates)
        for (const auto& by_first : by_end)
            for (const auto& by_led : by_first)
                for (const std::int64_t n : by_led) total += n;
    EXPECT_EQ(total, c.lines);
    // the lines that led, with their ids, and nothing of the others
    EXPECT_EQ(c.led_line_ids, (std::vector<std::int64_t>{0, 2, 3}));
    ASSERT_EQ(c.led_lines.size(), 3u);
    EXPECT_EQ(c.led_lines[0], stats.lines[0]);
    EXPECT_EQ(c.led_lines[1], stats.lines[2]);
    EXPECT_EQ(c.led_lines[2], stats.lines[3]);
}

TEST(ViterbiJsonSummary, AChannelThatSearchedNothingHasAnEmptySummary) {
    const kz4ap::bench::ChannelSummary c = kz4ap::bench::summarize_channel(2, 1, SearchStats{}, 0.002);
    EXPECT_EQ(c.samples, 0);
    EXPECT_EQ(c.lines, 0);
    EXPECT_EQ(c.live_mean_stories, 0.0);
    EXPECT_EQ(c.live_max_stories, 0.0);
    EXPECT_TRUE(c.led_line_ids.empty() && c.led_lines.empty() && c.live_histogram.empty());
    kz4ap::bench::ChannelSummary expected;  // all zeros and empty, but for the channel's place
    expected.track_id = 2;
    expected.label_index = 1;
    EXPECT_EQ(c, expected);
}

TEST(ViterbiJsonSummary, TheWindowsOfTheLiveSeriesMustCoverTheSamples) {
    // 120 samples at 2 ms are windows of 50, 50 and 20 samples
    const auto refusal = [](auto edit) {
        SearchStats s = counted_stats();
        edit(s);
        return refusal_of([&] { kz4ap::bench::summarize_channel(1, 0, s, 0.002); });
    };
    EXPECT_TRUE(contains(refusal([](SearchStats& s) { s.live_mean_100ms.pop_back(); s.live_max_100ms.pop_back(); }),
                         "windows"));  // two windows hold at most 100 samples
    EXPECT_TRUE(contains(refusal([](SearchStats& s) { s.live_mean_100ms.push_back(1.0f); s.live_max_100ms.push_back(1.0f); }),
                         "windows"));  // a fourth window would be empty
    EXPECT_TRUE(contains(refusal([](SearchStats& s) { s.live_max_100ms.pop_back(); }), "windows"));
    EXPECT_TRUE(contains(refusal([](SearchStats& s) { s.samples = 0; }), "windows"));  // windows without samples
    // a sample of 4 ms makes windows of 25 samples: the same 120 samples would be 5 windows
    EXPECT_NE(refusal_of([&] { kz4ap::bench::summarize_channel(1, 0, counted_stats(), 0.004); }), "(accepted)");
    // a line whose kind or end is not in the legends
    SearchStats odd = counted_stats();
    odd.lines[1].kind = static_cast<LineKind>(7);
    EXPECT_NE(refusal_of([&] { kz4ap::bench::summarize_channel(1, 0, odd, 0.002); }), "(accepted)");
    odd = counted_stats();
    odd.lines[1].end = static_cast<LineEnd>(4);
    EXPECT_NE(refusal_of([&] { kz4ap::bench::summarize_channel(1, 0, odd, 0.002); }), "(accepted)");
}

TEST(ViterbiJsonSummary, TheSummaryOfAFileSummarizesEachChannelAtTheFilesSample) {
    StatsFile file;
    file.recording = "x.wav";
    file.config.sample_interval_s = 0.004;  // windows of 25 samples: 60 samples are 25, 25 and 10
    ChannelStats channel;
    channel.track_id = 3;
    channel.label_index = 2;
    channel.stats = counted_stats();
    channel.stats.samples = 60;
    channel.stats.live_histogram = {60};
    file.channels = {channel};
    const StatsSummaryFile summary = kz4ap::bench::summarize(file);
    EXPECT_EQ(summary.recording, "x.wav");
    EXPECT_EQ(to_json(summary.config), to_json(file.config));
    ASSERT_EQ(summary.channels.size(), 1u);
    EXPECT_EQ(summary.channels[0], kz4ap::bench::summarize(channel, 0.004));
    EXPECT_DOUBLE_EQ(summary.channels[0].live_mean_stories, (25 * 10.0 + 25 * 20.0 + 10 * 40.0) / 60.0);
}

TEST(ViterbiJsonSummary, TheSummaryRoundTripsExactly) {
    const StatsSummaryFile file = counted_summary_file();
    // a channel, in memory and through the text a file holds
    const ordered_json j = to_json(file.channels[0]);
    EXPECT_EQ(kz4ap::bench::channel_summary_from_json(j), file.channels[0]);
    EXPECT_EQ(kz4ap::bench::channel_summary_from_json(ordered_json::parse(j.dump())), file.channels[0]);
    // the whole file: written channel by channel, the same object as to_json gives; read back, every member exact
    const std::string path = temp_file_name("summary_round_trip");
    kz4ap::bench::write_stats_summary_file(path, file);
    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    EXPECT_EQ(ordered_json::parse(text), to_json(file));
    EXPECT_EQ(text.back(), '\n');
    const StatsSummaryFile back = kz4ap::bench::read_stats_summary_file(path);
    std::filesystem::remove(path);
    EXPECT_FALSE(std::filesystem::exists(path + ".partial"));
    EXPECT_EQ(back.recording, file.recording);
    EXPECT_EQ(to_json(back.config), to_json(file.config));
    ASSERT_EQ(back.channels.size(), file.channels.size());
    for (std::size_t i = 0; i < file.channels.size(); ++i) EXPECT_EQ(back.channels[i], file.channels[i]) << i;
    EXPECT_EQ(back.channels[0].led_lines[2].largest_deficit_nats, 7.0 / 3.0);  // a double that is not short in decimal
}

TEST(ViterbiJsonSummary, TheSummaryFileNamesItsKeysUnitsAndCodes) {
    const ordered_json j = to_json(counted_summary_file());
    EXPECT_EQ(j.at("format"), "kz4ap-viterbi-search-stats");
    EXPECT_EQ(j.at("version"), 2);
    EXPECT_EQ(j.at("recording"), "A2-awgn-c05-0-s1.wav");
    EXPECT_EQ(j.at("config").at("beam_nats"), 35.0);
    EXPECT_EQ(j.at("line_kinds"), ordered_json::array({"Start", "Silence", "Rescue"}));
    EXPECT_EQ(j.at("line_ends"), ordered_json::array({"Alive", "Merge", "Beam", "Ceiling"}));
    EXPECT_EQ(j.at("fate_axes"), ordered_json::array({"kind", "end", "ended_in_first_element", "led"}));
    // what each key of a channel is, by its name, the unit or the kind of value first
    const ordered_json& fields = j.at("fields").at("ChannelSummary");
    for (const char* name : {"samples", "ceiling_samples", "live_histogram", "live_mean_stories", "live_max_stories",
                             "lines", "fates", "led_lines"})
        EXPECT_TRUE(fields.contains(name)) << name;
    EXPECT_EQ(fields.at("samples").get<std::string>().substr(0, 6), "count:");
    EXPECT_EQ(fields.at("live_histogram").get<std::string>().substr(0, 7), "counts:");
    EXPECT_EQ(fields.at("live_mean_stories").get<std::string>().substr(0, 8), "stories:");
    EXPECT_EQ(fields.at("live_max_stories").get<std::string>().substr(0, 8), "stories:");
    EXPECT_EQ(fields.at("fates").get<std::string>().substr(0, 7), "counts:");
    EXPECT_EQ(fields.at("led_lines").get<std::string>().substr(0, 8), "columns:");
    const ordered_json& line_fields = j.at("fields").at("NewbornLine");
    for (const char* name : {"line", "birth_s", "kind", "first_led_s", "largest_deficit_nats", "end", "end_s",
                             "ended_in_first_element"})
        EXPECT_TRUE(line_fields.contains(name)) << name;
    EXPECT_EQ(line_fields.at("line").get<std::string>().substr(0, 6), "count:");
    // a channel
    ASSERT_EQ(j.at("channels").size(), 3u);
    const ordered_json& channel = j.at("channels").at(0);
    EXPECT_EQ(channel.at("track_id"), 1);
    EXPECT_EQ(channel.at("label_index"), 0);
    EXPECT_TRUE(j.at("channels").at(2).at("label_index").is_null());
    EXPECT_EQ(channel.at("samples"), 120);
    EXPECT_EQ(channel.at("ceiling_samples"), 7);
    EXPECT_EQ(channel.at("live_histogram"), ordered_json::array({60, 40, 20}));
    EXPECT_EQ(channel.at("live_max_stories"), 41.0);
    EXPECT_EQ(channel.at("lines"), 5);
    ASSERT_EQ(channel.at("fates").size(), 3u);
    ASSERT_EQ(channel.at("fates").at(0).size(), 4u);
    ASSERT_EQ(channel.at("fates").at(0).at(0).size(), 2u);
    ASSERT_EQ(channel.at("fates").at(0).at(0).at(0).size(), 2u);
    EXPECT_EQ(channel.at("fates").at(0).at(1).at(0).at(1), 1);  // [Start][Merge][not first element][led]
    const ordered_json& led = channel.at("led_lines");
    EXPECT_EQ(led.at("line"), ordered_json::array({0, 2, 3}));
    EXPECT_EQ(led.at("kind"), ordered_json::array({0, 2, 1}));
    EXPECT_EQ(led.at("end"), ordered_json::array({1, 0, 2}));
    EXPECT_EQ(led.at("first_led_s"), ordered_json::array({0.002, 0.12, 0.07}));
    EXPECT_EQ(led.at("end_s"), ordered_json::array({0.1, nullptr, 0.15}));
    EXPECT_EQ(led.at("ended_in_first_element"), ordered_json::array({false, false, false}));
    // a channel that searched nothing has empty columns, not missing ones
    const ordered_json& empty = j.at("channels").at(1);
    EXPECT_EQ(empty.at("samples"), 0);
    EXPECT_TRUE(empty.at("led_lines").at("line").empty());
    EXPECT_TRUE(empty.at("live_histogram").empty());
}

TEST(ViterbiJsonSummary, AReaderRefusesWhatItCannotRead) {
    const ordered_json good = to_json(counted_summary_file());
    ASSERT_EQ(refusal_of([&] { kz4ap::bench::stats_summary_file_from_json(good); }), "(accepted)");
    const auto refused_after = [&](auto edit) {
        ordered_json j = good;
        edit(j);
        return refusal_of([&] { kz4ap::bench::stats_summary_file_from_json(j); });
    };
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["format"] = "something-else"; }), "format"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["version"] = 1; }), "version"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["version"] = 3; }), "version"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j.erase("channels"); }), "channels"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0].erase("fates"); }), "fates"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["samples"] = "many"; }), "samples"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["live_mean_stories"] = "ten"; }),
                         "live_mean_stories"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["live_histogram"][1] = 0.5; }),
                         "live_histogram"));
    // the fates: 3 x 4 x 2 x 2 counts that add up to the lines
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["fates"].erase(2); }), "fates"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["fates"][0][0][0].push_back(0); }),
                         "fates"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["fates"][0][0][0][0] = -1; }), "fates"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["fates"][0][0][0][0] = 1; }), "fates"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["lines"] = 6; }), "lines"));
    // the lines that led: equal columns, ascending ids below the number of lines, each with a time it first led
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["led_lines"].erase("end_s"); }),
                         "end_s"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["led_lines"]["end"].push_back(0); }),
                         "led_lines"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["led_lines"]["line"] = {0, 3, 2}; }),
                         "line"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["led_lines"]["line"] = {0, 2, 5}; }),
                         "line"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["led_lines"]["first_led_s"][1] = nullptr; }),
                         "first_led_s"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["led_lines"]["kind"][0] = 3; }), "kind"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["led_lines"]["end"][0] = -1; }), "end"));
    EXPECT_NE(refusal_of([&] { kz4ap::bench::stats_summary_file_from_json(ordered_json::array()); }), "(accepted)");
    EXPECT_THROW(kz4ap::bench::read_stats_summary_file(temp_file_name("missing_summary")), std::runtime_error);
}

TEST(ViterbiJsonSummary, AReaderChecksWhatTheWriterGuarantees) {
    // The channel in counted_summary_file(): 120 samples; 5 lines, 3 of which led: line 0 (Start, merged, not in its first
    // element), line 2 (Rescue, alive) and line 3 (Silence, dropped by the beam after its first element). A file edited
    // or assembled by hand must not pass with columns that contradict the counts.
    const ordered_json good = to_json(counted_summary_file());
    const auto refused_after = [&](auto edit) {
        ordered_json j = good;
        edit(j);
        return refusal_of([&] { kz4ap::bench::stats_summary_file_from_json(j); });
    };
    // the lines that led, by kind, end and first element, are the fates' led counts
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["led_lines"]["kind"][0] = 1; }), "fates"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["led_lines"]["kind"][1] = 1; }), "fates"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["led_lines"]["end"][1] = 3; }), "fates"));
    EXPECT_TRUE(contains(
        refused_after([](ordered_json& j) { j["channels"][0]["led_lines"]["ended_in_first_element"][2] = true; }),
        "fates"));
    // a swap of two lines' ends keeps the counts of ends and does not keep the counts of (kind, end)
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) {
                             std::swap(j["channels"][0]["led_lines"]["end"][1], j["channels"][0]["led_lines"]["end"][2]);
                         }),
                         "fates"));
    // the histogram counts the samples, each in one bin
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["live_histogram"][0] = 59; }),
                         "live_histogram"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["live_histogram"][2] = 21; }),
                         "live_histogram"));
    EXPECT_TRUE(contains(
        refused_after([](ordered_json& j) { j["channels"][0]["live_histogram"] = ordered_json::array({-60, 160, 20}); }),
        "live_histogram"));  // adds up to the 120 samples, and a count cannot be negative
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["samples"] = 121; }), "live_histogram"));
    // and what is consistent is accepted: the channel that searched nothing, and the others as they are
    EXPECT_EQ(refused_after([](ordered_json&) {}), "(accepted)");
}

// ---- A file whose last step fails ----

TEST(ViterbiJsonStats, AFileThatCannotBeRenamedIntoPlaceIsKeptWholeAndTheMessageSaysWhere) {
    // The destination is a folder: everything is written, and only the rename into place fails. The finished file is
    // not thrown away (a person can rename it), whichever form was written; a file that could not be written still
    // leaves nothing behind (AStatisticThatIsNotFiniteIsNotWritten).
    const std::string summary_target = temp_file_name("summary_is_a_folder");
    const std::string lines_target = temp_file_name("lines_is_a_folder");
    std::filesystem::create_directories(summary_target);
    std::filesystem::create_directories(lines_target);
    const StatsSummaryFile summary = counted_summary_file();
    const StatsFile lines = sample_file();
    const auto failure = [](auto call) {
        try {
            call();
        } catch (const std::runtime_error& e) {
            return std::string(e.what());
        }
        return std::string("(accepted)");
    };
    const std::string summary_why = failure([&] { kz4ap::bench::write_stats_summary_file(summary_target, summary); });
    const std::string lines_why = failure([&] { kz4ap::bench::write_stats_file(lines_target, lines); });
    EXPECT_TRUE(contains(summary_why, summary_target + ".partial")) << summary_why;
    EXPECT_TRUE(contains(lines_why, lines_target + ".partial")) << lines_why;
    ASSERT_TRUE(std::filesystem::exists(summary_target + ".partial"));
    ASSERT_TRUE(std::filesystem::exists(lines_target + ".partial"));
    // each reads back as the file that was to be written
    const StatsSummaryFile summary_back = kz4ap::bench::read_stats_summary_file(summary_target + ".partial");
    const StatsFile lines_back = kz4ap::bench::read_stats_file(lines_target + ".partial");
    ASSERT_EQ(summary_back.channels.size(), summary.channels.size());
    for (std::size_t i = 0; i < summary.channels.size(); ++i) EXPECT_EQ(summary_back.channels[i], summary.channels[i]) << i;
    ASSERT_EQ(lines_back.channels.size(), lines.channels.size());
    for (std::size_t i = 0; i < lines.channels.size(); ++i) EXPECT_EQ(lines_back.channels[i], lines.channels[i]) << i;
    EXPECT_TRUE(std::filesystem::is_directory(summary_target));
    EXPECT_TRUE(std::filesystem::is_directory(lines_target));
    std::filesystem::remove(summary_target + ".partial");
    std::filesystem::remove(lines_target + ".partial");
    std::filesystem::remove(summary_target);
    std::filesystem::remove(lines_target);
}

TEST(ViterbiJsonSummary, TheTwoVersionsHaveTheirOwnReaders) {
    const ordered_json summary = to_json(counted_summary_file());
    const std::string why = refusal_of([&] { kz4ap::bench::stats_file_from_json(summary); });
    EXPECT_TRUE(contains(why, "version")) << why;
    const ordered_json full = to_json(sample_file());
    const std::string other = refusal_of([&] { kz4ap::bench::stats_summary_file_from_json(full); });
    EXPECT_TRUE(contains(other, "version")) << other;
}

TEST(ViterbiJsonSummary, AStatisticThatIsNotFiniteIsNotSummarized) {
    kz4ap::bench::ChannelSummary c = kz4ap::bench::summarize_channel(1, 0, counted_stats(), 0.002);
    c.live_mean_stories = kNaN;
    EXPECT_TRUE(contains(refusal_of([&] { to_json(c); }), "live_mean_stories"));
    c = kz4ap::bench::summarize_channel(1, 0, counted_stats(), 0.002);
    c.led_lines[1].first_led_s = kInf;
    EXPECT_TRUE(contains(refusal_of([&] { to_json(c); }), "first_led_s"));
    // and the file is not left behind, begun or finished
    StatsSummaryFile file = counted_summary_file();
    file.channels[2].led_lines[0].birth_s = kInf;  // the third channel: the file is begun when it is reached
    const std::string path = temp_file_name("summary_not_finite");
    EXPECT_NE(refusal_of([&] { kz4ap::bench::write_stats_summary_file(path, file); }), "(accepted)");
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_FALSE(std::filesystem::exists(path + ".partial"));
}

// A finished run of the engine on two oracle channels of white noise (2 s), with the statistics on.
struct FinishedRun {
    EngineConfig config;
    kz4ap::EventBus bus;
    std::unique_ptr<kz4ap::Engine> engine;
    explicit FinishedRun(unsigned seed) {
        config.sample_rate = 48000;
        config.decoder.front_end = kz4ap::FrontEnd::Viterbi;
        config.oracle_frequencies_hz = {1203.0, -2501.0};
        config.viterbi.stats = true;
        config.viterbi.beam_nats = 35.0;
        engine = std::make_unique<kz4ap::Engine>(config, bus);
        engine->process(noise_of(2.0, seed));
        engine->finish();
    }
};

TEST(ViterbiJsonSummary, TheSummaryOfARunIsTheSummaryOfItsFullStatistics) {
    FinishedRun run(11);
    const StatsFile full = kz4ap::bench::collect_stats_file(*run.engine, "noise.wav", run.config.viterbi, 2);
    const StatsSummaryFile reduced = kz4ap::bench::summarize(full);
    const StatsSummaryFile direct = kz4ap::bench::collect_stats_summary(*run.engine, "noise.wav", run.config.viterbi, 2);
    EXPECT_EQ(direct.recording, "noise.wav");
    EXPECT_EQ(to_json(direct.config), to_json(reduced.config));
    ASSERT_EQ(direct.channels.size(), 2u);
    for (std::size_t i = 0; i < 2; ++i) {
        EXPECT_EQ(direct.channels[i], reduced.channels[i]) << i;
        const auto& c = direct.channels[i];
        EXPECT_EQ(c.track_id, i + 1);
        EXPECT_GT(c.samples, 700);
        EXPECT_EQ(c.lines, static_cast<std::int64_t>(full.channels[i].stats.lines.size()));
        EXPECT_GE(c.led_lines.size(), 1u);  // the first story led
        EXPECT_GT(c.live_mean_stories, 0.0);
        EXPECT_LE(c.live_mean_stories, c.live_max_stories);
    }
    // the file a run writes is the same object, in either form, and reads back as what was collected
    const std::string summary_path = temp_file_name("run_summary");
    const std::string lines_path = temp_file_name("run_lines");
    kz4ap::bench::write_run_stats(summary_path, *run.engine, "noise.wav", run.config.viterbi, 2,
                                  kz4ap::bench::StatsDetail::Summary);
    kz4ap::bench::write_run_stats(lines_path, *run.engine, "noise.wav", run.config.viterbi, 2,
                                  kz4ap::bench::StatsDetail::Lines);
    const StatsSummaryFile summary_back = kz4ap::bench::read_stats_summary_file(summary_path);
    const StatsFile lines_back = kz4ap::bench::read_stats_file(lines_path);
    std::filesystem::remove(summary_path);
    std::filesystem::remove(lines_path);
    ASSERT_EQ(summary_back.channels.size(), 2u);
    ASSERT_EQ(lines_back.channels.size(), 2u);
    for (std::size_t i = 0; i < 2; ++i) {
        EXPECT_EQ(summary_back.channels[i], direct.channels[i]) << i;
        EXPECT_EQ(lines_back.channels[i], full.channels[i]) << i;
    }
    // a channel the engine does not have is refused, and no file is left
    const std::string missing_path = temp_file_name("run_missing_channel");
    EXPECT_THROW(kz4ap::bench::write_run_stats(missing_path, *run.engine, "noise.wav", run.config.viterbi, 3,
                                               kz4ap::bench::StatsDetail::Summary),
                 std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(missing_path));
    EXPECT_FALSE(std::filesystem::exists(missing_path + ".partial"));
}

TEST(ViterbiJsonSummary, TwoRunsOfTheEngineGiveTheSameStatisticsInBothForms) {
    // Determinism (the plan's constraint): the same recording and settings give the same output.
    FinishedRun a(5), b(5), other(6);
    const auto lines_of = [](FinishedRun& r) {
        return kz4ap::bench::collect_stats_file(*r.engine, "noise.wav", r.config.viterbi, 2);
    };
    const auto summary_of = [](FinishedRun& r) {
        return kz4ap::bench::collect_stats_summary(*r.engine, "noise.wav", r.config.viterbi, 2);
    };
    EXPECT_EQ(to_json(lines_of(a)), to_json(lines_of(b)));
    EXPECT_EQ(to_json(summary_of(a)), to_json(summary_of(b)));
    EXPECT_NE(to_json(summary_of(a)), to_json(summary_of(other)));  // and the test can tell a different recording
}

std::filesystem::path fixture_path(const char* name) { return std::filesystem::path(KZ4AP_SEARCH_STATS_FIXTURES) / name; }

TEST(ViterbiJsonSummary, TheCommittedSummaryIsTheSummaryOfTheCommittedFullFile) {
    // training/tests/data/search-stats/: a real run of two oracle channels over 1.05 s, written once in each form. The
    // Python reductions (training/tests/test_search_stats.py) turn the full file into the summary too, so this ties the
    // bench's reducer to the reductions on a real file without Python at test time.
    const StatsFile full = kz4ap::bench::read_stats_file(fixture_path("tiny.v1.stats.json"));
    const StatsSummaryFile committed = kz4ap::bench::read_stats_summary_file(fixture_path("tiny.v2.stats.json"));
    const StatsSummaryFile reduced = kz4ap::bench::summarize(full);
    EXPECT_EQ(reduced.recording, committed.recording);
    EXPECT_EQ(to_json(reduced.config), to_json(committed.config));
    ASSERT_EQ(reduced.channels.size(), 2u);
    ASSERT_EQ(committed.channels.size(), 2u);
    for (std::size_t i = 0; i < 2; ++i) {
        kz4ap::bench::ChannelSummary mine = reduced.channels[i];
        const kz4ap::bench::ChannelSummary& theirs = committed.channels[i];
        EXPECT_DOUBLE_EQ(mine.live_mean_stories, theirs.live_mean_stories) << i;  // a sum of products: to the last digit
        mine.live_mean_stories = theirs.live_mean_stories;
        EXPECT_EQ(mine, theirs) << i;
        EXPECT_EQ(mine.samples, 523);
    }
    EXPECT_EQ(reduced.channels[0].led_line_ids, (std::vector<std::int64_t>{0, 89}));
}

TEST(ViterbiJsonSummary, StatsDetailNeedsStatsAndTheNewDecoder) {
    ViterbiOptions options;
    options.stats_detail = true;
    const std::string alone = refusal_of([&] { check_viterbi_options(true, true, options); });
    EXPECT_TRUE(contains(alone, "--stats-detail")) << alone;
    EXPECT_TRUE(contains(alone, "--stats")) << alone;
    options.stats = true;
    EXPECT_EQ(refusal_of([&] { check_viterbi_options(true, true, options); }), "(accepted)");
    EXPECT_NE(refusal_of([&] { check_viterbi_options(true, false, options); }), "(accepted)");  // --stats needs --oracle
    EXPECT_TRUE(contains(refusal_of([&] { check_viterbi_options(false, true, options); }), "--decoder viterbi"));
    options.stats_detail = false;
    EXPECT_EQ(refusal_of([&] { check_viterbi_options(true, true, options); }), "(accepted)");
}

}  // namespace
