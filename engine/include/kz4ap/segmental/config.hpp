// The segmental decoder's configuration: the fast form of the Bayesian decoder (segmental/search.hpp). Its timing, text
// and overs are the viterbi decoder's model, read from `model`; its own values are what the fast form replaces the
// per-story amplitude and the per-sample search with. Physical units throughout; each value is marked derived, measured
// or heuristic.
#pragma once

#include "kz4ap/viterbi/config.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

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

    // --- A neighbor's leak (SegmentalDecoder::set_band_spectrum) ---

    // s: how far back each block's leak estimate is held, its peak; 0 leaves the leak out. 9 blocks of 21.33 ms at 48 kHz, 0.192 s. A heuristic, chosen by
    // measurement looking at E2 and the strong pairs, so not independent of them; seed 2 agrees: E2 0.490 against 0.504 at 1 s, seed 2 0.651 against 0.673,
    // and a neighbor at least 6 dB re wanted at 60 to 120 Hz 0.129 against 0.283, where no floor reads 0.179 (docs/decision-log.md, 2026-10-31). The 1 s of
    // the floor's first try was never swept. Why (conjecture): in a neighbor's gaps there is no leak to excuse, so holding the floor over them only blinds the stream.
    double leak_hold_s = 0.2;
    // dB re the leak: the noise the floor counts for it: sigma^2 at least the leak, this far up. Heuristic, chosen by measurement: on a slice, at a hold of 1 s,
    // +3 dB reads E2 0.504, +6 dB 0.573 and +10 dB 0.690 against 0.575 without it, and B2 rises from +6 dB (docs/decision-log.md, 2026-10-31).
    double leak_floor_db = 3.0;
    // Hz: the bins read for it, from the channel's center; what is nearer is taken as the station's own. Heuristics: a Hann main lobe reaches 47 Hz
    // at 23.4 Hz a bin, and the chain's response past 320 Hz is under -100 dB re 0 Hz.
    double leak_min_offset_hz = 60.0;
    double leak_max_offset_hz = 320.0;

    // --- A story's fist: the gap stretch (search.hpp) ---

    // count: the gap stretches a story may take, g = 1, r, r^2, ..., r = speed_ratio^gap_stretch_speed_steps; 1 leaves them out.
    // The default's 7 at r = 1.61 reach g = 17.4; 33 WPM characters at 5 WPM overall are g = 15.7 (ARRL's rule; derived). The
    // step is a heuristic: a gap half a step off is 0.24 nepers, 0.8 of the fist's sigma, about 0.3 nats per gap (derived).
    int gap_stretches = 7;
    int gap_stretch_speed_steps = 5;
    // s: the longest gap unit g T_s, which is what a speed's stretches stop at: 0.7 s is about 4.5 WPM overall (60 / 19 s / WPM).
    // Heuristic: slower than anyone copies at; it bounds the tables and how long a gap stays open.
    double gap_unit_max_s = 0.7;
    // probability: that a new story's fist is not standard spacing, shared alike by the stretches above 1: ln(0.002 / 6) = -8.0 nats
    // once per over for a stretched story, ln 0.998 = -0.002 nats for any other. A heuristic, chosen by measurement on the whole
    // test set's seed 1, with the widest filter alone (fists, the two QSO groups, Farnsworth; no stretches 0.061, 0.055, 0.058, 0.523):
    // 0.01 0.064, 0.057, 0.063, 0.021; 0.002 0.062, 0.055, 0.058, 0.017; 0.0004 0.061, 0.055, 0.058, 0.039. And CPU, on a real
    // recording's near-threshold channels: 0.05 costs 14.0 ms per channel-second against 11.0 with no stretches, 0.01 costs 11.4.
    double gap_stretch_prior = 0.002;

    // --- The front filter's widths (segmental_decoder.hpp) ---

    // 1: the filter scales, each a whole channel of evidence and search with its own front filter: scale m has a filter matched to
    // a dot of m x 12 ms (noise bandwidth about 74.5 Hz / m) and reads its stories' speeds from a dot of m x 12 ms down. It must
    // start with 1, the widest. Each m is a whole number. Heuristic, by measurement: before the leak floor, on the whole test set's seed 1 {1, 3} reads E2 0.575
    // against 0.562 with {1, 2, 3} and 0.691 with the widest alone, every other group within 0.001 of {1, 2, 3}, for about a quarter
    // less CPU (docs/decision-log.md, 2026-10-31); adding 6 (12 Hz) reads E2 0.42 on a slice but fading 0.175 against 0.163 and the
    // second QSO group 0.082 against 0.068, for 2 ms per channel-second more and a filter a detector's frequency error matters to.
    std::vector<int> filter_scales = {1, 3};
    // s of the channel's age: the choice of filter reads S - F from here on. A jump in the noise reading gave one stream 100 000 nats in the
    // first second, before the channel floor is in (1 s). Heuristic.
    double filter_scale_start_s = 1.5;
    // 1, nats: a narrower filter may lead only if it has read at least this share of the symbols the widest has, or stands at this
    // S - F itself. Where a narrow filter reads next to nothing, S and F are both about 0, and that beats the widest filter's S - F
    // on a weak station, which is often negative; but a widest filter misled by a neighbor reads over twice the symbols there
    // are. Heuristics, by measurement (docs/decision-log.md, 2026-10-31).
    double filter_scale_min_symbols = 0.5;
    double filter_scale_enough_nats = 100.0;
    // s of the channel's age, nats: from this age on, a scale other than the widest and the leader that trails the leader by more than
    // this, or may not lead, is dropped for the channel's life. Heuristics; what they buy is CPU. A negative time never drops. The
    // margin: 30 and 100 nats read alike to 0.001 on the test set's seed 1 and seed 2, and 30 costs 10% less CPU there.
    double filter_scale_drop_after_s = 10.0;
    double filter_scale_drop_margin_nats = 30.0;
    // stories: the search's ceiling at the narrower filters. Heuristic, for CPU: on a real recording's near-threshold channels, 3 000
    // reads 20 ms per channel-second and this 16; what it costs was not measurable on the review's neighbors or Farnsworth. A
    // beam of 12 nats there would save 1.6 ms more and costs 60 Hz at 0 dB re wanted 0.083 to 0.442.
    int filter_scale_ceiling = 300;
    // nats: a scale takes the lead when it stands this far above the leader. A heuristic, by measurement: on a clean weak station a
    // narrower filter can stand a few tens of nats above the widest and read a little worse (a generated band: 3 nats, CER
    // 0.0190; 60 nats, 0.0176; 120 nats, 0.0166, as with the widest alone), and the review's neighbors stand well above that.
    double filter_scale_switch_nats = 120.0;
    // s: how much later the template's rise is taken than the chain's own: a narrower filter's response starts sooner after its
    // edge in samples, and the evidence's window (kEdgeFirst ... kEdgeDone) stays where it is. A story then claims its edges this
    // much early, which nothing reads but the symbols' times. Set by at_filter_scale.
    double template_delay_s = 0.0;
    // E / T, the raised-cosine edges' full length E as a share of the dot T, of a narrower filter's pulse: 0.25 is 9 ms at T = 36 ms. A longer edge
    // lowers the pulse's sidelobes, where a strong neighbor at 60 to 200 Hz gets in (derived); the noise bandwidth falls 27.0 to 26.0 Hz (derived), at no
    // cost measured. The value is a heuristic that the test set chose, inside a plateau of 3/16 to 5/16; 3/8 starts to cost at +30 dB S500. 0 keeps the
    // widest filter's 4.235 ms. The widest filter (m = 1) keeps its own edges, which are the chain's.
    double front_taper_ratio = 0.25;
    // s: the 10-90% rise of those edges, E x 0.590334; 0 means the model's edge_rise_s. Set by at_filter_scale.
    double front_filter_rise_s = 0.0;

    // --- The search (search.hpp) ---

    // nats: how far behind the best a story may fall and stay. Heuristic, set by measurement.
    double beam_nats = 15.0;
    // stories: the most open elements kept. A heuristic by measurement, with ceiling_stretch_bonus_nats: on a slice of seed 1, 1 500 at +4 nats reads A2 0.2765, C2 0.0627, E2
    // 0.4867, I2 0.0168 against 0.2763, 0.0625, 0.4870, 0.0210 at 3 000 with no bonus, for 11% less decoder CPU on real 40 m, 30 min of it listing the same 20 calls.
    int ceiling = 1500;
    // dots of the story's speed: a new element is not opened if one opened within this time in the same state (kind, fist, node) has a base at least as high. Its future
    // differs by a length within this share of a dot, against a lower score. 0 leaves it out. A heuristic by measurement, on a slice of seed 1: 0.125 reads A2 0.2763, C2 0.0625, E2
    // 0.4870, I2 0.0210 against 0.2767, 0.0628, 0.4904, 0.0211 with none, by signal 155 better, 147 worse, for 11% less decoder CPU on real 40 m; 0.25 and 0.5 save 16% and 19% and read C2 0.0635, 0.0637.
    double open_spacing_dots = 0.125;
    // A heuristic by measurement: a weak Farnsworth sender's true story stands 8 nats behind its plain rivals (gap_stretch_prior) until it has earned that, and the ceiling used
    // to take it first. At +8 nats A2 reads 0.2785 and C2 0.0637, the plain stories displaced; at +4 nats as above.
    // nats: what a story of a stretched fist counts for above its score when the ceiling ranks the stories: what its fist's prior took from it once.
    double ceiling_stretch_bonus_nats = 4.0;

    // 1: an element may end only within this many standard deviations of ln(length) of its median. Heuristic. 3.5 saves 5% more CPU per second of stream on real 40 m, with the same
    // list, and costs H2 0.0568 and 0.0492 against 0.0562 and 0.0473 on seeds 1 and 2, and one station of three beside a neighbor at 60 Hz on the detector's path: not taken.
    double duration_z_max = 4.0;

    // nats: a merge's runner-up within this of its winner, reading other text, leaves its deficit on the winner's symbols where they differ, the least
    // such deficit per symbol (a soft-output Viterbi's reliability, kept on the shared text): what a symbol's probability is made of, 1 / (1 + e^-margin).
    // A symbol no rival came this near reads this margin; 0 keeps no margins at all, and every symbol says probability 1. Changes no text.
    // Heuristic: past the beam, 15 nats.
    double rival_margin_nats = 16.0;
};

