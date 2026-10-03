#include "kz4ap/bank/bank_config.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "golden.hpp"

namespace {

void expect_vector(const std::vector<double>& actual, const nlohmann::json& golden, const char* name) {
    ASSERT_EQ(actual.size(), golden.size()) << name;
    for (std::size_t k = 0; k < actual.size(); ++k)
        EXPECT_DOUBLE_EQ(actual[k], golden[k].get<double>()) << name << "[" << k << "]";
}

}  // namespace

// ProtoConfig() as JSON (dataclasses.asdict), one EXPECT per member.
TEST(BankConfig, DefaultsEqualThePrototypes) {
    const auto g = kz4ap::test::load_golden("config");
    const kz4ap::bank::BankConfig c{};
    EXPECT_DOUBLE_EQ(c.min_wpm, g["min_wpm"].get<double>());
    EXPECT_DOUBLE_EQ(c.max_wpm, g["max_wpm"].get<double>());
    EXPECT_DOUBLE_EQ(c.ladder_step, g["ladder_step"].get<double>());
    EXPECT_DOUBLE_EQ(c.length_dits, g["length_dits"].get<double>());
    EXPECT_DOUBLE_EQ(c.block_s, g["block_s"].get<double>());
    EXPECT_EQ(c.noise_method, g["noise_method"].get<std::string>());
    expect_vector(c.mask_bias, g["mask_bias"], "mask_bias");
    EXPECT_DOUBLE_EQ(c.noise_tau_s, g["noise_tau_s"].get<double>());
    EXPECT_DOUBLE_EQ(c.noise_warmup_s, g["noise_warmup_s"].get<double>());
    EXPECT_DOUBLE_EQ(c.noise_guard, g["noise_guard"].get<double>());
    EXPECT_DOUBLE_EQ(c.neighbor_guard, g["neighbor_guard"].get<double>());
    EXPECT_DOUBLE_EQ(c.segment_s, g["segment_s"].get<double>());
    EXPECT_DOUBLE_EQ(c.spectrum_smoothing_hz, g["spectrum_smoothing_hz"].get<double>());
    EXPECT_DOUBLE_EQ(c.guard_margin_s, g["guard_margin_s"].get<double>());
    EXPECT_DOUBLE_EQ(c.min_clean_fraction, g["min_clean_fraction"].get<double>());
    EXPECT_DOUBLE_EQ(c.amplitude_tau_s, g["amplitude_tau_s"].get<double>());
    EXPECT_DOUBLE_EQ(c.prior_key_down, g["prior_key_down"].get<double>());
    EXPECT_DOUBLE_EQ(c.hysteresis_nats, g["hysteresis_nats"].get<double>());
    EXPECT_DOUBLE_EQ(c.squelch_a, g["squelch_a"].get<double>());
    EXPECT_DOUBLE_EQ(c.squelch_ref_s, g["squelch_ref_s"].get<double>());
    EXPECT_DOUBLE_EQ(c.squelch_exponent, g["squelch_exponent"].get<double>());
    EXPECT_DOUBLE_EQ(c.false_marks_per_s, g["false_marks_per_s"].get<double>());
    expect_vector(c.x_on_values, g["x_on_values"], "x_on_values");
    EXPECT_DOUBLE_EQ(c.release_probability, g["release_probability"].get<double>());
    EXPECT_DOUBLE_EQ(c.rekey_after_s, g["rekey_after_s"].get<double>());
    EXPECT_DOUBLE_EQ(c.seed_memory_rekeys, g["seed_memory_rekeys"].get<double>());
    EXPECT_DOUBLE_EQ(c.fit_memory, g["fit_memory"].get<double>());
    EXPECT_DOUBLE_EQ(c.t_grid_step, g["t_grid_step"].get<double>());
    expect_vector(c.q_grid, g["q_grid"], "q_grid");
    expect_vector(c.w_grid, g["w_grid"], "w_grid");
    expect_vector(c.tg_grid, g["tg_grid"], "tg_grid");
    EXPECT_DOUBLE_EQ(c.sigma_ln_mark, g["sigma_ln_mark"].get<double>());
    EXPECT_DOUBLE_EQ(c.sigma_ln_space, g["sigma_ln_space"].get<double>());
    EXPECT_DOUBLE_EQ(c.outlier_prior, g["outlier_prior"].get<double>());
    expect_vector(c.outlier_range_s, g["outlier_range_s"], "outlier_range_s");
    EXPECT_DOUBLE_EQ(c.prior_sigma_ln, g["prior_sigma_ln"].get<double>());
    EXPECT_EQ(c.refine_iterations, g["refine_iterations"].get<int>());
    EXPECT_DOUBLE_EQ(c.min_fit_weight, g["min_fit_weight"].get<double>());
    EXPECT_EQ(c.periodicity_method, g["periodicity_method"].get<std::string>());
    expect_vector(c.periodicity_windows_s, g["periodicity_windows_s"], "periodicity_windows_s");
    EXPECT_DOUBLE_EQ(c.periodicity_update_s, g["periodicity_update_s"].get<double>());
    EXPECT_DOUBLE_EQ(c.periodicity_rate_hz, g["periodicity_rate_hz"].get<double>());
    EXPECT_EQ(c.comb_teeth, g["comb_teeth"].get<int>());
    EXPECT_DOUBLE_EQ(c.comb_width, g["comb_width"].get<double>());
    EXPECT_EQ(c.spectrum_nulls, g["spectrum_nulls"].get<int>());
    EXPECT_DOUBLE_EQ(c.spectrum_null_width, g["spectrum_null_width"].get<double>());
    EXPECT_DOUBLE_EQ(c.spectrum_front_floor_db, g["spectrum_front_floor_db"].get<double>());
    EXPECT_DOUBLE_EQ(c.comb_confidence_min, g["comb_confidence_min"].get<double>());
    EXPECT_DOUBLE_EQ(c.edge_confidence_min, g["edge_confidence_min"].get<double>());
    EXPECT_DOUBLE_EQ(c.spectrum_confidence_min, g["spectrum_confidence_min"].get<double>());
    EXPECT_DOUBLE_EQ(c.eligibility_tolerance, g["eligibility_tolerance"].get<double>());
    EXPECT_EQ(c.switch_persistence, g["switch_persistence"].get<int>());
    EXPECT_DOUBLE_EQ(c.quality_tie_nats, g["quality_tie_nats"].get<double>());
    EXPECT_DOUBLE_EQ(c.text_tie_nats, g["text_tie_nats"].get<double>());
    EXPECT_EQ(c.text_window_chars, g["text_window_chars"].get<int>());
    EXPECT_DOUBLE_EQ(c.text_separation_nats, g["text_separation_nats"].get<double>());
    EXPECT_DOUBLE_EQ(c.new_over_min_s, g["new_over_min_s"].get<double>());
    EXPECT_DOUBLE_EQ(c.new_over_gaps, g["new_over_gaps"].get<double>());
    EXPECT_DOUBLE_EQ(c.rekey_timeout_s, g["rekey_timeout_s"].get<double>());
    EXPECT_EQ(c.fresh_fit_min_obs, g["fresh_fit_min_obs"].get<int>());
    EXPECT_DOUBLE_EQ(c.correction_reach_s, g["correction_reach_s"].get<double>());
    EXPECT_EQ(g.size(), 61u) << "ProtoConfig has a field that BankConfig does not mirror";
}
