#pragma once

#include "kz4ap/types.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <vector>

namespace kz4ap {

struct ChannelizerConfig {
    int sample_rate = 192000;
    int fft_size = 8192;       // input FFT size; the hop is fft_size / 2
    int channel_bins = 64;     // bins kept per channel; output rate = sample_rate * channel_bins / fft_size
    double cutoff_hz = 150.0;  // channel low-pass cutoff (-6 dB point)
};

// Splits a wideband I/Q stream into narrow, decimated baseband streams, one per
// channel, by fast convolution: one shared FFT per block, then a small inverse
// FFT per channel over the bins around its center.
class Channelizer {
public:
    // first_index counts output samples from the start of the stream.
    using Sink = std::function<void(std::uint32_t channel_id, std::uint64_t first_index,
                                    std::span<const Sample> samples)>;

    explicit Channelizer(const ChannelizerConfig& config);

    double output_rate() const;
    int hz_to_bin(double hz) const;  // nearest bin, clamped to [-fft_size/2, fft_size/2)
    double bin_to_hz(int bin) const;

    void add_channel(std::uint32_t id, int center_bin);
    void remove_channel(std::uint32_t id);
    std::size_t channel_count() const { return channels_.size(); }

    // Consumes samples; calls sink once per channel for every block they complete.
    void push(std::span<const Sample> in, const Sink& sink);

private:
    void process_block(const Sink& sink);

    ChannelizerConfig config_;
    int hop_;
    int decimation_;
    std::vector<Sample> filter_;  // channel_bins frequency-response values, indexed by bin offset mod channel_bins
    std::vector<Sample> history_;
    std::vector<Sample> pending_;
    std::vector<Sample> spectrum_;
    std::vector<Sample> channel_in_;
    std::vector<Sample> channel_out_;
    std::uint64_t block_index_ = 0;
    std::map<std::uint32_t, int> channels_;  // id -> center bin; ordered for deterministic sink order
};

}  // namespace kz4ap
