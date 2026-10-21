#include "kz4ap/viterbi/tracker.hpp"

#include "fft.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace kz4ap::viterbi {
namespace {

// points: the length of the zero-padded transform. 8 192 is 5.46 times the 1 500 samples of the default window, and at
// 1 500 samples/s a bin is 1 500 / 8 192 = 0.1831 Hz (the plan's choice; heuristic).
constexpr std::size_t kFftPoints = 8192;

// Hz: the standard deviation of the start's offset around the engine's anchor (the controller's ruling; placeholder:
// the detector's anchor is within +/-11.7 Hz of the carrier, the labels' exact).
constexpr double kInitialOffsetSdHz = 5.0;

// dimensionless: the standard deviations of the trust rule, the acceptance window and the agreement test (the standard
// 3 sigma).
constexpr double kSigmas = 3.0;

// Hz: the decoder's tolerance. A residual Delta f costs under 1 dB of signal power while Delta f T <= 0.26 for a
// coherent sum of length T, and the default fading spread's blocks are 390 ms: 0.26 / 0.390 s = 0.67 Hz (derived; plan
// 2.9, item 5).
constexpr double kToleranceHz = 0.67;

// Hz^2: c in a measurement's variance c / rho, rho the line's power over the mean noise power per bin. Measured: the
// least-squares fit in ln(variance), the slope fixed at the Cramer-Rao form's -1, to the first tracker's measured
// scatter of the keyed carrier (sd 0.116, 0.064, 0.037, 0.021 and 0.007 Hz at -5, 0, 5, 10 and 20 dB S500, PARIS at
// 25 WPM; rho = 0.44^2 / (N0 x 1.5 Hz) for a unit carrier).
constexpr double kVarianceScaleHz2 = 0.2833;

// Hz: the floor of a measurement's standard deviation, the parabola's own bias on a clean tone (measured: at most
// 0.08 mHz, by a calculation of the same estimator).
constexpr double kBiasFloorHz = 0.08e-3;

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

// The trust rule: an offset predicted with this variance (Hz^2) is trusted while 3 sigma_f <= 0.67 Hz.
bool trusted(double variance_hz2) { return kSigmas * std::sqrt(variance_hz2) <= kToleranceHz; }

// The vertex, in bins from the middle point, of the parabola through three equally spaced values in dB whose middle one
// is not below the others: 0.5 (below - above) / (below - 2 middle + above), within +/- 0.5. NaN when the three do not
// make a strictly concave parabola (a flat top has no vertex).
double parabola_vertex_bins(double db_below, double db_middle, double db_above) {
    const double curvature = db_below - 2.0 * db_middle + db_above;  // dB, negative at a strict local maximum
    return curvature < 0.0 ? 0.5 * (db_below - db_above) / curvature : kNaN;
}

// s^2: the integral from 0 to tau of min(u, h) - min(0, h), u and h in s: the drift's share of the oscillator's phase
// about a window's center, when the oscillator follows its drift until h seconds after the center and holds its
// frequency from then on (h may be negative, the hold having begun before the center, or +infinity).
double ramp_integral_s2(double tau_s, double h_s) {
    if (h_s >= 0.0) return tau_s <= h_s ? 0.5 * tau_s * tau_s : h_s * tau_s - 0.5 * h_s * h_s;
    return tau_s >= h_s ? 0.0 : 0.5 * (tau_s - h_s) * (tau_s - h_s);
}

}  // namespace

