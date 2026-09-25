#pragma once

#include "kz4ap/types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace kz4ap {

struct SpectrumFrame {
    double time_s = 0;            // time just after the frame's last input sample
    std::vector<float> power_db;  // fft_size bins, lowest frequency first:
                                  // bin i is at (i - fft_size/2) * sample_rate / fft_size Hz
};

// Hann-windowed power spectrum over a sliding window. A tone of amplitude A
// reads 20*log10(A) dB in its bin.
class SpectrumAnalyzer {
public:
    SpectrumAnalyzer(int sample_rate, int fft_size, int hop);

    // Consumes samples and returns every frame they complete.
    std::vector<SpectrumFrame> push(std::span<const Sample> in);

    double bin_to_hz(int index) const;

private:
    int sample_rate_;
    int fft_size_;
    int hop_;
    std::vector<float> window_;
    float power_scale_ = 1;
    std::vector<Sample> buffer_;
    std::vector<Sample> fft_in_;
    std::vector<Sample> fft_out_;
    std::uint64_t dropped_ = 0;  // samples already removed from the front of buffer_
};

}  // namespace kz4ap
