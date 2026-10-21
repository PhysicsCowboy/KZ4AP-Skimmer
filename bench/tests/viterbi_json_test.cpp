// The new decoder in the bench (bench/src/viterbi_json.cpp): its configuration as JSON and as --set, the genie values
// from the labels, the rules among the options, and the statistics file with its round trip.
#include "viterbi_json.hpp"

#include "kz4ap/engine.hpp"
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
#include <optional>
#include <stdexcept>
#include <string>
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
    [[maybe_unused]] const auto& [piece_s, dot_min_ms, speed_ratio, speed_count, factor_ratio, factor_count,
                                  fist_medians_dots, fist_sigmas, fist_shifts_dots, beam_nats, ceiling,
                                  fading_spread_hz, block_loss_db, keying_rise_s, over_rate_per_s, rescue_epsilon,
                                  word_end_p, over_end_q, level_min_db, level_max_db, quadrature_order,
                                  hiss_half_width_hz, hiss_percentile, hiss_average_s, tracker_window_s,
                                  tracker_hop_s, tracker_search_hz, tracker_gate_db, tracker_drift_sd_hz_per_s,
                                  tracker_drift_noise, genie_frequency_offset_hz, genie_frequency_drift_hz_per_s,
                                  genie_fading_spread_hz, stats] = c;
}

const std::vector<std::string> kExpectedNames = {
    "piece_s", "dot_min_ms", "speed_ratio", "speed_count", "factor_ratio", "factor_count", "fist_medians_dots",
    "fist_sigmas", "fist_shifts_dots", "beam_nats", "ceiling", "fading_spread_hz", "block_loss_db", "keying_rise_s",
    "over_rate_per_s", "rescue_epsilon", "word_end_p", "over_end_q", "level_min_db", "level_max_db",
    "quadrature_order", "hiss_half_width_hz", "hiss_percentile", "hiss_average_s", "tracker_window_s",
    "tracker_hop_s", "tracker_search_hz", "tracker_gate_db", "tracker_drift_sd_hz_per_s", "tracker_drift_noise",
    "genie_frequency_offset_hz", "genie_frequency_drift_hz_per_s", "genie_fading_spread_hz", "stats"};

// ---- The configuration and --set ----