CarrierTracker::CarrierTracker(double rate_hz, const ViterbiConfig& config, double start_offset_hz) {
    require(std::isfinite(rate_hz) && rate_hz > 0.0, "CarrierTracker: rate_hz must be positive and finite (samples/s)");
    require(std::isfinite(start_offset_hz), "CarrierTracker: start_offset_hz must be finite (Hz)");
    require(std::isfinite(config.tracker_window_s) && config.tracker_window_s * rate_hz >= 2.0 &&
                config.tracker_window_s * rate_hz <= static_cast<double>(kFftPoints),
            "CarrierTracker: tracker_window_s must hold 2 to 8 192 samples (s)");
    require(std::isfinite(config.tracker_hop_s) && config.tracker_hop_s * rate_hz >= 0.5 &&
                config.tracker_hop_s * rate_hz <= 1e12,
            "CarrierTracker: tracker_hop_s must be at least one sample (s)");
    require(std::isfinite(config.tracker_search_hz) && config.tracker_search_hz > 0.0,
            "CarrierTracker: tracker_search_hz must be positive and finite (Hz)");
    require(std::isfinite(config.tracker_gate_db) && config.tracker_gate_db > 0.0,
            "CarrierTracker: tracker_gate_db must be positive and finite (dB re the mean noise power per bin)");
    require(std::isfinite(config.tracker_drift_sd_hz_per_s) && config.tracker_drift_sd_hz_per_s >= 0.0,
            "CarrierTracker: tracker_drift_sd_hz_per_s must be finite and not negative (Hz/s)");
    require(std::isfinite(config.tracker_drift_noise) && config.tracker_drift_noise >= 0.0,
            "CarrierTracker: tracker_drift_noise must be finite and not negative (Hz^2/s^3)");

    rate_hz_ = rate_hz;
    window_samples_ = static_cast<std::size_t>(std::llround(config.tracker_window_s * rate_hz));
    hop_samples_ = static_cast<std::size_t>(std::llround(config.tracker_hop_s * rate_hz));
    // The bins within +/- tracker_search_hz of 0: k = -K ... K, K whole bins of rate / kFftPoints Hz.
    const double search_bins = std::floor(config.tracker_search_hz * static_cast<double>(kFftPoints) / rate_hz);
    require(search_bins >= 1.0 && search_bins + 2.0 <= static_cast<double>(kFftPoints / 2),
            "CarrierTracker: tracker_search_hz must span at least one bin and stay below half the sample rate (Hz)");
    search_bins_ = static_cast<std::size_t>(search_bins);
    search_hz_ = config.tracker_search_hz;
    gate_ratio_ = std::pow(10.0, config.tracker_gate_db / 10.0);
    drift_noise_ = config.tracker_drift_noise;
    agreement_drift_sd_hz_per_s_ = config.tracker_drift_sd_hz_per_s;

    // The periodic Hann window, symmetric about sample N / 2 rather than (N - 1) / 2: the window's center is then the
    // center of its interval, N / (2 rate) after its first sample, as measure() takes it. Its sums give the noise
    // bandwidth, rate sum(w^2) / sum(w)^2 (1.5 Hz for 1 s: sum w = N / 2, sum w^2 = 3 N / 8).
    hann_.resize(window_samples_);
    for (std::size_t i = 0; i < window_samples_; ++i) {
        hann_[i] = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i) / static_cast<double>(window_samples_));
        sum_w_ += hann_[i];
        sum_w2_ += hann_[i] * hann_[i];
    }

    spectrum_in_.assign(kFftPoints, std::complex<double>(0.0, 0.0));
    spectrum_out_.assign(kFftPoints, std::complex<double>(0.0, 0.0));

    offset_hz_ = start_offset_hz;
    p_offset_ = kInitialOffsetSdHz * kInitialOffsetSdHz;
    p_drift_ = config.tracker_drift_sd_hz_per_s * config.tracker_drift_sd_hz_per_s;
    set_hold();
}

double CarrierTracker::offset_hz(double t_s) const {
    return offset_hz_ + drift_hz_per_s_ * std::min(t_s - state_time_s_, hold_after_s_);
}

double CarrierTracker::predicted_variance_hz2(double dt_s) const {
    const double span = std::abs(dt_s);
    return p_offset_ + 2.0 * dt_s * p_cross_ + dt_s * dt_s * p_drift_ + drift_noise_ * span * span * span / 3.0;
}

double CarrierTracker::frequency_sd_hz(double t_s) const {
    return std::sqrt(std::max(0.0, predicted_variance_hz2(t_s - state_time_s_)));
}

double CarrierTracker::noise_power_per_bin_fs2(double noise_density_fs2_per_hz) const {
    return noise_density_fs2_per_hz * rate_hz_ * sum_w2_ / (sum_w_ * sum_w_);
}

void CarrierTracker::set_hold() {
    // The oscillator follows the drift while the predicted offset is trusted (stage 3): find the last time after the
    // state's at which it is. The predicted variance does not decrease with time (its cross term is never negative: the
    // start's is 0, a prediction adds dt P11 + q dt^2 / 2 to it, an update scales it by R / S, a restart sets R / gap),
    // so the trusted times are an interval from the state's time.
    if (!trusted(predicted_variance_hz2(0.0))) {
        hold_after_s_ = 0.0;
        return;
    }
    double high = 1.0;
    while (trusted(predicted_variance_hz2(high))) {
        high *= 2.0;
        if (high > 1e12) {  // never untrusted: a drift known exactly, without process noise
            hold_after_s_ = kInf;
            return;
        }
    }
    double low = 0.0;
    for (int step = 0; step < 200 && high - low > 1e-12 * high; ++step) {
        const double middle = 0.5 * (low + high);
        (trusted(predicted_variance_hz2(middle)) ? low : high) = middle;
    }
    hold_after_s_ = low;
}

