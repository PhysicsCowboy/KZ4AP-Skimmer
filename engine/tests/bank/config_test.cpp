#include "kz4ap/bank/bank_config.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "golden.hpp"

namespace {

void expect_vector(const std::vector<double>& actual, const nlohmann::json& golden, const char* name) {
    ASSERT_EQ(actual.size(), golden.size()) << name;
    for (std::size_t k = 0; k < actual.size(); ++k)
        EXPECT_EQ(actual[k], golden.at(k).get<double>()) << name << "[" << k << "]";
}

}  // namespace

// ProtoConfig() as JSON (dataclasses.asdict), one EXPECT per member.
TEST(BankConfig, DefaultsEqualThePrototypes) {
    const auto g = kz4ap::test::load_golden("config");
    const kz4ap::bank::BankConfig c{};
    EXPECT_EQ(c.min_wpm, g.at("min_wpm").get<double>());
    EXPECT_EQ(c.max_wpm, g.at("max_wpm").get<double>());
    EXPECT_EQ(c.ladder_step, g.at("ladder_step").get<double>());
    EXPECT_EQ(c.length_dits, g.at("length_dits").get<double>());
    EXPECT_EQ(c.block_s, g.at("block_s").get<double>());
    EXPECT_EQ(c.noise_method, g.at("noise_method").get<std::string>());
    // mask_bias and guard_margin_s: changed by Plan B's B4b (PlanBDefaults below); stage 1's values are kept as
    // kStage1MaskBias and kStage1GuardMarginS
    expect_vector(kz4ap::bank::kStage1MaskBias, g.at("mask_bias"), "mask_bias");
    EXPECT_EQ(c.noise_tau_s, g.at("noise_tau_s").get<double>());
    EXPECT_EQ(c.noise_warmup_s, g.at("noise_warmup_s").get<double>());
    EXPECT_EQ(c.noise_guard, g.at("noise_guard").get<double>());
    EXPECT_EQ(c.neighbor_guard, g.at("neighbor_guard").get<double>());
    EXPECT_EQ(c.segment_s, g.at("segment_s").get<double>());
    EXPECT_EQ(c.spectrum_smoothing_hz, g.at("spectrum_smoothing_hz").get<double>());
    EXPECT_EQ(kz4ap::bank::kStage1GuardMarginS, g.at("guard_margin_s").get<double>());
    EXPECT_EQ(c.min_clean_fraction, g.at("min_clean_fraction").get<double>());
    EXPECT_EQ(c.amplitude_tau_s, g.at("amplitude_tau_s").get<double>());
    EXPECT_EQ(c.prior_key_down, g.at("prior_key_down").get<double>());
    EXPECT_EQ(c.hysteresis_nats, g.at("hysteresis_nats").get<double>());
    EXPECT_EQ(c.squelch_a, g.at("squelch_a").get<double>());
    EXPECT_EQ(c.squelch_ref_s, g.at("squelch_ref_s").get<double>());
    EXPECT_EQ(c.squelch_exponent, g.at("squelch_exponent").get<double>());
    EXPECT_EQ(c.false_marks_per_s, g.at("false_marks_per_s").get<double>());
    expect_vector(c.x_on_values, g.at("x_on_values"), "x_on_values");
    EXPECT_EQ(c.release_probability, g.at("release_probability").get<double>());
    // rekey_after_s: replaced by rekey_after_dits (Plan B, B4a; PlanBDefaults below); the name is now an override, unset
    EXPECT_EQ(g.at("rekey_after_s").get<double>(), 0.8);
    EXPECT_EQ(c.seed_memory_rekeys, g.at("seed_memory_rekeys").get<double>());
    EXPECT_EQ(c.fit_memory, g.at("fit_memory").get<double>());
    EXPECT_EQ(c.t_grid_step, g.at("t_grid_step").get<double>());
    expect_vector(c.q_grid, g.at("q_grid"), "q_grid");
    expect_vector(c.w_grid, g.at("w_grid"), "w_grid");
    expect_vector(c.tg_grid, g.at("tg_grid"), "tg_grid");
    EXPECT_EQ(c.sigma_ln_mark, g.at("sigma_ln_mark").get<double>());
    EXPECT_EQ(c.sigma_ln_space, g.at("sigma_ln_space").get<double>());
    EXPECT_EQ(c.outlier_prior, g.at("outlier_prior").get<double>());
    expect_vector(c.outlier_range_s, g.at("outlier_range_s"), "outlier_range_s");
    EXPECT_EQ(c.prior_sigma_ln, g.at("prior_sigma_ln").get<double>());
    EXPECT_EQ(c.refine_iterations, g.at("refine_iterations").get<int>());
    EXPECT_EQ(c.min_fit_weight, g.at("min_fit_weight").get<double>());
    EXPECT_EQ(c.periodicity_method, g.at("periodicity_method").get<std::string>());
    // periodicity_windows_s: the default windows again since Plan B's B4d (owner, 2026-10-06; B4a had replaced them by
    // windows in dits, now variants) until the owner's amendment of 2026-10-07 dropped the 2 s window (PlanBDefaults
    // below); stage 1's values are kept as kStage1PeriodicityWindowsS
    expect_vector(kz4ap::bank::kStage1PeriodicityWindowsS, g.at("periodicity_windows_s"), "periodicity_windows_s");
    EXPECT_EQ(c.periodicity_update_s, g.at("periodicity_update_s").get<double>());
    EXPECT_EQ(c.periodicity_rate_hz, g.at("periodicity_rate_hz").get<double>());
    EXPECT_EQ(c.comb_teeth, g.at("comb_teeth").get<int>());
    EXPECT_EQ(c.comb_width, g.at("comb_width").get<double>());
    EXPECT_EQ(c.spectrum_nulls, g.at("spectrum_nulls").get<int>());
    EXPECT_EQ(c.spectrum_null_width, g.at("spectrum_null_width").get<double>());
    EXPECT_EQ(c.spectrum_front_floor_db, g.at("spectrum_front_floor_db").get<double>());
    EXPECT_EQ(c.comb_confidence_min, g.at("comb_confidence_min").get<double>());
    EXPECT_EQ(c.edge_confidence_min, g.at("edge_confidence_min").get<double>());
    EXPECT_EQ(c.spectrum_confidence_min, g.at("spectrum_confidence_min").get<double>());
    EXPECT_EQ(c.eligibility_tolerance, g.at("eligibility_tolerance").get<double>());
    EXPECT_EQ(c.switch_persistence, g.at("switch_persistence").get<int>());
    EXPECT_EQ(c.quality_tie_nats, g.at("quality_tie_nats").get<double>());
    EXPECT_EQ(c.text_tie_nats, g.at("text_tie_nats").get<double>());
    EXPECT_EQ(c.text_window_chars, g.at("text_window_chars").get<int>());
    EXPECT_EQ(c.text_separation_nats, g.at("text_separation_nats").get<double>());
    EXPECT_EQ(c.new_over_min_s, g.at("new_over_min_s").get<double>());
    EXPECT_EQ(c.new_over_gaps, g.at("new_over_gaps").get<double>());
    // rekey_timeout_s: replaced by rekey_timeout_ratio (Plan B, B4a); now an override, unset
    EXPECT_EQ(g.at("rekey_timeout_s").get<double>(), 2.0);
    EXPECT_EQ(c.fresh_fit_min_obs, g.at("fresh_fit_min_obs").get<int>());
    EXPECT_EQ(c.correction_reach_s, g.at("correction_reach_s").get<double>());
    EXPECT_EQ(g.size(), 61u) << "ProtoConfig has a field that BankConfig does not mirror";
}