// The configuration at filter scale m: the sample interval, the filter's dot and the fastest speed's dot m times as long, so
// that in samples the front filter's dot and the fastest dot stay what they are (the evidence's window of 6 samples and its short
// elements stay valid) and the speed grid keeps its slowest dot. The level's frame and look-ahead keep about their lengths in s,
// in whole samples; the channel floor's stride and refresh are m times as long, where its noise's correlation falls.
// Throws std::invalid_argument for an m below 1.
inline SegmentalConfig at_filter_scale(SegmentalConfig c, int m) {
    if (m < 1) throw std::invalid_argument("segmental: a filter scale must be at least 1");
    if (!(c.front_taper_ratio >= 0.0 && c.front_taper_ratio <= 1.0)) throw std::invalid_argument("segmental: front_taper_ratio must lie in 0 to 1");
    if (m == 1) return c;
    const double scale = m;
    c.ceiling = c.filter_scale_ceiling;
    const double interval_s = c.model.sample_interval_s;
    const int frame_samples = static_cast<int>(std::ceil(c.level_frame_s / (interval_s * scale) - 1e-9));
    const double frame_s = frame_samples * interval_s * scale;
    const auto frames_of = [&](double duration_s) { return std::max(1.0, std::round(duration_s / frame_s)) * frame_s; };
    c.level_lookahead_s = frames_of(c.level_lookahead_s);
    c.level_commit_s = frames_of(c.level_commit_s);
    c.level_frame_s = frame_s;
    c.channel_floor_stride_s *= scale;
    c.channel_floor_refresh_s *= scale;
    // The half of a step's response lies at the channel filter's 10.67 ms and the edge's 2.12 ms plus half the front pulse
    // (T + E) / 2, E = 4.235 ms at m = 1 (derived; viterbi/mark_template.hpp): 7.84 samples. The delay keeps it there, to a sample.
    const double edge_s = c.front_taper_ratio > 0.0 ? c.front_taper_ratio * c.model.front_dot_s * scale : 0.004235;
    if (c.front_taper_ratio > 0.0) c.front_filter_rise_s = edge_s * ((std::numbers::pi - 2.0 * std::acos(0.8)) / std::numbers::pi);
    const double half_s = 0.01279 + (c.model.front_dot_s * scale + edge_s) / 2.0;
    const double base_half_s = 0.01279 + (c.model.front_dot_s + 0.004235) / 2.0;
    c.template_delay_s = std::max(0.0, std::round(base_half_s / interval_s - half_s / (interval_s * scale))) * interval_s * scale;
    c.model.speed_count = std::max(1, c.model.speed_count - static_cast<int>(std::lround(std::log(scale) / std::log(c.model.speed_ratio))));
    c.model.sample_interval_s = interval_s * scale;
    c.model.front_dot_s *= scale;
    c.model.dot_min_ms *= scale;
    return c;
}

}  // namespace kz4ap::segmental
