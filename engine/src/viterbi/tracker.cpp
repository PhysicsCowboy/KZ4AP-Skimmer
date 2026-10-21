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

// Hz: the standard deviation of the start's offset around the engine's anchor (placeholder; the detector's anchor is
// within +/-11.7 Hz of the carrier, and in oracle mode the label's frequency is exact).
constexpr double kInitialOffsetSdHz = 5.0;

// dimensionless: the standard deviations of the trust rule, the acceptance window and the agreement test (the standard
// 3 sigma).
constexpr double kSigmas = 3.0;

// Hz: the trust rule's tolerance, fixed whatever the fading spread: 0.26 cycles over the decoder's default block,
// 0.390 s (195 pieces of 2 ms at the default spread of 1 Hz and 0.5 dB of signal energy), 0.667 Hz. A residual Delta f
// costs under 1 dB of signal power while Delta f T <= 0.26 for a coherent sum of length T (sinc^2(0.26) = 0.797;
// derived). The tolerance guards against a false update moving the frequency further than the default block tolerates;
// making it stricter does not make the estimate better (tracker.hpp, 2a).
constexpr double kToleranceHz = 0.26 / 0.390;

// Hz^2: c in a measurement's variance c / rho, rho the line's power over the mean noise power per bin. Measured: the
// least-squares fit in ln(variance), the slope fixed at the Cramer-Rao form's -1, to the scatter of the keyed carrier
// measured with the tracker's first design (sd 0.116, 0.064, 0.037, 0.021 and 0.007 Hz at -5, 0, 5, 10 and 20 dB S500,
// PARIS at 25 WPM; rho = 0.44^2 / (N0 x 1.5 Hz) for a unit carrier).
constexpr double kVarianceScaleHz2 = 0.2833;

// Hz: the floor of a measurement's standard deviation, the parabola's own bias on a clean tone (measured: at most
// 0.08 mHz, by a calculation of the same estimator).
constexpr double kBiasFloorHz = 0.08e-3;

// dimensionless: the correlation of two consecutive windows' frequency errors at a hop of half the window, measured on
// the keyed carrier (PARIS, 25 WPM, 0 dB S500), against -0.4526 derived for the noise alone. Another hop takes the
// noise-only correlation derived for it, times the same ratio, -0.35 / -0.4526 (2g).
constexpr double kHalfHopCorrelation = -0.35;

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

// The trust rule: an offset predicted with this variance (Hz^2) is trusted while 3 sigma_f <= the tolerance.
bool trusted(double variance_hz2) { return kSigmas * std::sqrt(variance_hz2) <= kToleranceHz; }

// The correlation of the noise's share of two windows' frequency errors when the second starts `hop` samples after
// the first: each error is a sum of the noise weighted by w_i tau_i (tau from its own window's center), so the
// correlation is sum_i b_i b_(i + hop) / sum_i b_i^2, b_i = w_i tau_i (derived; 0 when the windows share no sample).
double noise_correlation(const std::vector<double>& w, std::size_t hop) {
    const std::size_t n = w.size();
    if (hop >= n) return 0.0;
    double products = 0.0;
    double squares = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double b = w[i] * (static_cast<double>(i) - 0.5 * static_cast<double>(n));
        squares += b * b;
        if (i + hop < n) products += b * w[i + hop] * (static_cast<double>(i + hop) - 0.5 * static_cast<double>(n));
    }
    return products / squares;
}

