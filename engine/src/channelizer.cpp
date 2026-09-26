#include "kz4ap/channelizer.hpp"

#include "fft.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace kz4ap {

Channelizer::Channelizer(const ChannelizerConfig& config) : config_(config) {
    const int n = config.fft_size;
    const int m = config.channel_bins;
    if (n <= 0 || n % 2 != 0 || m <= 0 || m % 2 != 0 || n % m != 0 || config.sample_rate <= 0)
        throw std::invalid_argument("invalid channelizer parameters");
    hop_ = n / 2;
    decimation_ = n / m;
    // Windowed-sinc low-pass, as long as overlap-save allows (fft_size - hop + 1 taps).
    const int taps = n - hop_ + 1;

    // The channel keeps only the channel_bins bins around its center, i.e. frequencies
    // within half the output rate. The filter's whole transition band must fit inside
    // that, or part of the response is cut off (the cutoff is the -6 dB point, not the
    // stopband edge). A Blackman window's transition width is about 5.5 / (filter length
    // in seconds): 258 Hz for 4097 taps at 192 kHz.
    const double half_output_rate = static_cast<double>(config.sample_rate) / decimation_ / 2;
    const double transition_hz = 5.5 * config.sample_rate / taps;
    if (!std::isfinite(config.cutoff_hz) || config.cutoff_hz <= 0 ||
        config.cutoff_hz + transition_hz / 2 > half_output_rate)
        throw std::invalid_argument(
            "channelizer cutoff plus half the filter's transition width must not exceed half the output rate");

    const double fc = config.cutoff_hz / config.sample_rate;
    const double mid = (taps - 1) / 2.0;
    std::vector<double> h(static_cast<std::size_t>(taps));
    double sum = 0;
    for (int i = 0; i < taps; ++i) {
        const double x = i - mid;
        const double sinc = x == 0 ? 2 * fc : std::sin(2 * std::numbers::pi * fc * x) / (std::numbers::pi * x);
        const double w = 0.42 - 0.5 * std::cos(2 * std::numbers::pi * i / (taps - 1)) +
                         0.08 * std::cos(4 * std::numbers::pi * i / (taps - 1));  // Blackman
        h[i] = sinc * w;
        sum += h[i];
    }
    std::vector<Sample> padded(static_cast<std::size_t>(n));
    for (int i = 0; i < taps; ++i) padded[i] = Sample(static_cast<float>(h[i] / sum), 0.0f);
    std::vector<Sample> response(static_cast<std::size_t>(n));
    detail::fft_forward(padded.data(), response.data(), static_cast<std::size_t>(n));
    filter_.resize(static_cast<std::size_t>(m));
    for (int k = -m / 2; k < m / 2; ++k) filter_[(k + m) % m] = response[(k + n) % n];

    history_.assign(static_cast<std::size_t>(n), Sample{});
    spectrum_.resize(static_cast<std::size_t>(n));
    channel_in_.resize(static_cast<std::size_t>(m));
    channel_out_.resize(static_cast<std::size_t>(m));
    pending_.reserve(static_cast<std::size_t>(hop_));
}

double Channelizer::output_rate() const {
    return static_cast<double>(config_.sample_rate) / decimation_;
}

int Channelizer::hz_to_bin(double hz) const {
    const auto bin = static_cast<int>(std::lround(hz * config_.fft_size / config_.sample_rate));
    return std::clamp(bin, -config_.fft_size / 2, config_.fft_size / 2 - 1);
}

double Channelizer::bin_to_hz(int bin) const {
    return static_cast<double>(bin) * config_.sample_rate / config_.fft_size;
}

void Channelizer::add_channel(std::uint32_t id, int center_bin) { channels_[id] = center_bin; }

void Channelizer::remove_channel(std::uint32_t id) { channels_.erase(id); }

void Channelizer::push(std::span<const Sample> in, const Sink& sink) {
    for (const Sample& s : in) {
        pending_.push_back(s);
        if (static_cast<int>(pending_.size()) == hop_) process_block(sink);
    }
}

void Channelizer::process_block(const Sink& sink) {
    const int n = config_.fft_size;
    const int m = config_.channel_bins;
    std::move(history_.begin() + hop_, history_.end(), history_.begin());
    std::copy(pending_.begin(), pending_.end(), history_.end() - hop_);
    pending_.clear();
    detail::fft_forward(history_.data(), spectrum_.data(), static_cast<std::size_t>(n));

    // Block b holds input samples [b*hop - (n - hop), b*hop + hop). Only the last
    // hop/decimation outputs are free of circular wrap-around; they start at input b*hop.
    const std::int64_t block_start = static_cast<std::int64_t>(block_index_) * hop_ - (n - hop_);
    const int keep = hop_ / decimation_;
    const std::uint64_t first_index = block_index_ * static_cast<std::uint64_t>(keep);

    for (const auto& [id, center] : channels_) {
        for (int k = -m / 2; k < m / 2; ++k) {
            const int src = ((center + k) % n + n) % n;
            channel_in_[(k + m) % m] = spectrum_[src] * filter_[(k + m) % m];
        }
        detail::fft_inverse(channel_in_.data(), channel_out_.data(), static_cast<std::size_t>(m));
        // The inverse FFT mixes relative to this block's start; re-reference the mixer
        // to the start of the stream so phase is continuous from block to block.
        const std::int64_t turns = ((static_cast<std::int64_t>(center) * block_start) % n + n) % n;
        const double angle = -2 * std::numbers::pi * static_cast<double>(turns) / n;
        const Sample rotate = Sample(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))) /
                              static_cast<float>(n);
        for (int i = m - keep; i < m; ++i) channel_out_[i] *= rotate;
        sink(id, first_index, std::span<const Sample>(channel_out_.data() + (m - keep), static_cast<std::size_t>(keep)));
    }
    ++block_index_;
}

}  // namespace kz4ap