// Plan B's fields, which the prototype does not have (docs/signal-processing.md section 8c).
TEST(BankConfig, PlanBDefaults) {
    const kz4ap::bank::BankConfig c{};
    // B3: the stuck-level recovery after 4 tau_n of non-zero input with no accepted tap (heuristic), s
    EXPECT_EQ(c.noise_stuck_s, 8.0);
    EXPECT_EQ(c.noise_stuck_s, 4.0 * c.noise_tau_s);
    // B4a: time constants in nominal dits, equal to the prototype's seconds at 25 WPM (T = 48 ms; stage-2 spec
    // section 3.1): W_min = 16.7 dits (0.8 s / 48 ms = 16.67), the time-out 2.5 W_min (2 s / 0.8 s), the variants'
    // periodicity windows 41.7, 104 and 208 dits (2, 5 and 10 s / 48 ms = 41.67, 104.2, 208.3)
    EXPECT_EQ(c.rekey_after_dits, 16.7);
    EXPECT_NEAR(c.rekey_after_dits * 0.048 / 0.8, 1.0, 0.0025);  // 0.2%
    EXPECT_EQ(c.rekey_timeout_ratio, 2.5);
    EXPECT_EQ(c.periodicity_windows_dits, (std::vector<double>{41.7, 104.0, 208.0}));
    // B4b: the spectrum mask's guard margin 0.5 L_1, L_1 = 0.8 dits at max_wpm = 0.8 x 1.2 s / 100 = 9.6 ms (stage-2
    // spec section 3.3), s
    EXPECT_EQ(c.guard_margin_s, 0.0048);
    EXPECT_NEAR(c.guard_margin_s, 0.5 * c.length_dits * 1.2 / c.max_wpm, 1e-15);
    // B4b, adopted by B4d: b_mask,k re-measured at that margin on 200 seeds (kz4ap-noise-mask white, seeds 1001-1200,
    // 60 s each; docs/signal-processing.md A.8c), one per branch, rounded to four decimals
    const std::vector<double> b4d = {0.8381, 0.8304, 0.8273, 0.8221, 0.8179, 0.8144, 0.8103, 0.808,
                                     0.8051, 0.8027, 0.8007, 0.7985, 0.7967, 0.7949, 0.7934, 0.7921,
                                     0.7909, 0.7897, 0.7887, 0.7877, 0.7869, 0.7861, 0.7854, 0.7847,
                                     0.7842, 0.7836, 0.7831, 0.7827, 0.7823, 0.782, 0.7816, 0.7812};
    EXPECT_EQ(c.mask_bias.size(), 32u);
    EXPECT_EQ(c.mask_bias, b4d);
    EXPECT_EQ(kz4ap::bank::kStage1MaskBias.size(), 32u);
    // B4g: stage 1's re-key wait and time-out in seconds are the default again (owner, 2026-10-06, option a')
    EXPECT_EQ(c.rekey_after_s, 0.8);
    EXPECT_EQ(c.rekey_timeout_s, 2.0);
    // B4d: the periodicity windows in seconds for every candidate (class 2, a latency; owner, 2026-10-06), stage 1's
    // 2, 5 and 10 s until the owner's amendment of 2026-10-07, which dropped the 2 s window (below 10 WPM its false
    // match at about a third of the dit set T_P): 5 and 10 s; the per-candidate (B4a) and shared (B4a-C) windows in
    // dits are variants, not the default; the shared variant's windows before a selection are stage 1's
    EXPECT_EQ(c.periodicity_window_mode, "seconds");
    EXPECT_EQ(c.periodicity_windows_s, (std::vector<double>{5.0, 10.0}));
    EXPECT_EQ(kz4ap::bank::kStage1PeriodicityWindowsS, (std::vector<double>{2.0, 5.0, 10.0}));
    // B4d's re-key clock fixes: variants since B4g, off
    EXPECT_FALSE(c.rekey_clear_moves_stretch);
    EXPECT_FALSE(c.rekey_timeout_from_first_mark);
    // B4d: the re-key stretch's lead before the first provisional mark, one word gap of the branch's dits (heuristic)
    EXPECT_EQ(c.rekey_lead_dits, 7.0);
    // B4e: the re-key wait counted in the station's marks, 8 provisional marks (heuristic), with a time-out of 7 s of
    // channel time for every branch: 8 marks of PARIS (a mark every 50/14 dits) at the 5 WPM floor (dit 240 ms) take
    // 6.9 s (derived), rounded to 7 s (owner, 2026-10-06, option d)
    EXPECT_FALSE(c.rekey_wait_in_marks);  // a variant since B4g
    EXPECT_EQ(c.rekey_marks, 8);
    EXPECT_EQ(c.rekey_marks_timeout_s, 7.0);
    EXPECT_NEAR(8.0 * 50.0 / 14.0 * 1.2 / c.min_wpm, 6.857, 0.001);
    EXPECT_EQ(std::round(8.0 * 50.0 / 14.0 * 1.2 / c.min_wpm), c.rekey_marks_timeout_s);
    // B4f: the guards on the counted provisional marks; variants since B4g, off
    EXPECT_FALSE(c.rekey_guard_filter_full);
    EXPECT_FALSE(c.rekey_guard_min_length);
    EXPECT_EQ(c.periodicity_unselected_windows_s, (std::vector<double>{2.0, 5.0, 10.0}));
    const std::vector<double> stage1_s = {2.0, 5.0, 10.0};
    for (std::size_t i = 0; i < stage1_s.size(); ++i)
        EXPECT_NEAR(c.periodicity_windows_dits[i] * 0.048 / stage1_s[i], 1.0, 0.002) << i;  // 0.08%, 0.16%, 0.16%
}
