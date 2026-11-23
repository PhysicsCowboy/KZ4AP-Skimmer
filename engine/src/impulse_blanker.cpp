#include "kz4ap/impulse_blanker.hpp"

#include "fft.hpp"
#include "kz4ap/spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace kz4ap {

namespace {

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

// The median of values[0 ... n) (reordered), the mean of the middle two for an even n.
double median_of(double* values, std::size_t n) {
    const std::size_t mid = n / 2;
    std::nth_element(values, values + mid, values + n);
    const double upper = values[mid];
    if (n % 2 == 1) return upper;
    return 0.5 * (upper + *std::max_element(values, values + mid));
}

}  // namespace

ImpulseBlanker::ImpulseBlanker(const ImpulseBlankerConfig& config, int sample_rate, int fft_size, int decimation,
                               double band_lo_hz, double band_hi_hz, double keep_s)
    : config_(config), n_(fft_size), decimation_(decimation) {
    require(std::isfinite(config.threshold_db), "impulse blanker: threshold_db must be finite (dB re the sample's noise)");
    require(std::isfinite(config.subband_hz) && config.subband_hz > 0.0, "impulse blanker: subband_hz must be positive (Hz)");
    require(config.min_subbands >= 1, "impulse blanker: min_subbands must be at least 1");
    require(config.min_quiet_share >= 0.0 && config.min_quiet_share <= 1.0, "impulse blanker: min_quiet_share must lie in [0, 1]");
    require(std::isfinite(config.occupied_db), "impulse blanker: occupied_db must be finite (dB re the subband's lower quartile)");
    require(config.occupied_guard_bins >= 0, "impulse blanker: occupied_guard_bins must not be negative (bins)");
    require(std::isfinite(config.average_s) && config.average_s > 0.0, "impulse blanker: average_s must be positive (s)");
    require(std::isfinite(config.noise_memory_s) && config.noise_memory_s >= 0.0, "impulse blanker: noise_memory_s must not be negative (s)");
    require(std::isfinite(config.gate) && config.gate >= 0.0, "impulse blanker: gate must not be negative (power re the noise)");
    require(std::isfinite(config.max_run_s) && config.max_run_s > 0.0, "impulse blanker: max_run_s must be positive (s)");
    require(config.flat_ratio >= 1.0, "impulse blanker: flat_ratio must be at least 1 (linear, power)");
    require(sample_rate > 0 && fft_size >= 8 && fft_size % 4 == 0 && decimation >= 1 && std::isfinite(keep_s) && keep_s >= 0.0,
            "impulse blanker: invalid analyzer parameters");
    series_.threshold = std::pow(10.0, config.threshold_db / 10.0);
    const double bin_hz = static_cast<double>(sample_rate) / fft_size;
    // A power of two of bins, the nearest to subband_hz, at most the FFT.
    int bins = 1;
    while (bins * 2 <= fft_size && std::abs(std::log2(bins * 2 * bin_hz / config.subband_hz)) < std::abs(std::log2(bins * bin_hz / config.subband_hz)))
        bins *= 2;
    subband_bins_ = bins;
    step_ = fft_size / bins;
    const BinRange band = band_bins(band_lo_hz, band_hi_hz, sample_rate, fft_size);
    first_bin_ = band.first;
    subbands_ = (band.last - band.first + 1) / bins;
    // The central half of the window in a subband's time samples, B / 4 ... 3 B / 4, cut into channel samples: the input's
    // N / 4 must be whole channel samples and a channel sample whole time samples.
    if (decimation % step_ != 0 || (fft_size / 4) % decimation != 0 || bins < 4) subbands_ = 0;
    if (!active()) return;
    per_slice_ = decimation / step_;
    const double hop_s = 0.5 * fft_size / sample_rate;
    alpha_ = 1.0 - std::exp(-hop_s / config.average_s);
    memory_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(config.noise_memory_s / hop_s)));
    warm_ = (memory_ + 1) / 2;  // nothing is read before the median holds half its frames
    max_run_ = static_cast<std::size_t>(std::lround(config.max_run_s * sample_rate / decimation));
    keep_ = static_cast<std::size_t>(std::ceil(keep_s * sample_rate / decimation)) + static_cast<std::size_t>(fft_size / decimation);
    average_.assign(static_cast<std::size_t>(fft_size), 0.0);
    inverse_window_sq_.resize(static_cast<std::size_t>(bins / 2));
    for (int m = bins / 4; m < 3 * bins / 4; ++m) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * m / bins);  // the analyzer's Hann window at input m N / B
        inverse_window_sq_[static_cast<std::size_t>(m - bins / 4)] = static_cast<float>(1.0 / (w * w));
    }
    noise_ring_.assign(static_cast<std::size_t>(subbands_), std::vector<double>(memory_, 0.0));
    noise_next_.assign(static_cast<std::size_t>(subbands_), 0);
    noise_held_.assign(static_cast<std::size_t>(subbands_), 0);
    nu_.assign(static_cast<std::size_t>(subbands_), 0.0);
    in_.resize(static_cast<std::size_t>(bins));
    out_.resize(static_cast<std::size_t>(bins));
    sorted_.resize(static_cast<std::size_t>(std::max(bins, static_cast<int>(memory_)) + subbands_));
    occupied_.resize(static_cast<std::size_t>(fft_size));
    excess_.assign(static_cast<std::size_t>(subbands_), std::vector<float>(static_cast<std::size_t>(fft_size / 2 / decimation), 0.0f));
    valid_.resize(static_cast<std::size_t>(subbands_));
}

