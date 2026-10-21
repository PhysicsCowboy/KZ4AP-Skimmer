// The new decoder's configuration (first prototype): every parameter in physical units (Hz, s, ms, nats, dB with a
// named reference); pieces, bins and samples appear only where a value is used. Each default is the design's
// (docs/design/2026-10-09-new-decoder-design-notes.md) and is marked where the design keeps it as a placeholder until
// it is measured or counted on real traffic.
#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>

namespace kz4ap::viterbi {

struct ViterbiConfig {
    // --- Time, and the grids of speeds and Farnsworth factors (topic 2, items 1 to 3) ---

    // s: the decoder's time step, a "piece": 3 channel samples at 1500 samples/s (converted to samples where used)
    double piece_s = 0.002;
    // ms: the dot of the fastest speed, 100 WPM (a dot lasts 1.2 s / WPM)
    double dot_min_ms = 12.0;
    // dimensionless: each speed's dot is this times the previous speed's; the dot of speed s is
    // dot_min_ms x speed_ratio^s
    double speed_ratio = 1.1;
    // count: speeds s = 0 ... 31, dots of 12.0 to 230.3 ms (100 to 5.2 WPM)
    int speed_count = 32;
    // dimensionless: the Farnsworth factor f stretches the gaps between letters and between words by
    // F_f = factor_ratio^f
    double factor_ratio = 1.1;
    // count: factors f = 0 ... 22, F = 1 to 8.14 (the owner's range)
    int factor_count = 23;

    // --- The fist, the timing model of a sender (topic 2, items 4 and 5) ---
    // One entry per kind, in the order of Kind (fist.hpp): Dot, Dash, ElementGap, LetterGap, WordGap. A kind's length
    // is shift + a log-normal of median M and standard deviation sigma in ln(length); M is the kind's median in dots
    // times the speed's dot, times the Farnsworth factor for LetterGap and WordGap.

    // dots: the medians; 1, 3, 1, 3, 7 are the ITU sending standard's ratios, not a measurement of senders
    // (placeholder, heuristic)
    std::array<double, 5> fist_medians_dots = {{1.0, 3.0, 1.0, 3.0, 7.0}};
    // ln(length): the log-normal's standard deviation per kind (placeholder, heuristic)
    std::array<double, 5> fist_sigmas = {{0.3, 0.3, 0.3, 0.3, 0.3}};
    // dots: a constant added to each kind's length, a sender's weighting; 0 in the baseline and used only by the genie
    // fist. The Farnsworth factor does not stretch it. The genie fist is this fist with speed_count = 1, dot_min_ms
    // the sender's dot, and the sender's own medians, sigmas and shifts
    std::array<double, 5> fist_shifts_dots = {{0.0, 0.0, 0.0, 0.0, 0.0}};

    // --- The search (topic 5) ---

    // nats: how far behind the best story (a hypothesis: one continuous story of what has been sent) a story may fall
    // and still be kept (placeholder)
    double beam_nats = 40.0;
    // stories: the most stories kept at a piece (placeholder)
    int ceiling = 1700;

    // --- Fading and the block (topic 4, item 9) ---

    // Hz: the Doppler spread f_D of the fading, twice the standard deviation of a Gaussian Doppler spectrum; sets the
    // block length (block_length_s) and the rate of the level (placeholder, to be set by measurement)
    double fading_spread_hz = 1.0;
    // dB of signal energy: the largest loss a block may have at that spread (heuristic)
    double block_loss_db = 0.5;

    // --- The mark's rise (design notes, topic 4, item 2, as amended 2026-10-20) ---

    // s: the length of the raised-cosine edge with which the keyed envelope rises at a mark's start,
    // 0.5 - 0.5 cos(pi t / keying_rise_s); with the channel filter's own rise it sets how a mark's first pieces grow
    // (rise_fractions, observation.hpp: 0.193, 0.614, 0.943 and 1.019 of a full piece at 48 kHz), which weight a
    // mark's first block, the matched filter to the rise (search.hpp). Placeholder (heuristic): the generator's default
    // edge; real transmitters vary, commonly a few ms; to be measured on real recordings. 0 is allowed: the filter's
    // own rise alone; more than 1 s is refused (rise_fractions)
    double keying_rise_s = 0.005;

