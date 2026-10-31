// The segmental decoder's configuration: the fast form of the Bayesian decoder (segmental/search.hpp). Its timing, text
// and overs are the viterbi decoder's model, read from `model`; its own values are what the fast form replaces the
// per-story amplitude and the per-sample search with. Physical units throughout; each value is marked derived, measured
// or heuristic.
#pragma once

#include "kz4ap/viterbi/config.hpp"

namespace kz4ap::segmental {

// The viterbi decoder's model with what the fast form sets differently: the systematic floor f, 1 (a relative amplitude),
// 0.1 against the viterbi decoder's 0.02. A heuristic chosen by measurement (docs/decision-log.md, 2026-10-31): at strong
// levels a relative amplitude error delta costs about w delta^2 / f'^2 nats per sample, so at 0.02 a dip under flutter or
// ripple is cheaper to read as a gap, and CER rises with the level. `--set systematic_floor=` still sets it.
inline viterbi::ViterbiConfig fast_form_model() {
    viterbi::ViterbiConfig model;
    model.systematic_floor = 0.1;
    return model;
}

struct SegmentalConfig {
    // The model the fast form shares with the viterbi decoder: the sample interval, the speed grid, the fist, the text's
    // and the over's probabilities, the symbol prior, the edge rises; the systematic floor is its own (above).
    viterbi::ViterbiConfig model = fast_form_model();

    // --- The shared level (channel-level forward-backward over key state x level, evidence.hpp) ---

    // dB S500: the grid of key-down levels, relative to the channel's noise, the bottom and the top. Heuristic: below
    // -6 dB S500 a mark is not decodable on this model; +84 dB S500 matches the viterbi decoder's level range's top.
    double level_min_db_s500 = -6.0;
    double level_max_db_s500 = 84.0;
    // dB: the grid's step. Heuristic.
    double level_step_db = 1.5;
    // 1: how much of the grid's own variance, step^2 / 12 in nepers^2, goes into V (0 to leave it out)
    double level_grid_in_v = 1.0;
    // s: the level's frame, over which it is held constant: 8 samples, 21.33 ms, as the viterbi newborn's prediction step
    double level_frame_s = 8.0 * 4.0 / 1500.0;
    // s: the look-ahead the level reads past the sample it is used for, at least, and the block the look-ahead is committed
    // in: 8 frames each, 170.7 ms; the evidence lags by their sum plus the search's 17 samples at most, 0.387 s. Measured: at the
    // walk below, CER does not change from 0.085 to 1.5 s of look-ahead, since the walk forgets within about 0.1 s.
    double level_lookahead_s = 8.0 * 8.0 * 4.0 / 1500.0;
    double level_commit_s = 8.0 * 8.0 * 4.0 / 1500.0;
    // 1/s: the rate at which the level filter's key changes state, inside the level's filter only (the search has the real
    // timing). A heuristic chosen by measurement (docs/decision-log.md, 2026-10-30); of the size of the element rate at 40 WPM.
    double key_switch_per_s = 30.0;
    // nepers per square root of a second: the random walk of ln a from frame to frame. A heuristic chosen by measurement
    // (docs/decision-log.md, 2026-10-30). The Doppler's own rate at f_D = 3.5 Hz (Butterworth, near its rms level) is 1.4
    // (derived); the walk stands in for a smooth level, whose variance grows as lag^2, with one whose variance grows as lag.
    double level_walk_nepers_per_sqrt_s = 4.8;
    // 1/s: the rate at which the level jumps to anywhere on the grid (a new station, a new over). Heuristic, of the size
    // of the model's over rate.
    double level_jump_per_s = 0.05;

    // --- The channel's own noise floor (ChannelFloor, evidence.hpp) ---

    // s: how far back the floor reads, 0 to leave it out and use the hiss alone. Heuristic.
    double channel_floor_window_s = 5.0;
    // 1: the quantile of e^2 it reads. Heuristic. (The factor from the quantile to the noise, -ln(1 - q), is derived.)
    double channel_floor_quantile = 0.10;
    // dB re the floor as estimated: the margin below the estimate. Heuristic: on the review's seeds -1.5 dB costs about 1 dB
    // of sensitivity and -3 dB costs nothing measurable.
    double channel_floor_margin_db = -3.0;
    // s: the spacing of the e^2 it keeps, every 4th sample, where the noise's correlation has fallen to rho_4 = 0.14. Heuristic.
    double channel_floor_stride_s = 4.0 * 4.0 / 1500.0;
    // s: how often the quantile is taken again, every 36 samples. Heuristic.
    double channel_floor_refresh_s = 36.0 * 4.0 / 1500.0;
    // s: how much of the channel it reads before it is used. Heuristic.
    double channel_floor_start_s = 1.0;

    // --- The search (search.hpp) ---

    // nats: how far behind the best a story may fall and stay. Heuristic, set by measurement.
    double beam_nats = 15.0;
    // stories: the most open elements kept. Heuristic, set by measurement.
    int ceiling = 3000;
    // 1: an element may end only within this many standard deviations of ln(length) of its median. Heuristic.
    double duration_z_max = 4.0;
};

}  // namespace kz4ap::segmental