// The vertex, in bins from the middle point, of the parabola through three equally spaced values in dB re any one
// reference, whose middle one is not below the others: 0.5 (below - above) / (below - 2 middle + above), within
// +/- 0.5. NaN when the three do not make a strictly concave parabola (a flat top has no vertex).
double parabola_vertex_bins(double db_below, double db_middle, double db_above) {
    // dB of power ratio (the reference cancels), negative at a strict local maximum
    const double curvature = db_below - 2.0 * db_middle + db_above;
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
    // 8 samples at least, so that each of the presence profile's blocks holds a sample of positive weight (the periodic
    // Hann window's first sample is 0; 2f); 7.5 rounds to 8.
    require(std::isfinite(config.tracker_window_s) && config.tracker_window_s * rate_hz >= 7.5 &&
                config.tracker_window_s * rate_hz <= static_cast<double>(kFftPoints),
            "CarrierTracker: tracker_window_s must hold 8 to 8 192 samples (s)");
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
    // The moving-average model of consecutive windows' errors (2g) needs windows two hops apart to share no sample.
    require(2 * hop_samples_ >= window_samples_,
            "CarrierTracker: tracker_hop_s must be at least half of tracker_window_s (s)");
    // The bins within +/- tracker_search_hz of 0: k = -K ... K, K whole bins of rate / kFftPoints Hz.
    const double search_bins = std::floor(config.tracker_search_hz * static_cast<double>(kFftPoints) / rate_hz);
    require(search_bins >= 1.0 && search_bins + 2.0 <= static_cast<double>(kFftPoints / 2),
            "CarrierTracker: tracker_search_hz must span at least one bin and stay below half the sample rate (Hz)");
    search_bins_ = static_cast<std::size_t>(search_bins);
    search_hz_ = config.tracker_search_hz;
    gate_ratio_ = std::pow(10.0, config.tracker_gate_db / 10.0);
    drift_noise_ = config.tracker_drift_noise;
    drift_sd_hz_per_s_ = config.tracker_drift_sd_hz_per_s;

    // The periodic Hann window, symmetric about sample N / 2 rather than (N - 1) / 2: the window's center is then the
    // center of its interval, N / (2 rate) after its first sample, as measure() takes it. Its sums give the noise
    // bandwidth, rate sum(w^2) / sum(w)^2 (1.5 Hz for 1 s: sum w = N / 2, sum w^2 = 3 N / 8), and its moments per
    // block those of the presence profile (2f).
    const double n = static_cast<double>(window_samples_);
    hann_.resize(window_samples_);
    for (std::size_t i = 0; i < window_samples_; ++i) {
        hann_[i] = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i) / n);
        sum_w_ += hann_[i];
        sum_w2_ += hann_[i] * hann_[i];
        const double tau_s = (static_cast<double>(i) - 0.5 * n) / rate_hz;
        noise_moment1_ += hann_[i] * hann_[i] * tau_s;
        noise_moment2_ += hann_[i] * hann_[i] * tau_s * tau_s;
    }
    for (std::size_t j = 0; j <= kProfileBlocks; ++j) block_first_[j] = j * window_samples_ / kProfileBlocks;
    for (std::size_t j = 0; j < kProfileBlocks; ++j) {
        for (std::size_t i = block_first_[j]; i < block_first_[j + 1]; ++i) {
            const double tau_s = (static_cast<double>(i) - 0.5 * n) / rate_hz;
            block_moment0_[j] += hann_[i];
            block_moment1_[j] += hann_[i] * tau_s;
            block_moment2_[j] += hann_[i] * tau_s * tau_s;
            block_w2_[j] += hann_[i] * hann_[i];
        }
    }
    // The correlation of consecutive windows' errors for this hop (2g): the measured one at half the window, scaled by
    // the noise-only correlation derived for the hop; and the moving-average coefficient theta of e_k = s_k (a_k +
    // theta a_(k-1)), a white and of unit variance, whose lag-one correlation theta / (1 + theta^2) it is (the root
    // with |theta| < 1; 0 when the windows share no sample).
    correlation_ = kHalfHopCorrelation * noise_correlation(hann_, hop_samples_) /
                   noise_correlation(hann_, window_samples_ / 2);
    theta_ = correlation_ == 0.0 ? 0.0
                                 : (1.0 - std::sqrt(1.0 - 4.0 * correlation_ * correlation_)) / (2.0 * correlation_);
    full_profile_ = 1.0;
    const double uniform[kProfileBlocks] = {1.0, 1.0, 1.0, 1.0};
    double centroid_s = 0.0;
    full_profile_ = fill_factor(uniform, centroid_s);  // F of a filled window, in s^-2: the reference

    spectrum_in_.assign(kFftPoints, std::complex<double>(0.0, 0.0));
    spectrum_out_.assign(kFftPoints, std::complex<double>(0.0, 0.0));

    offset_hz_ = start_offset_hz;
    p_offset_ = kInitialOffsetSdHz * kInitialOffsetSdHz;
    p_drift_ = config.tracker_drift_sd_hz_per_s * config.tracker_drift_sd_hz_per_s;
    forget_noise_state();
    set_hold();
}