void ImpulseBlanker::push(std::span<const Sample> bins, std::span<const float> power_fs2, std::int64_t end_input) {
    if (!active()) return;
    const auto n = static_cast<std::size_t>(n_);
    if (bins.size() != n || power_fs2.size() != n) throw std::invalid_argument("ImpulseBlanker::push: a frame of another size");
    // 1. The bins' averages, and which are occupied.
    if (!started_) {
        for (std::size_t k = 0; k < n; ++k) average_[k] = power_fs2[k];
    } else {
        for (std::size_t k = 0; k < n; ++k) average_[k] += alpha_ * (static_cast<double>(power_fs2[k]) - average_[k]);
    }
    const auto b = static_cast<std::size_t>(subband_bins_);
    const double occupied_ratio = std::pow(10.0, config_.occupied_db / 10.0);
    std::fill(occupied_.begin(), occupied_.end(), std::uint8_t{0});
    for (int s = 0; s < subbands_; ++s) {
        const std::size_t lo = static_cast<std::size_t>(first_bin_) + static_cast<std::size_t>(s) * b;
        std::copy_n(average_.begin() + static_cast<std::ptrdiff_t>(lo), b, sorted_.begin());
        const std::size_t quarter = b / 4;
        std::nth_element(sorted_.begin(), sorted_.begin() + static_cast<std::ptrdiff_t>(quarter), sorted_.begin() + static_cast<std::ptrdiff_t>(b));
        const double limit = sorted_[quarter] * occupied_ratio;
        for (std::size_t q = 0; q < b; ++q) {
            if (!(average_[lo + q] >= limit)) continue;
            const std::size_t from = lo + q >= static_cast<std::size_t>(config_.occupied_guard_bins) ? lo + q - static_cast<std::size_t>(config_.occupied_guard_bins) : 0;
            const std::size_t to = std::min(n - 1, lo + q + static_cast<std::size_t>(config_.occupied_guard_bins));
            for (std::size_t k = from; k <= to; ++k) occupied_[k] = 1;
        }
    }
    // 2 and 3. Each subband's quiet signal in time over the window's central half, its noise, and its excess by channel sample.
    const std::size_t half = n / 2;
    const std::size_t central = b / 2;
    const std::size_t slices = half / static_cast<std::size_t>(decimation_);
    std::vector<double>& work = sorted_;
    for (int s = 0; s < subbands_; ++s) {
        const std::size_t lo = static_cast<std::size_t>(first_bin_) + static_cast<std::size_t>(s) * b;
        std::size_t quiet = 0;
        for (std::size_t q = 0; q < b; ++q) {
            const std::size_t k = lo + q;  // lowest first; the FFT's index is k + N / 2 modulo N
            if (occupied_[k]) {
                in_[q] = Sample{};
            } else {
                in_[q] = bins[(k + half) % n];
                ++quiet;
            }
        }
        const auto us = static_cast<std::size_t>(s);
        valid_[us] = static_cast<double>(quiet) >= config_.min_quiet_share * static_cast<double>(b) && quiet > 0;
        nu_[us] = 0.0;
        if (!valid_[us]) continue;
        detail::fft_inverse(in_.data(), out_.data(), b);
        for (std::size_t i = 0; i < central; ++i) work[i] = static_cast<double>(std::norm(out_[b / 4 + i])) * inverse_window_sq_[i];
        // ln 2: the median of an exponential of mean 1 (derived)
        const double now = median_of(work.data(), central) / std::numbers::ln2;
        // Each subband's own ring: a frame where it was too crowded to count leaves no value there (review of text-blank-1: with one
        // ring position for all, such a frame's slot kept a stale value, or the start's 0, in its median).
        // Per quiet bin, so that a frame with more or fewer bins occupied than the frames before it reads its noise the same.
        std::vector<double>& ring = noise_ring_[us];
        ring[noise_next_[us]] = now / static_cast<double>(quiet);
        noise_next_[us] = (noise_next_[us] + 1) % memory_;
        noise_held_[us] = std::min(noise_held_[us] + 1, memory_);
        const std::size_t held = noise_held_[us];
        // The median over the last held frames, this one's included: not this frame's value alone, which a crash of 15 ms or more,
        // filling most of the central half, would set.
        std::copy_n(ring.begin(), held, work.begin());
        const double per_bin = median_of(work.data(), held);
        const double nu = per_bin * static_cast<double>(quiet);
        if (held < warm_ || !(nu > 0.0)) {
            valid_[us] = 0;  // read only once its median holds half its frames
            continue;
        }
        nu_[us] = per_bin;
        std::vector<float>& excess = excess_[static_cast<std::size_t>(s)];
        for (std::size_t j = 0; j < slices; ++j) {
            double sum = 0.0;
            for (int i = 0; i < per_slice_; ++i) {
                const std::size_t m = j * static_cast<std::size_t>(per_slice_) + static_cast<std::size_t>(i);
                sum += static_cast<double>(std::norm(out_[b / 4 + m])) * inverse_window_sq_[m];
            }
            excess[j] = static_cast<float>(sum / per_slice_ / nu - 1.0);
        }
    }
    started_ = true;
    // 4. The subbands' excess by channel sample, into the series: the central half is the input [E - 3 N / 4, E - N / 4).
    const std::int64_t first_input = end_input - static_cast<std::int64_t>(3 * n / 4);
    if (first_input < 0) return;
    const auto first_slice = static_cast<std::uint64_t>(first_input / decimation_);
    if (series_.values.empty()) series_.first = first_slice;
    if (first_slice < series_.end()) return;  // a frame already read (never, from the engine)
    series_.values.resize(static_cast<std::size_t>(first_slice - series_.first), 0.0f);  // a gap: none read there
    for (std::size_t j = 0; j < slices; ++j) {
        std::size_t count = 0;
        for (int s = 0; s < subbands_; ++s)
            if (valid_[static_cast<std::size_t>(s)]) work[count++] = excess_[static_cast<std::size_t>(s)][j];
        // Found where the subbands' lower quartile reaches the gate (three quarters of them risen at once) and their upper quartile
        // stands at most flat_ratio above it (the spectrum flat, not a click's 1 / f^2); read as their median.
        double u = 0.0;
        if (count >= static_cast<std::size_t>(config_.min_subbands)) {
            std::sort(work.begin(), work.begin() + static_cast<std::ptrdiff_t>(count));
            const double lower = work[(count - 1) / 4], upper = work[count - 1 - (count - 1) / 4];
            if (lower >= config_.gate && upper <= config_.flat_ratio * lower)
                u = count % 2 == 1 ? work[count / 2] : 0.5 * (work[count / 2 - 1] + work[count / 2]);
        }
        if (!(u >= config_.gate)) u = 0.0;
        // A run longer than max_run_s is no impulse (the noise rising for good, which the median takes in within noise_memory_s / 2):
        // the rest of it reads 0.
        if (u > 0.0) {
            if (run_ == 0) ++firings_;
            if (++run_ > max_run_) u = 0.0;
        } else {
            run_ = 0;
        }
        series_.values.push_back(static_cast<float>(u));
        ++slices_;
        if (u > 0.0) ++fired_slices_;
    }
    if (series_.values.size() > keep_ + slices) {
        const std::size_t drop = series_.values.size() - keep_;
        series_.values.erase(series_.values.begin(), series_.values.begin() + static_cast<std::ptrdiff_t>(drop));
        series_.first += drop;
    }
}

}  // namespace kz4ap
