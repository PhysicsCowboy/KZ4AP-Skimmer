// The segmental decoder's configuration: the fast form of the Bayesian decoder (segmental/search.hpp). Its timing, text
// and overs are the viterbi decoder's model, read from `model`; its own values are what the fast form replaces the
// per-story amplitude and the per-sample search with. Physical units throughout; each value is marked derived, measured
// or heuristic.
#pragma once

#include "kz4ap/viterbi/config.hpp"

namespace kz4ap::segmental {

struct SegmentalConfig {
    // The model the fast form shares with the viterbi decoder: the sample interval, the speed grid, the fist, the text's
    // and the over's probabilities, the symbol prior, the systematic floor, the edge rises.
    viterbi::ViterbiConfig model;

    // --- The shared level (channel-level forward-backward over key state x level, evidence.hpp) ---

    // dB S500: the grid of key-down levels, relative to the channel's noise, the bottom and the top. Heuristic: below
    // -6 dB S500 a mark is not decodable on this model; +84 dB S500 matches the viterbi decoder's level range's top.
    double level_min_db_s500 = -6.0;
    double level_max_db_s500 = 84.0;
    // dB: the grid's step. Heuristic.
    double level_step_db = 1.5;
    // s: the level's frame, over which it is held constant: 8 samples, 21.33 ms, as the viterbi newborn's prediction step
    double level_frame_s = 8.0 * 4.0 / 1500.0;
    // s: the look-ahead the level reads past the sample it is used for, at least, and the block the look-ahead is
    // committed in; the text lags by their sum plus the edge's 11 samples at most, about 1.05 s. Heuristic.
    double level_lookahead_s = 36.0 * 8.0 * 4.0 / 1500.0;
    double level_commit_s = 12.0 * 8.0 * 4.0 / 1500.0;
    // probability: a sample is key-down, inside the level's factor only. Heuristic (the duty cycle of Morse is 0.4 to 0.5).
    double key_down_prior = 0.45;
    // nepers per square root of a second: the random walk of ln a from frame to frame. Heuristic, set by measurement.
    double level_walk_nepers_per_sqrt_s = 0.82;
    // 1/s: the rate at which the level jumps to anywhere on the grid (a new station, a new over). Heuristic, of the size
    // of the model's over rate.
    double level_jump_per_s = 0.05;

    // --- The search (search.hpp) ---

    // nats: how far behind the best a story may fall and stay. Heuristic, set by measurement.
    double beam_nats = 25.0;
    // stories: the most open elements kept. Heuristic, set by measurement.
    int ceiling = 3000;
    // 1: an element may end only within this many standard deviations of ln(length) of its median. Heuristic.
    double duration_z_max = 4.0;
};

}  // namespace kz4ap::segmental