double CarrierTracker::fill_factor(const double* levels, double& centroid_s) const {
    // F = (sum g)^2 sum w^2 (tau - u)^2 / (sum w^2 (sum g (tau - u)^2)^2), g = w x the block's level, u = the line's
    // centroid; over full_profile_ (2f).
    double g0 = 0.0;
    double g1 = 0.0;
    for (std::size_t j = 0; j < kProfileBlocks; ++j) {
        g0 += levels[j] * block_moment0_[j];
        g1 += levels[j] * block_moment1_[j];
    }
    if (!(g0 > 0.0)) {
        // No block above its noise: possible only under a gate below 10 log10(4) = 6.02 dB re the mean noise power
        // per bin (at or above it, the four blocks' sums cannot all hold less than their noise and still make the
        // peak), and then the profile is taken as filled.
        centroid_s = 0.0;
        return 1.0;
    }
    const double u = g1 / g0;
    double g2 = 0.0;
    for (std::size_t j = 0; j < kProfileBlocks; ++j)
        g2 += levels[j] * (block_moment2_[j] - 2.0 * u * block_moment1_[j] + u * u * block_moment0_[j]);
    const double noise = noise_moment2_ - 2.0 * u * noise_moment1_ + u * u * sum_w2_;
    centroid_s = u;
    return g0 * g0 * noise / (sum_w2_ * g2 * g2) / full_profile_;
}

