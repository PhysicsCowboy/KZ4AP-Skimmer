// Whether a track's keying is a stronger neighbor's (docs/signal-processing.md, section 6, "Copies of a neighbor"). A channel 150 Hz
// wide hears a strong station tens of Hz away through its filter's skirt, and when the channel's own station is silent, or there never
// was one, its decoder prints the neighbor's text: a second row of one station. Its envelope then follows the neighbor's keying,
// element by element; an independent station's does not.
//
// The test: e_w and e_s, the two channels' envelope power, one value per channel block (Decoder::block_envelope_fs2), over the same
// last n blocks; each less its centered moving mean over m blocks (a high-pass: a pile-up's callers start and stop together, which
// says nothing of their elements); then Pearson's correlation of the two,
//     rho = sum(x_w x_s) / sqrt(sum(x_w^2) sum(x_s^2)).
// For a copy, e_w = g k(t) + noise, k the neighbor's key: with the key down a fraction d of the time and a copy of key-down SNR S at
// the weak channel's filter (S = g / sigma^2, power over the noise's), rho = S sqrt(d (1 - d)) / sqrt(S^2 d (1 - d) + (1 + 2 S d) / q)
// (derived, for envelope power with q independent noise values a block, the neighbor's envelope taken as its key), S / (S + 2) at
// d = 1/2 and q = 1. For two independent keys rho tends to 0, within about 1 / sqrt(N), N the number of independent values in the
// window (derived). The thresholds are in EngineConfig (copy_*).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kz4ap {

// rho as above, and the gain of the strong channel's keying in the weak one's, beta = sum(x_w x_s) / sum(x_s^2): a copy heard through
// the weak channel's skirt is the source's envelope times |H(df)|^2 <= 1, the filters being alike (derived), while a strong channel
// that hears a weak neighbor while its own station is silent reads that neighbor's keying, so that beta stands above 1.
struct KeyingCoherence {
    double rho;   // NaN if either series is constant after the high-pass, or shorter than 2
    double gain;  // beta; NaN likewise
    double weak_mean = 0.0;    // the plain means of the two series, FS^2
    double strong_mean = 0.0;
};

// Over two series of the same length; detrend_blocks m (at least 1; 1 subtracts the plain mean).
KeyingCoherence keying_coherence(std::span<const float> weak, std::span<const float> strong, std::size_t detrend_blocks);
inline double keying_correlation(std::span<const float> weak, std::span<const float> strong, std::size_t detrend_blocks) {
    return keying_coherence(weak, strong, detrend_blocks).rho;
}

// A channel's last values, a ring of `capacity` blocks: push one per block, in order; a block missed (a gap in the block numbers)
// starts the history again.
class EnvelopeHistory {
public:
    explicit EnvelopeHistory(std::size_t capacity = 0);
    void push(std::uint64_t block, float value);
    // The last `n` values, oldest first, into out (resized); n at most size().
    void last(std::size_t n, std::vector<float>& out) const;
    std::size_t size() const { return size_; }
    // The block of the newest value; meaningful only when size() > 0.
    std::uint64_t newest_block() const { return newest_; }

private:
    std::vector<float> ring_;
    std::size_t next_ = 0;
    std::size_t size_ = 0;
    std::uint64_t newest_ = 0;
};

}  // namespace kz4ap
