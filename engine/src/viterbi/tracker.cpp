#include "kz4ap/viterbi/tracker.hpp"

#include "fft.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <stdexcept>

namespace kz4ap::viterbi {
namespace {

// points: the length of the zero-padded transform. 8 192 is 5.46 times the 1 500 samples of the default window, and at
// 1 500 samples/s a bin is 1 500 / 8 192 = 0.1831 Hz (the plan's choice; heuristic).
constexpr std::size_t kFftPoints = 8192;

// Hz and Hz/s: the standard deviations of the initial state, the offset and the drift rate (the drift rate's also after
// a restart; the controller's rulings; placeholders).
constexpr double kInitialOffsetSdHz = 5.0;
constexpr double kInitialDriftSdHzPerS = 1.0;

// dimensionless: a used measurement updates the filter when its innovation lies within this many standard deviations of
// the innovation's own (the controller's ruling; placeholder).
constexpr double kValidationSigmas = 3.0;

// Hz: two measurements outside the validation gate that lie within this of each other are one new carrier (the
// controller's ruling; placeholder).
constexpr double kRestartAgreementHz = 1.0;

constexpr double kTwoPi = 2.0 * std::numbers::pi;

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

// The median of the values, which the call reorders: the middle one of an odd count, the mean of the two middle ones of
// an even count.
double median_of(std::vector<double>& values) {
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    if (values.size() % 2 == 1) return *middle;
    return 0.5 * (*std::max_element(values.begin(), middle) + *middle);
}

// The vertex, in bins from the middle point, of the parabola through three equally spaced values in dB whose middle one
// is not below the others: 0.5 (below - above) / (below - 2 middle + above), within +/- 0.5; 0 for three equal values.
double parabola_vertex_bins(double db_below, double db_middle, double db_above) {
    const double curvature = db_below - 2.0 * db_middle + db_above;  // dB, not positive at a local maximum
    return curvature < 0.0 ? 0.5 * (db_below - db_above) / curvature : 0.0;
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
    require(std::isfinite(config.tracker_gate_db),
            "CarrierTracker: tracker_gate_db must be finite (dB re the median searched bin)");
    require(std::isfinite(config.tracker_measurement_sd_hz) && config.tracker_measurement_sd_hz > 0.0,
            "CarrierTracker: tracker_measurement_sd_hz must be positive and finite (Hz)");
    require(std::isfinite(config.tracker_drift_sd_hz_per_s) && config.tracker_drift_sd_hz_per_s >= 0.0,
            "CarrierTracker: tracker_drift_sd_hz_per_s must be finite and not negative (Hz/s per sqrt(s))");

    rate_hz_ = rate_hz;
    window_samples_ = static_cast<std::size_t>(std::llround(config.tracker_window_s * rate_hz));
    hop_samples_ = static_cast<std::size_t>(std::llround(config.tracker_hop_s * rate_hz));
    // The bins within +/- tracker_search_hz of 0: k = -K ... K, K whole bins of rate / kFftPoints Hz.
    const double search_bins = std::floor(config.tracker_search_hz * static_cast<double>(kFftPoints) / rate_hz);
    require(search_bins >= 1.0 && search_bins + 2.0 <= static_cast<double>(kFftPoints / 2),
            "CarrierTracker: tracker_search_hz must span at least one bin and stay below half the sample rate (Hz)");
    search_bins_ = static_cast<std::size_t>(search_bins);
    gate_ratio_ = std::pow(10.0, config.tracker_gate_db / 10.0);
    measurement_variance_hz2_ = config.tracker_measurement_sd_hz * config.tracker_measurement_sd_hz;
    drift_density_hz2_per_s3_ = config.tracker_drift_sd_hz_per_s * config.tracker_drift_sd_hz_per_s;

    // The periodic Hann window, symmetric about sample N / 2 rather than (N - 1) / 2: the window's center is then the
    // center of its interval, N / (2 rate) after its first sample, as measure() takes it.
    hann_.resize(window_samples_);
    for (std::size_t i = 0; i < window_samples_; ++i)
        hann_[i] = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i) / static_cast<double>(window_samples_));

    spectrum_in_.assign(kFftPoints, std::complex<double>(0.0, 0.0));
    spectrum_out_.assign(kFftPoints, std::complex<double>(0.0, 0.0));
    powers_.resize(2 * search_bins_ + 1);

    offset_hz_ = start_offset_hz;
    p_offset_ = kInitialOffsetSdHz * kInitialOffsetSdHz;
    p_drift_ = kInitialDriftSdHzPerS * kInitialDriftSdHzPerS;
}

double CarrierTracker::offset_hz(double t_s) const { return offset_hz_ + drift_hz_per_s_ * (t_s - state_time_s_); }

void CarrierTracker::push(std::span<const Sample> samples, double t0_s) {
    if (samples.empty()) return;
    require(std::isfinite(t0_s), "CarrierTracker::push: t0_s must be finite (s)");
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
        measure(next_window_);
        next_window_ += hop_samples_;
    }
    // Keep only what a later window needs: from the next window's first sample on.
    const std::uint64_t keep_from = std::min(next_window_, seen_);
    if (keep_from > buffer_first_) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(keep_from - buffer_first_));
        buffer_first_ = keep_from;
    }
}