void CarrierTracker::forget_noise_state() {
    noise_hat_ = 0.0;
    p_fa_ = 0.0;
    p_da_ = 0.0;
    p_aa_ = 1.0;
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
    // state's at which it is. The predicted variance's derivative, 2 P01 + 2 P11 dt + q dt^2, does not decrease for
    // dt >= 0 (P11 and q are not negative): after the state's time the variance falls at most once and then rises, so
    // if it is trusted at dt = 0 the trusted times are one interval from there, whose end the bisection finds; if not,
    // the oscillator holds from the state's time.
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

    // a. The state predicted to the center, and the trust rule. The previous window's noise component (g) keeps its
    // covariance with the state's offset and drift at the state's time.
    const double dt = center_s - state_time_s_;
    const double offset_pred = offset_hz_ + drift_hz_per_s_ * dt;
    const double q = drift_noise_;
    const double p00 = predicted_variance_hz2(dt);
    const double p01 = p_cross_ + dt * p_drift_ + q * dt * dt / 2.0;
    const double p11 = p_drift_ + q * dt;
    const double pfa = p_fa_ + dt * p_da_;
    const double pda = p_da_;
    const double paa = p_aa_;
    const bool confirmed = trusted(p00);
    window.confirmed = confirmed;
    window.frequency_sd_hz = std::sqrt(p00);
    if (!confirmed) ++counts_.unconfirmed;

    // b. The window shifted by the oscillator itself: sample i, tau_i = (i - N/2) / r s from the center, is turned
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

    // c. The peak: the largest of the bins within +/- tracker_search_hz of 0 (k = -K ... K); on a confirmed track, the
    // largest within the acceptance window of a filled window's peak at the gate, if that one passes the gate and is a
    // strict local maximum.
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
    const double norm = sum_w_ * sum_w_;  // a tone of amplitude A reads A^2 norm
    const double noise_power = noise_density_fs2_per_hz * rate * sum_w2_;
    if (confirmed && finite && noise_power > 0.0) {
        const double widest_hz2 = kVarianceScaleHz2 / (gate_ratio_ - 1.0) + kBiasFloorHz * kBiasFloorHz;
        const double inner_hz = std::min(kSigmas * std::sqrt(p00 + widest_hz2), search_hz_);
        const auto inner =
            std::min(search, static_cast<std::ptrdiff_t>(std::floor(inner_hz * kFftPoints / rate)) + 1);
        std::ptrdiff_t inner_k = 0;
        double inner_power = -1.0;
        for (std::ptrdiff_t k = -inner; k <= inner; ++k) {
            const double power = std::norm(bin(k));
            if (power > inner_power) {
                inner_power = power;
                inner_k = k;
            }
        }
        if (inner_power >= gate_ratio_ * noise_power && std::norm(bin(inner_k - 1)) < inner_power &&
            std::norm(bin(inner_k + 1)) < inner_power) {
            peak_power = inner_power;
            peak_k = inner_k;
        }
    }

    // d. The gate, against the noise power expected in one bin: N0 rate sum(w^2) in the transform's own scale (in FS^2,
    // a tone of amplitude A reading A^2, that is N0 x 1.5 Hz).
    window.noise_per_bin_fs2 = noise_power / norm;
    window.peak_fs2 = finite ? peak_power / norm : kNaN;
    bool measured = finite && noise_power > 0.0 && peak_power > 0.0 && peak_power >= gate_ratio_ * noise_power;

    // e. A parabola through the peak and its two neighbors, in dB re 1 FS^2 in the transform's scale (the vertex does
    // not depend on the reference): its vertex is the measured residual of the window. The peak must be a strict local
    // maximum: at the edge of the search range the bin outside may be larger (a line out there, or the skirt of one),
    // and a flat top has no vertex.
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
        forget_noise_state();
        window.outcome = Outcome::NotMeasured;
        ++counts_.not_measured;
        last_ = window;
        return;
    }

    // f. The measurement, the offset at the center, and its variance: the Cramer-Rao form in the line's
    // signal-to-noise ratio (the peak's power over the noise per bin, less the noise's own share; positive, the gate
    // being above 0 dB re the mean noise power per bin), times the fill factor of the line's presence profile, plus the
    // drift's share of attributing it to the center.
    const double residual_hz = (static_cast<double>(peak_k) + vertex_bins) * rate / static_cast<double>(kFftPoints);
    const double measured_hz = oscillator_hz + residual_hz;
    const double snr = peak_power / noise_power - 1.0;
    double levels[kProfileBlocks];
    const std::complex<double> turn = std::polar(1.0, -kTwoPi * residual_hz / rate);
    for (std::size_t j = 0; j < kProfileBlocks; ++j) {
        // The line's coherent sum over the block, at the measured residual, less the noise's expected power in it.
        const double tau0_s = (static_cast<double>(block_first_[j]) - 0.5 * n_window) / rate;
        std::complex<double> phasor = std::polar(1.0, -kTwoPi * residual_hz * tau0_s);
        std::complex<double> sum(0.0, 0.0);
        for (std::size_t i = block_first_[j]; i < block_first_[j + 1]; ++i) {
            sum += spectrum_in_[i] * phasor;
            phasor *= turn;
        }
        const double line_power = std::norm(sum) - noise_density_fs2_per_hz * rate * block_w2_[j];
        levels[j] = std::sqrt(std::max(line_power, 0.0)) / block_moment0_[j];
    }
    double centroid_s = 0.0;
    const double factor = fill_factor(levels, centroid_s);
    const double attribution_hz = drift_sd_hz_per_s_ * centroid_s;
    const double variance =
        kVarianceScaleHz2 / snr * factor + attribution_hz * attribution_hz + kBiasFloorHz * kBiasFloorHz;
    window.measured_hz = measured_hz;
    window.variance_hz2 = variance;
    window.fill_factor = factor;

    // g. A confirmed track: the Kalman update when the innovation lies within the acceptance window, 3 sqrt(S) capped
    // at the search range. The measurement's error is s (theta a_prev + a_new): the state carries a_prev, the previous
    // window's component, so the update predicts the share of this window's error that the two windows' common samples
    // made, and keeps a_new for the next window.
    if (confirmed) {
        const double s = std::sqrt(variance / (1.0 + theta_ * theta_));
        const double h[4] = {1.0, 0.0, s * theta_, s};
        const double p[4][4] = {{p00, p01, pfa, 0.0}, {p01, p11, pda, 0.0}, {pfa, pda, paa, 0.0}, {0.0, 0.0, 0.0, 1.0}};
        double ph[4];
        for (int i = 0; i < 4; ++i) {
            ph[i] = 0.0;
            for (int j = 0; j < 4; ++j) ph[i] += p[i][j] * h[j];
        }
        // S = H P H': the offset's variance, the two windows' common share and this window's own.
        const double innovation_variance = p00 + 2.0 * s * theta_ * pfa + s * s * theta_ * theta_ * paa + s * s;
        const double innovation = measured_hz - (offset_pred + s * theta_ * noise_hat_);
        const double acceptance_hz = std::min(kSigmas * std::sqrt(innovation_variance), search_hz_);
        window.acceptance_hz = acceptance_hz;
        if (std::abs(innovation) <= acceptance_hz) {
            double gain[4];
            for (int i = 0; i < 4; ++i) gain[i] = ph[i] / innovation_variance;
            // The Joseph form P = (I - K H) P (I - K H)', the measurement's noise being in the state.
            double keep[4][4];
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j) keep[i][j] = (i == j ? 1.0 : 0.0) - gain[i] * h[j];
            double kp[4][4];
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j) {
                    kp[i][j] = 0.0;
                    for (int l = 0; l < 4; ++l) kp[i][j] += keep[i][l] * p[l][j];
                }
            double np[4][4];
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j) {
                    np[i][j] = 0.0;
                    for (int l = 0; l < 4; ++l) np[i][j] += kp[i][l] * keep[j][l];
                }
            offset_hz_ = offset_pred + gain[0] * innovation;
            drift_hz_per_s_ += gain[1] * innovation;
            noise_hat_ = gain[3] * innovation;
            p_offset_ = np[0][0];
            p_cross_ = 0.5 * (np[0][1] + np[1][0]);
            p_drift_ = np[1][1];
            p_fa_ = 0.5 * (np[0][3] + np[3][0]);
            p_da_ = 0.5 * (np[1][3] + np[3][1]);
            p_aa_ = np[3][3];
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
    // previous window's: |z2 - z1| <= 3 sqrt(R1 + R2 - 2 c12 + (sigma_d gap)^2), sigma_d = tracker_drift_sd_hz_per_s,
    // the spread of any carrier's drift (the test compares z2 with z1, not with an estimated drift), and c12 the two
    // errors' covariance when the windows share samples. Two that agree restart the filter at the second, with the
    // two-point estimate's drift and covariance; the second stays pending for the next window.
    if (pending_) {
        const double gap_s = center_s - pending_time_s_;  // one hop: the pending measurement is the previous window's
        const double c12 = correlation_ * std::sqrt(pending_variance_hz2_ * variance);  // Hz^2, 0 if they share none
        const double drift_hz = drift_sd_hz_per_s_ * gap_s;  // the drift's spread over the gap
        const double limit_hz =
            kSigmas * std::sqrt(pending_variance_hz2_ + variance - 2.0 * c12 + drift_hz * drift_hz);
        window.agreement_hz = limit_hz;
        if (std::abs(measured_hz - pending_hz_) <= limit_hz) {
            offset_hz_ = measured_hz;
            drift_hz_per_s_ = (measured_hz - pending_hz_) / gap_s;
            p_offset_ = variance;
            p_cross_ = (variance - c12) / gap_s;
            p_drift_ = (variance + pending_variance_hz2_ - 2.0 * c12) / (gap_s * gap_s);
            // The offset's error is minus this window's, s (theta a_prev + a_new): a_new, kept for the next window,
            // is correlated with the offset by -s and with the drift by -s / gap.
            const double s = std::sqrt(variance / (1.0 + theta_ * theta_));
            noise_hat_ = 0.0;
            p_fa_ = -s;
            p_da_ = -s / gap_s;
            p_aa_ = 1.0;
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
    forget_noise_state();
    window.outcome = Outcome::Pending;
    ++counts_.pending;
    last_ = window;
}

}  // namespace kz4ap::viterbi
