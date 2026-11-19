#include "kz4ap/audio_input.hpp"

#include "fft.hpp"
#include "kaiser.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <stdexcept>

namespace kz4ap {

using detail::bessel_i0;

std::vector<Sample> analytic_filter_taps(int rate, double center_fraction, double transition_hz, double attenuation_db) {
    if (rate < 1 || !(transition_hz > 0.0 && transition_hz < rate / 4.0) || !(attenuation_db >= 50.0) ||
        !(center_fraction >= 0.0 && center_fraction < 1.0))
        throw std::invalid_argument("analytic filter: a rate of at least 1 sample/s, a transition within (0, rate / 4) Hz, an "
                                    "attenuation of at least 50 dB re the passband and a center fraction in [0, 1)");
    // Kaiser's estimates (as the resampler's): beta for the attenuation, the length for the transition.
    const double beta = 0.1102 * (attenuation_db - 8.7);
    const double length = (attenuation_db - 7.95) / (2.285 * 2.0 * std::numbers::pi * transition_hz / rate);
    const int d = static_cast<int>(std::ceil(length / 2.0));
    const double radius = d + 1.0;  // the window's half-width: 2 (D + 1) taps cover the estimate
    const double center = d + center_fraction;
    const double i0_beta = bessel_i0(beta);
    std::vector<Sample> taps(static_cast<std::size_t>(2 * d + 2));
    for (std::size_t m = 0; m < taps.size(); ++m) {
        const double t = static_cast<double>(m) - center;
        const double r = t / radius;
        const double w = bessel_i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0_beta;
        double re = 1.0, im = 0.0;
        if (t != 0.0) {
            const double pt = std::numbers::pi * t;
            re = std::sin(pt) / pt;
            im = (1.0 - std::cos(pt)) / pt;
        }
        taps[m] = Sample(static_cast<float>(w * re), static_cast<float>(w * im));
    }
    return taps;
}

std::uint64_t AudioAnalytic::outputs_for(std::uint64_t n_audio, int audio_rate) {
    const auto g = static_cast<std::uint64_t>(std::gcd(audio_rate, kAudioEngineRate));
    const std::uint64_t up = kAudioEngineRate / g, down = static_cast<std::uint64_t>(audio_rate) / g;
    return (n_audio * up + down - 1) / down;
}

AudioAnalytic::AudioAnalytic(int audio_rate) : audio_rate_(audio_rate) {
    if (audio_rate < 1) throw std::invalid_argument("receiver audio: the rate must be positive (samples/s)");
    // The resampler's delay in output samples, (n - 1) / (2 M) for n taps in all and the ratio L / M: its whole part is dropped,
    // its fraction added to the analytic filter's center so that the sum is a whole number of samples.
    std::uint64_t delay_num = 0, delay_den = 1;
    if (audio_rate != kAudioEngineRate) {
        resampler_ = std::make_unique<RationalResampler>(audio_rate, kAudioEngineRate);
        delay_num = static_cast<std::uint64_t>(resampler_->taps_per_phase()) * static_cast<std::uint64_t>(resampler_->up()) - 1;
        delay_den = 2 * static_cast<std::uint64_t>(resampler_->down());
    }
    const std::uint64_t whole = (delay_num + delay_den - 1) / delay_den;  // ceil
    const double fraction = static_cast<double>(whole) - static_cast<double>(delay_num) / static_cast<double>(delay_den);
    taps_ = analytic_filter_taps(kAudioEngineRate, fraction);
    const int d = static_cast<int>(taps_.size() / 2) - 1;
    center_ = d + fraction;
    skip_ = static_cast<std::uint64_t>(d) + whole;
    fft_size_ = 1024;
    while (fft_size_ < 3 * static_cast<int>(taps_.size())) fft_size_ *= 2;
    const auto n = static_cast<std::size_t>(fft_size_);
    block_ = n - taps_.size() + 1;
    std::vector<Sample> padded(n, Sample(0.0f, 0.0f));
    std::copy(taps_.begin(), taps_.end(), padded.begin());
    response_.resize(n);
    detail::fft_forward(padded.data(), response_.data(), n);
    for (Sample& h : response_) h /= static_cast<float>(n);
    buffer_.resize(n);
    spectrum_.resize(n);
    result_.resize(n);
    pending_.assign(taps_.size() - 1, Sample(0.0f, 0.0f));
}

AudioAnalytic::~AudioAnalytic() = default;

void AudioAnalytic::push(std::span<const float> audio, std::vector<Sample>& out) {
    complex_in_.resize(audio.size());
    for (std::size_t i = 0; i < audio.size(); ++i) complex_in_[i] = Sample(audio[i], 0.0f);
    if (resampler_) {
        resampled_.clear();
        resampler_->push(complex_in_, resampled_);
        pending_.insert(pending_.end(), resampled_.begin(), resampled_.end());
    } else {
        pending_.insert(pending_.end(), complex_in_.begin(), complex_in_.end());
    }
    while (pending_.size() >= static_cast<std::size_t>(fft_size_)) run_block(out);
}

void AudioAnalytic::run_block(std::vector<Sample>& out) {
    // Overlap-save: the last taps - 1 samples and block_ new ones; outputs taps - 1 on are the linear convolution's.
    const auto n = static_cast<std::size_t>(fft_size_);
    std::copy_n(pending_.begin(), n, buffer_.begin());
    detail::fft_forward(buffer_.data(), spectrum_.data(), n);
    for (std::size_t k = 0; k < n; ++k) spectrum_[k] *= response_[k];
    detail::fft_inverse(spectrum_.data(), result_.data(), n);
    for (std::size_t i = taps_.size() - 1; i < n; ++i, ++produced_)
        if (produced_ >= skip_) out.push_back(result_[i]);
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(block_));
}

}  // namespace kz4ap
