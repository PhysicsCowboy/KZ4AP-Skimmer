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
    // Channel low-pass cutoff (-6 dB relative to the passband), Hz. Must be > 0, and cutoff_hz plus half the
    // filter's transition width (about 5.5 * sample_rate / (fft_size / 2 + 1) Hz) must
    // not exceed output rate / 2: at most about 621 Hz with the other defaults.
    double cutoff_hz = 150.0;
};

// The taps of the channel low-pass filter, those the Channelizer builds its filter from, before their rounding to
// float: a Blackman-windowed sinc of fft_size / 2 + 1 taps with its point 6 dB below the passband at cutoff_hz,
// normalized to a sum of 1 (unit gain at 0 Hz). Throws std::invalid_argument for the configurations the Channelizer
// refuses.
std::vector<double> channel_filter_taps(const ChannelizerConfig& config);

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
    // Moves a channel: its output from its next block on is cut at center_bin (a signed bin, as add_channel's); the
    // blocks already delivered are unchanged. Called from inside the sink it also takes effect from the channel's next
    // block: moves are applied when a block starts, before any channel is cut. Throws std::invalid_argument for an id
    // that is not a channel.
    // The phase: channel sample j (absolute index, input sample n = j x decimation) is
    //     y_c[j] = sum_l h[l] x[n - l] exp(-j 2 pi c (n - l) / fft_size),
    // the input demodulated at center c from the stream's start, then filtered (derived from process_block: the inverse
    // FFT mixes relative to the block's start, re-referenced to the stream's start). For a station narrow against the
    // filter's passband this is the input as of n - D demodulated by exp(-j 2 pi c (n - D) / fft_size), D = (taps - 1) / 2
    // = fft_size / 4 input samples, the filter's group delay (1 / (4 x bin width) s: 10.667 ms). So at a move from c1 to
    // c2 the station's phase in the channel steps by 2 pi (c1 - c2) (n - D) / fft_size; ChannelFollower compensates it.
    void set_center_bin(std::uint32_t id, int center_bin);
    std::size_t channel_count() const { return channels_.size(); }

    // Consumes samples; calls sink once per channel for every block they complete.
    void push(std::span<const Sample> in, const Sink& sink);

    // The same work in two steps, for a caller that keeps its own channels and cuts them on several threads:
    // begin_block takes exactly one hop (fft_size / 2 samples) and does the block's shared FFT; cut then gives one
    // channel's block, and may be called concurrently, with each caller's own in and out, until the next begin_block.
    // in and out: channel_bins() samples of work space each; the block returned lies inside out.
    struct Block {
        std::uint64_t first_index;  // the block's first output sample, counted from the start of the stream
        std::int64_t first_input;   // the input sample at the start of the block's FFT (negative at the stream's start)
    };
    Block begin_block(std::span<const Sample> hop);
    std::span<Sample> cut(const Block& block, int center_bin, std::span<Sample> in, std::span<Sample> out) const;
    int channel_bins() const { return config_.channel_bins; }

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
    std::map<std::uint32_t, int> moves_;     // id -> center bin from the next block on (set_center_bin)
};

}  // namespace kz4ap
