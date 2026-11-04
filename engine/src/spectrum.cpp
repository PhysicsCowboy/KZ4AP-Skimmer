#include "kz4ap/spectrum.hpp"

#include "fft.hpp"
#include "kz4ap/segmental/fast_math.hpp"

#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>

namespace {

// Validates the analyzer's parameters and returns fft_size as a size_t.
// Runs before any vector is sized, so an invalid fft_size (e.g. negative)
// throws std::invalid_argument instead of being reinterpreted as a huge
// unsigned allocation size.
std::size_t ValidatedFftSize(int sample_rate, int fft_size, int hop) {
    if (sample_rate <= 0 || fft_size <= 0 || hop <= 0 || hop > fft_size)
        throw std::invalid_argument("invalid spectrum analyzer parameters");
    return static_cast<std::size_t>(fft_size);
}

}  // namespace

namespace kz4ap {

SpectrumAnalyzer::SpectrumAnalyzer(int sample_rate, int fft_size, int hop, bool decibels)
    : sample_rate_(sample_rate), fft_size_(fft_size), hop_(hop), decibels_(decibels),
      window_(ValidatedFftSize(sample_rate, fft_size, hop)),
      fft_in_(static_cast<std::size_t>(fft_size)),
      fft_out_(static_cast<std::size_t>(fft_size)) {
    double sum = 0;
    for (int n = 0; n < fft_size; ++n) {
        window_[static_cast<std::size_t>(n)] =
            static_cast<float>(0.5 - 0.5 * std::cos(2 * std::numbers::pi * n / fft_size));
        sum += window_[static_cast<std::size_t>(n)];
    }
    power_scale_ = static_cast<float>(1.0 / (sum * sum));
}

std::vector<SpectrumFrame> SpectrumAnalyzer::push(std::span<const Sample> in) {
    buffer_.insert(buffer_.end(), in.begin(), in.end());
    std::vector<SpectrumFrame> frames;
    const auto n = static_cast<std::size_t>(fft_size_);
    const std::size_t half = n / 2;
    std::size_t start = 0;
    while (buffer_.size() - start >= n) {
        for (std::size_t i = 0; i < n; ++i) fft_in_[i] = buffer_[start + i] * window_[i];
        detail::fft_forward(fft_in_.data(), fft_out_.data(), n);
        SpectrumFrame frame;
        frame.time_s = static_cast<double>(dropped_ + start + n) / sample_rate_;
        if (decibels_) frame.power_db.resize(n);
        frame.power_fs2.resize(n);
        // The upper half of the FFT first: lowest frequency first. Power is at least 1e-20 FS^2, a normal float; the logarithm per bin is the analyzer's largest cost.
        const auto fill = [&](std::size_t first, std::size_t count, const std::complex<float>* bins) {
            constexpr double kDbPerNeper = 4.3429448190325182;
            for (std::size_t i = 0; i < count; ++i) {
                const float power = std::norm(bins[i]) * power_scale_ + 1e-20f;
                frame.power_fs2[first + i] = power;
                if (decibels_) frame.power_db[first + i] = static_cast<float>(kDbPerNeper * segmental::fast::log(power));
            }
        };
        fill(0, n - half, fft_out_.data() + half);
        fill(n - half, half, fft_out_.data());
        frames.push_back(std::move(frame));
        start += static_cast<std::size_t>(hop_);
    }
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(start));
    dropped_ += start;
    return frames;
}

double SpectrumAnalyzer::bin_to_hz(int index) const {
    return static_cast<double>(index - fft_size_ / 2) * sample_rate_ / fft_size_;
}

}  // namespace kz4ap