void CarrierTracker::push(std::span<const Sample> samples, double t0_s, double noise_density_fs2_per_hz) {
    if (samples.empty()) return;
    require(std::isfinite(t0_s), "CarrierTracker::push: t0_s must be finite (s)");
    require(std::isfinite(noise_density_fs2_per_hz) && noise_density_fs2_per_hz >= 0.0,
            "CarrierTracker::push: the noise density must be finite and not negative (FS^2/Hz)");
    if (!started_) {
        origin_s_ = t0_s;
        state_time_s_ = t0_s;
    } else {
        const double expected_s = origin_s_ + static_cast<double>(seen_) / rate_hz_;
        require(std::abs(t0_s - expected_s) <= 0.5 / rate_hz_,
                "CarrierTracker::push: a block must start where the previous one ended (to within half a sample)");
    }
    started_ = true;
    buffer_.insert(buffer_.end(), samples.begin(), samples.end());
    seen_ += samples.size();
    while (next_window_ + window_samples_ <= seen_) {
        measure(next_window_, noise_density_fs2_per_hz);
        next_window_ += hop_samples_;
    }
    // Keep only what a later window needs: from the next window's first sample on.
    const std::uint64_t keep_from = std::min(next_window_, seen_);
    if (keep_from > buffer_first_) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(keep_from - buffer_first_));
        buffer_first_ = keep_from;
    }
}

