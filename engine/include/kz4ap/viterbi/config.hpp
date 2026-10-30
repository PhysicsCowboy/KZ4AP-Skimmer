// The new decoder's configuration: every parameter in physical units (s, ms, Hz, dots, nats, dB with a named
// reference); samples and bins appear only where a value is used. Each default is the design's
// (docs/design/2026-10-09-new-decoder-design-notes.md, last section, part 17) and is marked with its class: derived,
// measured, heuristic, placeholder (kept until a measurement replaces it) or the owner's decision with its date.
#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace kz4ap::viterbi {

struct ViterbiConfig {
    // --- Time, and the grid of speeds (topic 2, items 1 and 2; part 11) ---
    // Standard spacing only: no Farnsworth factor (the owner's decision D14 of 2026-10-21; topic 2, item 3, amended).
    // Farnsworth-spaced signals decode poorly until a later fix: docs/backlog.md, "Farnsworth spacing in the new
    // decoder".

    // s: the decoder's sample interval Delta, one envelope sample every 4th channel sample at 1 500 samples/s,
    // 2.667 ms (owner, 2026-10-23; D17). It must be a whole number of channel samples (make_viterbi_shared).
    double sample_interval_s = 4.0 / 1500.0;
    // ms: the dot of the fastest speed, 100 WPM (a dot lasts 1.2 s / WPM)
    double dot_min_ms = 12.0;
    // dimensionless: each speed's dot is this times the previous speed's; the dot of speed s is
    // dot_min_ms x speed_ratio^s
    double speed_ratio = 1.1;
    // count: speeds s = 0 ... 31, dots of 12.0 to 230.3 ms (100 to 5.2 WPM)
    int speed_count = 32;

    // --- The fist, the timing model of a sender (topic 2, items 4 and 5; part 11) ---
    // One entry per kind, in the order of Kind (fist.hpp): Dot, Dash, ElementGap, LetterGap, WordGap. A kind's length
    // is shift + a log-normal of median M and standard deviation sigma in ln(length); M is the kind's median in dots
    // times the speed's dot.

    // dots: the medians; 1, 3, 1, 3, 7 are the ITU sending standard's ratios, not a measurement of senders
    // (placeholder, heuristic)
    std::array<double, 5> fist_medians_dots = {{1.0, 3.0, 1.0, 3.0, 7.0}};
    // ln(length): the log-normal's standard deviation per kind (placeholder, heuristic)
    std::array<double, 5> fist_sigmas = {{0.3, 0.3, 0.3, 0.3, 0.3}};
    // dots: a constant added to each kind's length, a sender's weighting; 0 in the baseline and used only by the genie
    // fist. The genie fist is this fist with speed_count = 1, dot_min_ms the sender's dot, and the sender's own
    // medians, sigmas and shifts
    std::array<double, 5> fist_shifts_dots = {{0.0, 0.0, 0.0, 0.0, 0.0}};

    // --- The search (topic 5) ---

    // nats: how far behind the best story (one continuous account of what has been sent) a story may fall and still
    // be kept (placeholder, set by the plan's R2)
    double beam_nats = 40.0;
    // stories: the most stories kept at a sample (placeholder, set by the plan's R3)
    int ceiling = 1700;

    // --- The front filter and the mark's template (parts 2 and 5) ---

    // s: the dot the front filter is matched to, 12 ms at half amplitude, a 100 WPM dot (owner, 2026-10-23; D17)
    double front_dot_s = 0.012;
    // s: the 10-90% rise (and fall) time of that dot's raised-cosine edges and of the template's keying, 2.5 ms (owner,
    // 2026-10-23; D17)
    double edge_rise_s = 0.0025;
    // s: the range of other transmitters' 10-90% rise times the edge term averages over, uniform on it: the ARRL Lab's
    // measured range, 0.8 to 4.7 ms (owner, 2026-10-23; D17)
    double edge_rise_min_s = 0.0008;
    double edge_rise_max_s = 0.0047;

    // --- The amplitude and its long-run power (parts 6 and 7) ---

    // Hz: the Doppler spread f_D of the fading, twice the standard deviation of a Gaussian Doppler spectrum: sets the
    // amplitude's prediction and nu's correlation (owner, 2026-10-22; D16)
    double fading_spread_hz = 2.0;

    // --- The model's systematic uncertainty (part 9; D15) ---

    // relative amplitude: the floor f of the systematic term, added in quadrature to the noise, f^2 s^2 of the
    // carrier's power (placeholder until the prototype measures it; owner, 2026-10-23, D17). 0 to 1
    double systematic_floor = 0.02;
    // s: nu's bins, 16 samples (42.67 ms), and how far back a bin's cross terms reach, 10 bins (426.7 ms) (owner,
    // 2026-10-23; D17). Each must be a whole number of samples, and nu_memory_s a whole number of bins.
    double nu_bin_s = 16.0 * 4.0 / 1500.0;
    double nu_memory_s = 160.0 * 4.0 / 1500.0;

    // --- Overs, restarts and the ends of words (topic 5, item 3) ---

    // 1/s: the rate at which an over (one transmission of one station) starts in silence. The probability in one
    // sample of silence is over_rate_per_s x sample_interval_s = 1.333e-4 (not 1 - exp(-rate x Delta)). Placeholder,
    // set by the plan's R5
    double over_rate_per_s = 0.05;
    // probability per letter start: that the sender begins a new story (a new speed and level; the text goes on) at a
    // letter start. Placeholder, set by the plan's R5
    double rescue_epsilon = 1.0e-3;
    // probability: that a letter ends its word; 1 / 3.06, the mean word length VE3NEA measured in CW Skimmer's
    // decodes, until recounted on real traffic
    double word_end_p = 1.0 / 3.06;
    // probability: that a word ends its over; 1/24, the development set's own count, until recounted on real traffic
    double over_end_q = 1.0 / 24.0;