void CarrierTracker::measure(std::uint64_t first) {
    const double rate = rate_hz_;
    const double n_window = static_cast<double>(window_samples_);
    // s: the window's center, the instant a measurement belongs to.
    const double center_s = origin_s_ + (static_cast<double>(first) + 0.5 * n_window) / rate;

    // 1. The state predicted to the center: the offset advanced by the drift, the covariance by the continuous
    // white-noise acceleration model, Q = q [[dt^3 / 3, dt^2 / 2], [dt^2 / 2, dt]] with q in Hz^2/s^3.
    const double dt = center_s - state_time_s_;
    const double offset_pred = offset_hz_ + drift_hz_per_s_ * dt;
    const double q = drift_density_hz2_per_s3_;
    const double p00 = p_offset_ + 2.0 * dt * p_cross_ + dt * dt * p_drift_ + q * dt * dt * dt / 3.0;
    const double p01 = p_cross_ + dt * p_drift_ + q * dt * dt / 2.0;
    const double p11 = p_drift_ + q * dt;

    // 2. The window shifted by the predicted frequency, offset_pred + drift (t - center), sample by sample (the phase,
    // in cycles, is that frequency's integral about the center), Hann-windowed and zero-padded to kFftPoints.
    const Sample* x = buffer_.data() + (first - buffer_first_);
    for (std::size_t i = 0; i < window_samples_; ++i) {
        const double tau_s = (static_cast<double>(i) - 0.5 * n_window) / rate;
        const double cycles = offset_pred * tau_s + 0.5 * drift_hz_per_s_ * tau_s * tau_s;
        const std::complex<double> sample(static_cast<double>(x[i].real()), static_cast<double>(x[i].imag()));
        spectrum_in_[i] = (hann_[i] * sample) * std::polar(1.0, -kTwoPi * cycles);
    }
    detail::fft_forward(spectrum_in_.data(), spectrum_out_.data(), kFftPoints);

    // 3. The largest of the bins within +/- tracker_search_hz of 0 (k = -K ... K), and the median of their powers.
    const auto points = static_cast<std::ptrdiff_t>(kFftPoints);
    const auto bin = [this, points](std::ptrdiff_t k) {
        return spectrum_out_[static_cast<std::size_t>((k + points) % points)];  // bin k of the transform, k < 0 wraps
    };
    const auto search = static_cast<std::ptrdiff_t>(search_bins_);
    std::ptrdiff_t peak_k = -search;
    double peak_power = -1.0;
    for (std::ptrdiff_t k = -search; k <= search; ++k) {
        const double power = std::norm(bin(k));
        if (!std::isfinite(power)) return;  // garbage in: nothing to measure
        powers_[static_cast<std::size_t>(k + search)] = power;
        if (power > peak_power) {
            peak_power = power;
            peak_k = k;
        }
    }
    const double median_power = median_of(powers_);

    // 4. The gate: the peak must stand tracker_gate_db above the median searched bin. (A window of exact zeros has
    // nothing to measure.)
    if (!(peak_power > 0.0) || peak_power < gate_ratio_ * median_power) return;

    // 5. A parabola through the peak and its two neighbors, in dB: its vertex is the measured residual of the window.
    // The peak must be a local maximum: at the edge of the search range the bin outside may be larger (a line out
    // there, or the skirt of one), and then there is no vertex to find.
    const double below = std::norm(bin(peak_k - 1));
    const double above = std::norm(bin(peak_k + 1));
    if (!(below > 0.0) || !(above > 0.0) || below > peak_power || above > peak_power) return;
    const double vertex_bins =
        parabola_vertex_bins(10.0 * std::log10(below), 10.0 * std::log10(peak_power), 10.0 * std::log10(above));
    // Hz: the window's residual from the shift, its average over the window (the value at its center under a steady
    // drift), and the measurement, the offset at the center.
    const double residual_hz = (static_cast<double>(peak_k) + vertex_bins) * rate / static_cast<double>(kFftPoints);
    const double measured_hz = offset_pred + residual_hz;

    // 6. Validation. Inside 3 sqrt(S) of the prediction: the Kalman update. Outside: held, and a second measurement
    // outside, within kRestartAgreementHz of the held one, is a new carrier, where the filter restarts.
    const double innovation_variance = p00 + measurement_variance_hz2_;
    if (std::abs(residual_hz) <= kValidationSigmas * std::sqrt(innovation_variance)) {
        const double gain_offset = p00 / innovation_variance;
        const double gain_drift = p01 / innovation_variance;
        // The Joseph form of P = (I - K H) P (I - K H)' + K R K', H = [1 0].
        const double keep = 1.0 - gain_offset;
        const double new_p00 = keep * keep * p00 + gain_offset * gain_offset * measurement_variance_hz2_;
        const double new_p01 = keep * (p01 - gain_drift * p00) + gain_offset * gain_drift * measurement_variance_hz2_;
        const double new_p11 = gain_drift * gain_drift * p00 - 2.0 * gain_drift * p01 + p11 +
                               gain_drift * gain_drift * measurement_variance_hz2_;
        offset_hz_ = offset_pred + gain_offset * residual_hz;
        drift_hz_per_s_ += gain_drift * residual_hz;
        p_offset_ = new_p00;
        p_cross_ = new_p01;
        p_drift_ = new_p11;
        state_time_s_ = center_s;
        held_ = false;
    } else if (held_ && std::abs(measured_hz - held_hz_) <= kRestartAgreementHz) {
        offset_hz_ = measured_hz;
        drift_hz_per_s_ = 0.0;
        p_offset_ = measurement_variance_hz2_;
        p_cross_ = 0.0;
        p_drift_ = kInitialDriftSdHzPerS * kInitialDriftSdHzPerS;
        state_time_s_ = center_s;
        held_ = false;
    } else {
        held_ = true;
        held_hz_ = measured_hz;
    }
}

}  // namespace kz4ap::viterbi
