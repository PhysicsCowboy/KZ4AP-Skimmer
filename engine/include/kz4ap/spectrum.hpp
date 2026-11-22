#pragma once

#include "kz4ap/types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace kz4ap {

struct SpectrumFrame {
    double time_s = 0;            // time just after the frame's last input sample
    std::vector<float> power_db;  // fft_size bins, lowest frequency first:
                                  // bin i is at (i - fft_size/2) * sample_rate / fft_size Hz; dB re 1 FS^2
                                  // (empty from an analyzer made without decibels)
    // The same power in FS^2, 10^(power_db / 10) but for the rounding of the dB value; SpectrumAnalyzer fills it so that
    // whoever needs linear power need not undo the logarithm per bin. May be empty in a frame made elsewhere (a reader
    // then converts).
    std::vector<float> power_fs2;
};

// Hann-windowed power spectrum over a sliding window. A tone of amplitude A
// reads 20*log10(A) dB in its bin.
class SpectrumAnalyzer {
public:
    // decibels false: the frames carry power_fs2 only, for a reader that needs no dB per bin (the logarithm is most of the
    // per-bin cost).
    SpectrumAnalyzer(int sample_rate, int fft_size, int hop, bool decibels = true);

    // Consumes samples and returns every frame they complete.
    std::vector<SpectrumFrame> push(std::span<const Sample> in);

    double bin_to_hz(int index) const;
    // The last frame push computed, as the FFT gives it (bin 0 at 0 Hz, unnormalized; its power is |X|^2 over (sum of the
    // window)^2): the impulse blanker's input (impulse_blanker.hpp). Valid until the next push; empty before the first frame.
    std::span<const Sample> last_bins() const { return have_bins_ ? std::span<const Sample>(fft_out_) : std::span<const Sample>(); }

private:
    bool have_bins_ = false;
    int sample_rate_;
    int fft_size_;
    int hop_;
    bool decibels_;
    std::vector<float> window_;
    float power_scale_ = 1;
    std::vector<Sample> buffer_;
    std::vector<Sample> fft_in_;
    std::vector<Sample> fft_out_;
    std::uint64_t dropped_ = 0;  // samples already removed from the front of buffer_
};

// The first and last bin of a frame (bin i at (i - fft_size / 2) x sample_rate / fft_size Hz) whose centers lie within
// [lo_hz, hi_hz], Hz from the span's center: a receiver's passband (EngineConfig::passband_lo_hz). The whole frame,
// 0 to fft_size - 1, when hi_hz <= lo_hz; one bin, the nearest, when the band holds no bin's center.
struct BinRange {
    int first = 0;
    int last = 0;
};
BinRange band_bins(double lo_hz, double hi_hz, int sample_rate, int fft_size);

}  // namespace kz4ap