void CarrierTracker::measure(std::uint64_t first, double noise_density_fs2_per_hz) {
    const double rate = rate_hz_;
    const double n_window = static_cast<double>(window_samples_);
    // s: the window's center, the instant a measurement belongs to.
    const double center_s = origin_s_ + (static_cast<double>(first) + 0.5 * n_window) / rate;
    Window window;
    window.center_s = center_s;
    ++counts_.windows;

    // a. The state predicted to the center, and the trust rule.
    const double dt = center_s - state_time_s_;
    const double offset_pred = offset_hz_ + drift_hz_per_s_ * dt;
    const double q = drift_noise_;
    const double p00 = predicted_variance_hz2(dt);
    const double p01 = p_cross_ + dt * p_drift_ + q * dt * dt / 2.0;
    const double p11 = p_drift_ + q * dt;
    const bool confirmed = trusted(p00);
    window.confirmed = confirmed;
    window.frequency_sd_hz = std::sqrt(p00);
    if (!confirmed) ++counts_.unconfirmed;

    // b. The window shifted by the oscillator itself: sample i, tau_i = (i - N/2) / rate s from the center, is turned
    // by minus the oscillator's phase about the center, its frequency integrated (it follows the drift until the hold,
    // h seconds from the center, and is steady after).
    const double oscillator_hz = offset_hz(center_s);
    const double hold_from_center_s = state_time_s_ + hold_after_s_ - center_s;
    const Sample* x = buffer_.data() + (first - buffer_first_);
    for (std::size_t i = 0; i < window_samples_; ++i) {
        const double tau_s = (static_cast<double>(i) - 0.5 * n_window) / rate;
        const double cycles = oscillator_hz * tau_s + drift_hz_per_s_ * ramp_integral_s2(tau_s, hold_from_center_s);
        const std::complex<double> sample(static_cast<double>(x[i].real()), static_cast<double>(x[i].imag()));
        spectrum_in_[i] = (hann_[i] * sample) * std::polar(1.0, -kTwoPi * cycles);
    }
    detail::fft_forward(spectrum_in_.data(), spectrum_out_.data(), kFftPoints);

    // c. The largest of the bins within +/- tracker_search_hz of 0 (k = -K ... K).
    const auto points = static_cast<std::ptrdiff_t>(kFftPoints);
    const auto bin = [this, points](std::ptrdiff_t k) {
        return spectrum_out_[static_cast<std::size_t>((k + points) % points)];  // bin k of the transform, k < 0 wraps
    };
    const auto search = static_cast<std::ptrdiff_t>(search_bins_);
    std::ptrdiff_t peak_k = -search;
    double peak_power = -1.0;
    bool finite = true;
    for (std::ptrdiff_t k = -search; k <= search; ++k) {
        const double power = std::norm(bin(k));
        finite = finite && std::isfinite(power);
        if (power > peak_power) {
            peak_power = power;
            peak_k = k;
        }
    }

    // d. The gate, against the noise power expected in one bin: N0 rate sum(w^2) in the transform's own scale (in FS^2,
    // a tone of amplitude A reading A^2, that is N0 x 1.5 Hz).
    const double norm = sum_w_ * sum_w_;  // a tone of amplitude A reads A^2 norm
    const double noise_power = noise_density_fs2_per_hz * rate * sum_w2_;
    window.noise_per_bin_fs2 = noise_power / norm;
    window.peak_fs2 = finite ? peak_power / norm : kNaN;
    bool measured = finite && noise_power > 0.0 && peak_power > 0.0 && peak_power >= gate_ratio_ * noise_power;

    // e. A parabola through the peak and its two neighbors, in dB: its vertex is the measured residual of the window.
    // The peak must be a strict local maximum: at the edge of the search range the bin outside may be larger (a line
    // out there, or the skirt of one), and a flat top has no vertex.
    double vertex_bins = kNaN;
    if (measured) {
        const double below = std::norm(bin(peak_k - 1));
        const double above = std::norm(bin(peak_k + 1));
        if (below > 0.0 && above > 0.0 && below <= peak_power && above <= peak_power)
            vertex_bins = parabola_vertex_bins(10.0 * std::log10(below), 10.0 * std::log10(peak_power),
                                               10.0 * std::log10(above));
        measured = std::isfinite(vertex_bins);
    }
    if (!measured) {
        pending_ = false;
        window.outcome = Outcome::NotMeasured;
        ++counts_.not_measured;
        last_ = window;
        return;
    }

    // f. The measurement, the offset at the center, and its variance from the line's signal-to-noise ratio: the peak's
    // power over the noise per bin, less the noise's own share (positive, the gate being above 0 dB).
    const double residual_hz = (static_cast<double>(peak_k) + vertex_bins) * rate / static_cast<double>(kFftPoints);
    const double measured_hz = oscillator_hz + residual_hz;
    const double snr = peak_power / noise_power - 1.0;
    const double variance = kVarianceScaleHz2 / snr + kBiasFloorHz * kBiasFloorHz;
    window.measured_hz = measured_hz;
    window.variance_hz2 = variance;

    // g. A confirmed track: the Kalman update when the innovation lies within the acceptance window, 3 sqrt(S) capped
    // at the search range.
    if (confirmed) {
        const double innovation_variance = p00 + variance;
        const double acceptance_hz = std::min(kSigmas * std::sqrt(innovation_variance), search_hz_);
        window.acceptance_hz = acceptance_hz;
        const double innovation = measured_hz - offset_pred;
        if (std::abs(innovation) <= acceptance_hz) {
            const double gain_offset = p00 / innovation_variance;
            const double gain_drift = p01 / innovation_variance;
            // The Joseph form of P = (I - K H) P (I - K H)' + K R K', H = [1 0].
            const double keep = 1.0 - gain_offset;
            const double new_p00 = keep * keep * p00 + gain_offset * gain_offset * variance;
            const double new_p01 = keep * (p01 - gain_drift * p00) + gain_offset * gain_drift * variance;
            const double new_p11 =
                gain_drift * gain_drift * p00 - 2.0 * gain_drift * p01 + p11 + gain_drift * gain_drift * variance;
            offset_hz_ = offset_pred + gain_offset * innovation;
            drift_hz_per_s_ += gain_drift * innovation;
            p_offset_ = new_p00;
            p_cross_ = new_p01;
            p_drift_ = new_p11;
            state_time_s_ = center_s;
            set_hold();
            pending_ = false;
            window.outcome = Outcome::Updated;
            ++counts_.updated;
            last_ = window;
            return;
        }
    }

    // h. Otherwise (an unconfirmed track, or outside the acceptance window) the measurement must agree with the
    // previous window's: |z2 - z1| <= 3 sqrt(R1 + R2 + (sigma_d gap)^2), sigma_d = tracker_drift_sd_hz_per_s, the
    // spread of any carrier's drift (the test compares z2 with z1, not with an estimated drift). Two that agree restart
    // the filter at the second, with the two-point estimate's drift and covariance; the second stays pending for the
    // next window.
    if (pending_) {
        const double gap_s = center_s - pending_time_s_;
        const double drift_hz = agreement_drift_sd_hz_per_s_ * gap_s;  // the drift's spread over the gap
        const double limit_hz = kSigmas * std::sqrt(pending_variance_hz2_ + variance + drift_hz * drift_hz);
        window.agreement_hz = limit_hz;
        if (std::abs(measured_hz - pending_hz_) <= limit_hz) {
            offset_hz_ = measured_hz;
            drift_hz_per_s_ = (measured_hz - pending_hz_) / gap_s;
            p_offset_ = variance;
            p_cross_ = variance / gap_s;
            p_drift_ = (variance + pending_variance_hz2_) / (gap_s * gap_s);
            state_time_s_ = center_s;
            set_hold();
            window.outcome = Outcome::Restarted;
            ++counts_.restarted;
            pending_hz_ = measured_hz;
            pending_variance_hz2_ = variance;
            pending_time_s_ = center_s;
            last_ = window;
            return;
        }
    }
    pending_ = true;
    pending_hz_ = measured_hz;
    pending_variance_hz2_ = variance;
    pending_time_s_ = center_s;
    window.outcome = Outcome::Pending;
    ++counts_.pending;
    last_ = window;
}

}  // namespace kz4ap::viterbi