    // --- The level prior and the newborn (topic 5, item 3; parts 7 and 8) ---

    // dB S500 (key-down carrier power over the noise power in 500 Hz): the range of a newborn's level, flat in dB over
    // it, and of Omega_bar's prior (placeholder)
    double level_min_db = -15.0;
    double level_max_db = 85.0;
    // count: the order of the Gauss-Hermite quadrature of each sample's score (plan D4)
    int quadrature_order = 5;
    // nepers of ln a: a newborn switches from its integral to N(mu, P) once its belief's standard deviation in ln a
    // falls below this (derived: the log-normal's skew then costs about 0.008 nats; owner, 2026-10-24, D17)
    double newborn_switch_sigma_u = 0.1;
    // count: the newborn's grid in ln a over the level prior (heuristic, with a derived bound and a measured check;
    // owner, 2026-10-26, D22)
    int newborn_grid_points = 128;
    // nats: grid points more than this below the largest are dropped for good (heuristic, about twice the deepest
    // revival measured on correctly timed stories under fading, 67.9 nats; docs/decision-log.md, 2026-10-30)
    double newborn_live_range_nats = 160.0;
    // s: the newborn's prediction step runs every this many scored samples, 8 x 4 / 1 500 s = 21.33 ms (heuristic,
    // chosen by measurement; D24). It must be a whole number of samples.
    double newborn_fade_interval_s = 8.0 * 4.0 / 1500.0;

    // --- The hiss (noise level) estimator (topic 3, item 3) ---

    // Hz: it reads the spectrum's bins within +/- this of the station (placeholder, heuristic)
    double hiss_half_width_hz = 2000.0;
    // percent: the rank, among those bins, of the one taken as the hiss (placeholder, heuristic)
    double hiss_percentile = 25.0;
    // s: the time constant of the one-pole average of each bin's power, a plain running mean while 1/n exceeds the
    // pole's step; the signal detector's rule and value (plan section 2.10, "Hiss average: one-pole, 1 s"; heuristic)
    double hiss_average_s = 1.0;

    // (No frequency tracker: the channelizer follows the detector, and the decoder reads its channel as received;
    // design Part 10, ChannelFollower.)

    // --- Genie runs only: the truth, given to the decoder in place of what it would estimate ---
    // (The carrier's frequency is no genie value: an oracle channel follows its label's frequency, EngineConfig::
    // oracle_frequency_tracks.)

    // Hz: the true Doppler spread of the fading, in place of fading_spread_hz (empty: none)
    std::optional<double> genie_fading_spread_hz;

    // --- Instrumentation ---

    // on or off: the decoder's instrumentation
    bool stats = false;
};

// One oracle channel's genie values (genie runs only; EngineConfig::viterbi_genie, one per oracle channel, since each
// channel's station has its own): the truth its decoder is given in place of what it would estimate. Each part is used
// only where it is set; with_genie puts them into the channel's configuration.
struct ViterbiGenie {
    // Hz: the fading's true Doppler spread (genie_fading_spread_hz)
    std::optional<double> fading_spread_hz;
    // The sender's fist: its dot, ms, and its per kind medians (dots), sigmas (ln(length)) and shifts (dots), in the
    // order of Kind (fist.hpp). The speed grid collapses to that one speed (the genie fist of fist_shifts_dots above).
    struct Fist {
        double dot_ms = 0.0;
        std::array<double, 5> medians_dots = {{1.0, 3.0, 1.0, 3.0, 7.0}};
        std::array<double, 5> sigmas = {{0.3, 0.3, 0.3, 0.3, 0.3}};
        std::array<double, 5> shifts_dots = {{0.0, 0.0, 0.0, 0.0, 0.0}};
    };
    std::optional<Fist> fist;
};

// The configuration of one oracle channel with the genie's values in place: the spread as genie_fading_spread_hz; the
// fist as speed_count = 1, dot_min_ms = its dot and its medians, sigmas and shifts. The parts not set leave the
// configuration as it was.
inline ViterbiConfig with_genie(ViterbiConfig config, const ViterbiGenie& genie) {
    if (genie.fading_spread_hz) config.genie_fading_spread_hz = genie.fading_spread_hz;
    if (genie.fist) {
        config.speed_count = 1;
        config.dot_min_ms = genie.fist->dot_ms;
        config.fist_medians_dots = genie.fist->medians_dots;
        config.fist_sigmas = genie.fist->sigmas;
        config.fist_shifts_dots = genie.fist->shifts_dots;
    }
    return config;
}

// The whole number of `unit` (s) in `duration` (s), a configuration's duration converted at its point of use: a
// positive whole number within 1e-9 of the ratio, relatively (heuristic: the rounding of a decimal or of 4 / 1 500 s).
// Throws std::invalid_argument naming the field (`field`, in `unit_name`) otherwise, with the numbers.
inline int whole_count(double duration_s, double unit_s, const char* field, const char* unit_name) {
    const double ratio = duration_s / unit_s;
    const double whole = std::round(ratio);
    if (std::isfinite(ratio) && whole >= 1.0 && whole <= 1e9 && std::fabs(ratio - whole) <= 1e-9 * whole)
        return static_cast<int>(whole);
    std::ostringstream message;
    message.precision(10);
    message << field << " = " << duration_s << " s is " << ratio << " " << unit_name << " of " << unit_s
            << " s; it must be a positive whole number of them (within 1e-9)";
    throw std::invalid_argument(message.str());
}

}  // namespace kz4ap::viterbi