    // --- Overs, restarts and the ends of words (topic 5, item 3) ---

    // 1/s: the rate at which an over (one transmission of one station) starts in silence. The probability in one
    // piece of silence is over_rate_per_s x piece_s = 1.0e-4 exactly (not 1 - exp(-rate x piece)). Placeholder: the
    // development set's own rate, until counted on real traffic
    double over_rate_per_s = 0.05;
    // probability per letter start: that the sender begins a new story (a new speed, factor and level; the text goes
    // on) at a letter start. Placeholder: the development set's own count, 1/1000, until counted on real traffic
    double rescue_epsilon = 1.0e-3;
    // probability: that a letter ends its word; 1 / 3.06, the mean word length VE3NEA measured in CW Skimmer's
    // decodes, until recounted on real traffic
    double word_end_p = 1.0 / 3.06;
    // probability: that a word ends its over; 1/24, the development set's own count, until recounted on real traffic
    double over_end_q = 1.0 / 24.0;

    // --- The level (topic 3; topic 5, item 3) ---

    // dB S500 (key-down carrier power over the noise power in 500 Hz): the range of the newborn's level, equally
    // likely anywhere in it in dB (placeholder)
    double level_min_db = -15.0;
    double level_max_db = 85.0;
    // count: the order of the Gauss-Hermite quadrature over the level's uncertainty
    int quadrature_order = 5;

    // --- The hiss (noise level) estimator (topic 3, item 3) ---

    // Hz: it reads the spectrum's bins within +/- this of the station (placeholder, heuristic)
    double hiss_half_width_hz = 2000.0;
    // percent: the rank, among those bins, of the one taken as the hiss (placeholder, heuristic)
    double hiss_percentile = 25.0;
    // s: the time constant of the one-pole average of each bin's power, a plain running mean while 1/n exceeds the
    // pole's step; the signal detector's rule and value (plan section 2.10, "Hiss average: one-pole, 1 s"; heuristic)
    double hiss_average_s = 1.0;

    // --- The frequency tracker (topic 4, item 8; the owner's decision B of 2026-10-20; tracker.hpp has the design) ---

    // s: the length of the window of each measurement (placeholder)
    double tracker_window_s = 1.0;
    // s: the time between measurements (placeholder)
    double tracker_hop_s = 0.5;
    // Hz: the half-width of the search around the oscillator's frequency (placeholder)
    double tracker_search_hz = 15.0;
    // dB relative to the mean noise power per bin (N0 times the window's noise bandwidth, 1.5 Hz for the 1 s Hann
    // window): a window is measured only if its peak is at least this far above it. Derived: a steady line at the
    // perfect decoder's threshold at 5.2 WPM (-8.6 dB S500) stands 9.5 dB above it and passes in 64% of the windows
    double tracker_gate_db = 9.0;
    // Hz/s: the standard deviation of the drift rate at the start, and of any carrier's drift in the agreement test
    // of two measurements. Sourced: older rigs' specified warm-up drift, at most about 0.3 Hz/s (a Kenwood TS-830S,
    // 1 kHz in its first hour)
    double tracker_drift_sd_hz_per_s = 0.3;
    // Hz^2/s^3, (Hz/s)^2 per second: the process noise of the drift rate, a change of tracker_drift_sd_hz_per_s over
    // ten minutes, (0.3 Hz/s)^2 / 600 s (derived from the sourced drift; the ten minutes heuristic)
    double tracker_drift_noise = 1.5e-4;

    // --- Genie runs only: the truth, given to the decoder in place of what it would estimate ---

