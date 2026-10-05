// The bank decoder's JSON forms (bench/src/bank_json.cpp) against Python's: float repr and round, json.dumps's
// formatting, the configuration as runner.decode stores it, and ChannelResult.to_json() byte for byte on golden
// streams (engine/tests/data/bank/channel.json "dumps", the prototype's json.dumps(result.to_json())).
#include "bank_json.hpp"

#include "golden.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using kz4ap::bench::ordered_json;
using kz4ap::bench::py_dumps;
using kz4ap::bench::py_float_repr;
using kz4ap::bench::py_round;

TEST(BankJson, FloatsAsPythonWritesThem) {
    // Python: json.dumps of these values (expected strings from Python 3.12).
    const std::vector<std::pair<double, std::string>> cases = {
        {0.0, "0.0"},
        {-0.0, "-0.0"},
        {1e-05, "1e-05"},
        {0.0001, "0.0001"},
        {1e16, "1e+16"},
        {1e15, "1000000000000000.0"},
        {123456789012345678.0, "1.2345678901234568e+17"},
        {0.1, "0.1"},
        {2.5, "2.5"},
        {1.5e-07, "1.5e-07"},
        {12.448, "12.448"},
        {-3.25e-300, "-3.25e-300"},
        {5e-324, "5e-324"},
        {1.7976931348623157e308, "1.7976931348623157e+308"},
        {100.0, "100.0"},
        {0.021333333333333333, "0.021333333333333333"},
    };
    for (const auto& [x, want] : cases) EXPECT_EQ(py_float_repr(x), want);
}

TEST(BankJson, RoundAsPythonRounds) {
    EXPECT_EQ(py_round(2.675, 2), 2.67);           // 2.675 is 2.67499999... in binary
    EXPECT_EQ(py_round(0.03125, 4), 0.0312);       // an exact tie: to even
    EXPECT_EQ(py_dumps(py_round(-0.00001, 4)), "-0.0");
    EXPECT_EQ(py_round(7.873666666666667, 4), 7.8737);
    EXPECT_EQ(py_round(0.20453742026841254, 6), 0.204537);
    EXPECT_EQ(py_round(1e20, 4), 1e20);
    EXPECT_EQ(py_round(0.5, 0), 0.0);
    EXPECT_EQ(py_round(1.5, 0), 2.0);
    EXPECT_TRUE(std::isnan(py_round(std::numeric_limits<double>::quiet_NaN(), 4)));
}

TEST(BankJson, DumpsAsPythonDumps) {
    ordered_json j = ordered_json::object();
    j["a\"b\\"] = "x\ny\xc3\xa9\xf0\x9f\x98\x80\x01\x7f";
    j["n"] = ordered_json::array({std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                                  -std::numeric_limits<double>::infinity(), nullptr, true, 3});
    j["e"] = ordered_json::array();
    j["o"] = ordered_json::object();
    EXPECT_EQ(py_dumps(j),
              "{\"a\\\"b\\\\\": \"x\\ny\\u00e9\\ud83d\\ude00\\u0001\\u007f\", "
              "\"n\": [NaN, Infinity, -Infinity, null, true, 3], \"e\": [], \"o\": {}}");
}