TEST(ViterbiJson, TheConfigurationHasEveryFieldInDeclarationOrder) {
    every_field_is_named(ViterbiConfig{});
    const ordered_json j = to_json(ViterbiConfig{});
    std::vector<std::string> names;
    for (const auto& item : j.items()) names.push_back(item.key());
    EXPECT_EQ(names, kExpectedNames);
    EXPECT_EQ(kz4ap::bench::viterbi_config_field_names(), kExpectedNames);
    // the defaults, each in its own kind and unit
    EXPECT_EQ(j.at("piece_s"), 0.002);
    EXPECT_EQ(j.at("dot_min_ms"), 12.0);
    EXPECT_TRUE(j.at("speed_count").is_number_integer());
    EXPECT_EQ(j.at("speed_count"), 32);
    EXPECT_EQ(j.at("factor_count"), 23);
    EXPECT_EQ(j.at("fist_medians_dots"), ordered_json::array({1.0, 3.0, 1.0, 3.0, 7.0}));
    EXPECT_EQ(j.at("fist_sigmas"), ordered_json::array({0.3, 0.3, 0.3, 0.3, 0.3}));
    EXPECT_EQ(j.at("fist_shifts_dots"), ordered_json::array({0.0, 0.0, 0.0, 0.0, 0.0}));
    EXPECT_EQ(j.at("beam_nats"), 40.0);
    EXPECT_EQ(j.at("ceiling"), 1700);
    EXPECT_EQ(j.at("fading_spread_hz"), 1.0);
    EXPECT_EQ(j.at("level_max_db"), 85.0);
    EXPECT_EQ(j.at("quadrature_order"), 5);
    EXPECT_EQ(j.at("tracker_gate_db"), 9.0);
    EXPECT_TRUE(j.at("genie_frequency_offset_hz").is_null());
    EXPECT_EQ(j.at("genie_frequency_drift_hz_per_s"), 0.0);
    EXPECT_TRUE(j.at("genie_fading_spread_hz").is_null());
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
    apply_setting(c, "genie_frequency_offset_hz=-3.5");
    ASSERT_TRUE(c.genie_frequency_offset_hz.has_value());
    EXPECT_EQ(*c.genie_frequency_offset_hz, -3.5);
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
    c.piece_s = 0.004;
    c.dot_min_ms = 10.0;
    c.speed_ratio = 1.05;
    c.speed_count = 61;
    c.factor_ratio = 1.2;
    c.factor_count = 12;
    c.fist_medians_dots = {{1.0, 3.2, 1.0, 3.4, 7.5}};
    c.fist_sigmas = {{0.1, 0.12, 0.15, 0.2, 0.25}};
    c.fist_shifts_dots = {{0.05, 0.05, -0.05, -0.05, -0.05}};
    c.beam_nats = 80.0;
    c.ceiling = 17000;
    c.fading_spread_hz = 0.3;
    c.block_loss_db = 0.25;
    c.keying_rise_s = 0.0;
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
    c.tracker_window_s = 2.0;
    c.tracker_hop_s = 1.0;
    c.tracker_search_hz = 10.0;
    c.tracker_gate_db = 11.0;
    c.tracker_drift_sd_hz_per_s = 0.5;
    c.tracker_drift_noise = 2.5e-4;
    c.genie_frequency_offset_hz = -4.25;
    c.genie_frequency_drift_hz_per_s = 0.125;
    c.genie_fading_spread_hz = 0.6;
    c.stats = true;
    const ordered_json j = to_json(c);
    const ordered_json defaults = to_json(ViterbiConfig{});
    for (const auto& item : j.items()) EXPECT_NE(item.value(), defaults.at(item.key())) << item.key();
    const ViterbiConfig back = kz4ap::bench::viterbi_config_from_json(ordered_json::parse(j.dump()));
    EXPECT_EQ(to_json(back), j);
    EXPECT_EQ(back.speed_count, 61);
    EXPECT_EQ(back.fist_sigmas, c.fist_sigmas);
    EXPECT_EQ(back.genie_frequency_offset_hz, c.genie_frequency_offset_hz);
    EXPECT_TRUE(back.stats);
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

TEST(ViterbiJson, GenieKindsAreNamedAsTheCommandLineNamesThem) {
    EXPECT_EQ(parse_genie_kind("fist"), GenieKind::Fist);
    EXPECT_EQ(parse_genie_kind("frequency"), GenieKind::Frequency);
    EXPECT_EQ(parse_genie_kind("fading"), GenieKind::Fading);
    for (const GenieKind kind : {GenieKind::Fist, GenieKind::Frequency, GenieKind::Fading})
        EXPECT_EQ(parse_genie_kind(kz4ap::bench::genie_kind_name(kind)), kind);
    const std::string why = refusal_of([&] { parse_genie_kind("loud"); });
    EXPECT_TRUE(contains(why, "loud")) << why;
    EXPECT_TRUE(contains(why, "fist, frequency or fading")) << why;
    EXPECT_NE(refusal_of([&] { parse_genie_kind("Fist"); }), "(accepted)");
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

// The genie of `kinds` from a label, as the bench gives it: the label the index-th of its file, the piece 2 ms.
ViterbiGenie genie_of(const LabeledSignal& label, std::initializer_list<GenieKind> kinds, std::size_t index = 0,
                      double piece_s = 0.002) {
    const std::vector<GenieKind> list(kinds);
    return genie_from_label(label, index, list, piece_s);
}

std::string genie_refusal(const LabeledSignal& label, std::initializer_list<GenieKind> kinds, std::size_t index = 0,
                          double piece_s = 0.002) {
    return refusal_of([&] { genie_of(label, kinds, index, piece_s); });
}

TEST(ViterbiJsonGenie, TheFrequencyIsTheLabelsAtItsStartWithItsDrift) {
    const LabeledSignal s = signal_with(R"("drift_hz_per_s": 0.5)");
    const ViterbiGenie g = genie_of(s, {GenieKind::Frequency}, 3);
    ASSERT_TRUE(g.frequency_hz.has_value());
    EXPECT_EQ(*g.frequency_hz, -1234.5);
    EXPECT_EQ(g.frequency_time_s, 0.75);
    EXPECT_EQ(g.drift_hz_per_s, 0.5);
    EXPECT_FALSE(g.fading_spread_hz.has_value());
    EXPECT_FALSE(g.fist.has_value());
    // the engine's convention end to end: at stream time t the carrier is at f + drift (t - start_s), the generator's
    // angle 2 pi f t + pi drift (t - start_s)^2, from the channel's center (an FFT bin's center at 48 kHz)
    const double center_hz = -1242.1875;
    const ViterbiConfig c = kz4ap::viterbi::with_genie(ViterbiConfig{}, g, center_hz);
    for (const double t : {0.75, 4.0, 11.0}) {
        const double from_center_hz = *c.genie_frequency_offset_hz + c.genie_frequency_drift_hz_per_s * t;
        EXPECT_NEAR(from_center_hz, (-1234.5 + 0.5 * (t - 0.75)) - center_hz, 1e-12) << "t = " << t << " s";
    }
}

TEST(ViterbiJsonGenie, TheFadingSpreadIsTheLabelsAndZeroForASignalThatDoesNotFade) {
    const ViterbiGenie fading = genie_of(signal_with(R"("fading_hz": 0.6)"), {GenieKind::Fading});
    ASSERT_TRUE(fading.fading_spread_hz.has_value());
    EXPECT_EQ(*fading.fading_spread_hz, 0.6);
    EXPECT_FALSE(fading.frequency_hz.has_value());
    EXPECT_FALSE(fading.fist.has_value());
    // no fading_hz in the label (older files, a QSO's per-station labels), or 0: the truth is no fading
    for (const std::string& extra : {std::string(), std::string(R"("fading_hz": 0.0)")}) {
        const ViterbiGenie none = genie_of(signal_with(extra), {GenieKind::Fading});
        ASSERT_TRUE(none.fading_spread_hz.has_value());
        EXPECT_EQ(*none.fading_spread_hz, 0.0);
    }
}

TEST(ViterbiJsonGenie, TheFistOfAMachineIsExactToAPiece) {
    // machine keying at 25 WPM: a dot of 48 ms, medians 1, 3, 1, 3, 7 dots, no shifts; sigma 0 does not exist for the
    // fist, so each sigma is one piece over the kind's median length (heuristic): 0.002 s / (median x 0.048 s)
    const ViterbiGenie g = genie_of(signal_with(), {GenieKind::Fist});
    ASSERT_TRUE(g.fist.has_value());
    EXPECT_NEAR(g.fist->dot_ms, 48.0, 1e-12);
    EXPECT_EQ(g.fist->medians_dots, (std::array<double, 5>{{1.0, 3.0, 1.0, 3.0, 7.0}}));
    EXPECT_EQ(g.fist->shifts_dots, (std::array<double, 5>{{0.0, 0.0, 0.0, 0.0, 0.0}}));
    for (const double shift : g.fist->shifts_dots) EXPECT_FALSE(std::signbit(shift));  // 0, not -0
    const double medians[5] = {1.0, 3.0, 1.0, 3.0, 7.0};
    for (std::size_t kind = 0; kind < 5; ++kind)
        EXPECT_NEAR(g.fist->sigmas[kind], 0.002 / (medians[kind] * 0.048), 1e-12) << kind;
    EXPECT_FALSE(g.frequency_hz.has_value());
    EXPECT_FALSE(g.fading_spread_hz.has_value());
    // a coarser piece, a larger floor
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

TEST(ViterbiJsonGenie, AStylesSmallSigmasAreRaisedToAPieceAtAFastSpeed) {
    // "computer": mu (0, 1.10, 0, 1.10, 1.94), sigma (0.05, 0.016, 0.05, 0.016, 0.008). At 100 WPM (a dot of 12 ms) one
    // piece (2 ms) is 1/6 of a dot, and the floors 0.002 / (median x 0.012) lie above every one of the style's sigmas.
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
                                      {GenieKind::Fist, GenieKind::Frequency, GenieKind::Fading});
    EXPECT_TRUE(all.fist.has_value());
    ASSERT_TRUE(all.frequency_hz.has_value());
    EXPECT_EQ(all.drift_hz_per_s, -1.5);
    ASSERT_TRUE(all.fading_spread_hz.has_value());
    EXPECT_EQ(*all.fading_spread_hz, 2.0);
    const ViterbiGenie none = genie_of(signal_with(), {});
    EXPECT_FALSE(none.fist.has_value());
    EXPECT_FALSE(none.frequency_hz.has_value());
    EXPECT_FALSE(none.fading_spread_hz.has_value());
}

TEST(ViterbiJsonGenie, ALabelWithNoSingleTruthIsRefusedByNumber) {
    // a speed change: one dot cannot give both speeds
    const LabeledSignal changes = signal_with(R"("wpm_end": 35.0)");
    const std::string speed = genie_refusal(changes, {GenieKind::Fist}, 7);
    EXPECT_TRUE(contains(speed, "signal 7")) << speed;
    EXPECT_TRUE(contains(speed, "speed")) << speed;
    EXPECT_EQ(genie_refusal(changes, {GenieKind::Frequency, GenieKind::Fading}, 7), "(accepted)");
    // a speed "change" to the same speed is no change
    EXPECT_EQ(genie_refusal(signal_with(R"("wpm_end": 25.0)"), {GenieKind::Fist}), "(accepted)");
    // a QSO label of two stations: each has its own carrier and fist, the fading is the signal's
    const LabeledSignal qso = signal_with(R"("senders": [{"call": "K1ABC"}, {"call": "W9XYZ"}])");
    for (const GenieKind kind : {GenieKind::Fist, GenieKind::Frequency}) {
        const std::string why = genie_refusal(qso, {kind}, 2);
        EXPECT_TRUE(contains(why, "signal 2")) << why;
        EXPECT_TRUE(contains(why, "stations")) << why;
    }
    EXPECT_EQ(genie_refusal(qso, {GenieKind::Fading}, 2), "(accepted)");
    // a style the table does not have, a speed that is not a speed, a piece that is not a length
    const LabeledSignal fiddle = signal_with(R"("keying": "fiddle")");
    const std::string style = genie_refusal(fiddle, {GenieKind::Fist}, 4);
    EXPECT_TRUE(contains(style, "fiddle")) << style;
    EXPECT_EQ(genie_refusal(fiddle, {GenieKind::Frequency}, 4), "(accepted)");
    EXPECT_NE(genie_refusal(signal_with("", 0.0), {GenieKind::Fist}, 5), "(accepted)");
    EXPECT_NE(genie_refusal(signal_with(), {GenieKind::Fist}, 5, 0.0), "(accepted)");
}

TEST(ViterbiJsonGenie, TheResultFileRecordsEachChannelsValues) {
    const ordered_json empty = to_json(ViterbiGenie{});
    EXPECT_TRUE(empty.at("frequency_hz").is_null());
    EXPECT_EQ(empty.at("frequency_time_s"), 0.0);
    EXPECT_EQ(empty.at("drift_hz_per_s"), 0.0);
    EXPECT_TRUE(empty.at("fading_spread_hz").is_null());
    EXPECT_TRUE(empty.at("fist").is_null());
    const ViterbiGenie g = genie_of(signal_with(R"("fading_hz": 0.6, "drift_hz_per_s": 0.5, "keying": "paddle")"),
                                    {GenieKind::Fist, GenieKind::Frequency, GenieKind::Fading});
    const ordered_json j = to_json(g);
    EXPECT_EQ(j.at("frequency_hz"), -1234.5);
    EXPECT_EQ(j.at("frequency_time_s"), 0.75);
    EXPECT_EQ(j.at("drift_hz_per_s"), 0.5);
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
    s.pieces = 123456789012LL;
    s.ceiling_pieces = 7;
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
    for (const char* name : {"pieces", "ceiling_pieces", "live_histogram", "live_mean_100ms", "live_max_100ms",
                             "lines"})
        EXPECT_TRUE(stats_fields.contains(name)) << name;
    const ordered_json& line_fields = j.at("fields").at("NewbornLine");
    for (const char* name : {"birth_s", "kind", "first_led_s", "largest_deficit_nats", "end", "end_s",
                             "ended_in_first_element"})
        EXPECT_TRUE(line_fields.contains(name)) << name;
    EXPECT_EQ(line_fields.at("birth_s").get<std::string>().substr(0, 2), "s:");
    EXPECT_EQ(line_fields.at("largest_deficit_nats").get<std::string>().substr(0, 5), "nats:");
    EXPECT_EQ(stats_fields.at("live_mean_100ms").get<std::string>().substr(0, 8), "stories:");
    // a channel: its place, the members of SearchStats, and the lines as columns named by NewbornLine's members
    const ordered_json& channel = j.at("channels").at(0);
    EXPECT_EQ(channel.at("track_id"), 1);
    EXPECT_EQ(channel.at("label_index"), 0);
    EXPECT_TRUE(j.at("channels").at(2).at("label_index").is_null());
    EXPECT_EQ(channel.at("pieces"), 123456789012LL);
    EXPECT_EQ(channel.at("ceiling_pieces"), 7);
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
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0].erase("pieces"); }), "pieces"));
    EXPECT_TRUE(contains(refused_after([](ordered_json& j) { j["channels"][0]["pieces"] = "many"; }), "pieces"));
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

}  // namespace