    // Hz from the channel's center: the carrier's true frequency at stream time 0 s (the recording's first sample), in
    // place of the tracker's estimate (empty: none). The decoder re-centers sample time t, s, at
    // genie_frequency_offset_hz + genie_frequency_drift_hz_per_s x t
    std::optional<double> genie_frequency_offset_hz;
    // Hz/s: the carrier's true drift rate, with genie_frequency_offset_hz
    double genie_frequency_drift_hz_per_s = 0.0;
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
    // The carrier: frequency_hz, Hz from the span's center (the frame of EngineConfig::oracle_frequencies_hz), at stream
    // time frequency_time_s, s, and its drift rate, Hz/s, so that it is at frequency_hz + drift_hz_per_s x
    // (t - frequency_time_s) at time t (the labels' convention: the drift counts from the signal's start)
    std::optional<double> frequency_hz;
    double frequency_time_s = 0.0;
    double drift_hz_per_s = 0.0;
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

// The configuration of one channel whose center is channel_center_hz (Hz from the span's center), with the genie's
// values in place: the carrier as genie_frequency_offset_hz = frequency_hz - channel_center_hz - drift_hz_per_s x
// frequency_time_s (the offset at stream time 0) and genie_frequency_drift_hz_per_s = drift_hz_per_s; the spread as
// genie_fading_spread_hz; the fist as speed_count = 1, dot_min_ms = its dot and its medians, sigmas and shifts. The
// parts not set leave the configuration as it was.
inline ViterbiConfig with_genie(ViterbiConfig config, const ViterbiGenie& genie, double channel_center_hz) {
    if (genie.frequency_hz) {
        config.genie_frequency_offset_hz =
            *genie.frequency_hz - channel_center_hz - genie.drift_hz_per_s * genie.frequency_time_s;
        config.genie_frequency_drift_hz_per_s = genie.drift_hz_per_s;
    }
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

// The block length, s: the longest T over which a coherent sum of a Rayleigh-fading carrier keeps at least
// 10^(-loss_db / 10) of its energy, (2 / T^2) integral_0^T (T - tau) rho(tau) dtau, where rho(tau) = exp(-pi^2 f^2
// tau^2 / 2) is the fading's correlation for a Gaussian Doppler spectrum of spread f = spread_hz (twice its standard
// deviation); design notes, topic 4, item 9. With u = pi f T / sqrt(2) the energy kept is
//     g(u) = sqrt(pi) erf(u) / u - (1 - exp(-u^2)) / u^2,
// which falls from 1 at u = 0 toward 0 (derived), so T = u sqrt(2) / (pi f) for the u with g(u) = 10^(-loss_db / 10),
// found by bisection to 1e-15 of u: T scales exactly as 1 / spread_hz. With no fading (spread_hz = 0) a block is the
// whole mark: +infinity. Throws std::invalid_argument for a negative or non-finite spread_hz, or a loss_db that is
// not positive and finite. At a loss of 0.5 dB of signal energy: 78.07 ms at 5 Hz, 195.19 ms at 2 Hz, 390.37 ms at 1 Hz
// and 1301.25 ms at 0.3 Hz.
inline double block_length_s(double spread_hz, double loss_db) {
    if (!std::isfinite(spread_hz) || spread_hz < 0.0)
        throw std::invalid_argument("block_length_s: spread_hz must be finite and not negative (Hz)");
    if (!std::isfinite(loss_db) || !(loss_db > 0.0))
        throw std::invalid_argument("block_length_s: loss_db must be finite and positive (dB of signal energy)");
    if (spread_hz == 0.0) return std::numeric_limits<double>::infinity();
    const double kept = std::pow(10.0, -loss_db / 10.0);
    const auto energy_kept = [](double u) {
        return std::sqrt(std::numbers::pi) * std::erf(u) / u - (-std::expm1(-u * u)) / (u * u);
    };
    double low = 0.0;
    double high = 1.0;
    while (energy_kept(high) > kept && high < 1e150) high *= 2.0;
    for (int step = 0; step < 200 && high - low > 1e-15 * high; ++step) {
        const double middle = 0.5 * (low + high);
        if (energy_kept(middle) > kept)
            low = middle;
        else
            high = middle;
    }
    return 0.5 * (low + high) * std::numbers::sqrt2 / (std::numbers::pi * spread_hz);
}

}  // namespace kz4ap::viterbi