TEST(BankJson, ConfigAsRunnerDecodeStoresIt) {
    const auto g = kz4ap::test::load_golden("channel");
    const ordered_json cfg = kz4ap::bench::to_json(kz4ap::bank::BankConfig{});
    // the prototype's ProtoConfig() dump with B4a's fields in dits in place of the three in seconds they replaced and
    // B4b's guard margin and mask-bias table (pinned in BankConfig.PlanBDefaults), then Plan B's fields (noise_stuck_s,
    // B3; B4a's overrides in seconds, unset), which the prototype lacks
    std::string want = g.at("config_dumps").get<std::string>();
    const kz4ap::bank::BankConfig defaults;
    for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
             {"\"guard_margin_s\": 0.02", "\"guard_margin_s\": 0.0048"},
             {"\"mask_bias\": " + py_dumps(ordered_json(kz4ap::bank::kStage1MaskBias)),
              "\"mask_bias\": " + py_dumps(ordered_json(defaults.mask_bias))},
             {"\"rekey_after_s\": 0.8", "\"rekey_after_dits\": 16.7"},
             {"\"periodicity_windows_s\": [2.0, 5.0, 10.0]", "\"periodicity_windows_dits\": [41.7, 104.0, 208.0]"},
             {"\"rekey_timeout_s\": 2.0", "\"rekey_timeout_ratio\": 2.5"}}) {
        const auto at = want.find(from);
        ASSERT_NE(at, std::string::npos) << from;
        want.replace(at, from.size(), to);
    }
    ASSERT_EQ(want.back(), '}');
    want.insert(want.size() - 1, ", \"noise_stuck_s\": 8.0, \"rekey_after_s\": 0.0, \"rekey_timeout_s\": 0.0, "
                                 "\"periodicity_windows_s\": [], \"periodicity_window_mode\": \"per_candidate\", "
                                 "\"periodicity_unselected_windows_s\": [2.0, 5.0, 10.0]");
    EXPECT_EQ(py_dumps(cfg), want);
    // and back
    EXPECT_EQ(py_dumps(kz4ap::bench::to_json(kz4ap::bench::bank_config_from_json(cfg))), py_dumps(cfg));
    kz4ap::bank::BankConfig c;
    kz4ap::bench::set_config_value(c, "fit_memory", ordered_json(24));
    kz4ap::bench::set_config_value(c, "x_on_values", ordered_json::array());
    kz4ap::bench::set_config_value(c, "noise_method", ordered_json("branch"));
    kz4ap::bench::set_config_value(c, "comb_teeth", ordered_json(5));
    kz4ap::bench::set_config_value(c, "noise_stuck_s", ordered_json(16.0));
    EXPECT_EQ(c.noise_stuck_s, 16.0);
    kz4ap::bench::set_config_value(c, "periodicity_windows_s", ordered_json::array({2.0, 5.0, 10.0}));
    kz4ap::bench::set_config_value(c, "rekey_after_s", ordered_json(0.8));
    EXPECT_EQ(c.periodicity_windows_s, (std::vector<double>{2.0, 5.0, 10.0}));
    EXPECT_EQ(c.rekey_after_s, 0.8);
    kz4ap::bench::set_config_value(c, "guard_margin_s", ordered_json(0.02));
    kz4ap::bench::set_config_value(c, "mask_bias", ordered_json(kz4ap::bank::kStage1MaskBias));
    EXPECT_EQ(c.guard_margin_s, kz4ap::bank::kStage1GuardMarginS);
    EXPECT_EQ(c.mask_bias, kz4ap::bank::kStage1MaskBias);
    kz4ap::bench::set_config_value(c, "periodicity_window_mode", ordered_json("shared"));
    EXPECT_EQ(c.periodicity_window_mode, "shared");
    EXPECT_EQ(c.fit_memory, 24.0);
    EXPECT_TRUE(c.x_on_values.empty());
    EXPECT_EQ(c.noise_method, "branch");
    EXPECT_EQ(c.comb_teeth, 5);
    EXPECT_THROW(kz4ap::bench::set_config_value(c, "no_such_field", ordered_json(1)), std::invalid_argument);
    EXPECT_THROW(kz4ap::bench::set_config_value(c, "comb_teeth", ordered_json(4.5)), std::invalid_argument);
}

class BankJsonResult : public ::testing::TestWithParam<std::string> {};

TEST_P(BankJsonResult, ToJsonDumpsAsThePrototypes) {
    const auto g = kz4ap::test::load_golden("channel");
    const auto u = kz4ap::test::channel_stream(g, GetParam());
    // the prototype's time constants in seconds, guard margin and mask-bias table, set explicitly (Plan B's B4a made
    // the default nominal dits, B4b changed the margin and the table)
    const kz4ap::bank::BankConfig cfg = kz4ap::test::stage1_config();
    kz4ap::bank::BankChannel ch(cfg, g.at("streams").at(GetParam()).at("rate_hz").get<double>(),
                                kz4ap::bank::fixed_timing(cfg, 0.8, 2.0, {2.0, 5.0, 10.0}));
    ch.push(u);
    ch.finish();
    EXPECT_EQ(py_dumps(kz4ap::bench::to_json(ch.result())),
              g.at("results").at(GetParam()).at("dumps").get<std::string>());
}

// "zero_pad" (1 s of exact zeros, then a station) left this list in Plan B's B3: its golden pinned the prototype's
// exact-zero defect (it keyed nothing); BankChannel.LeadingExactZerosDecodeAsTheStationAlone (engine tests) now
// requires it to decode as the station alone.
INSTANTIATE_TEST_SUITE_P(Streams, BankJsonResult, ::testing::Values("clean", "turnover", "noise_tail"),
                         [](const auto& info) { return info.param; });

}  // namespace
