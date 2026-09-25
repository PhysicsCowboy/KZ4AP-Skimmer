#pragma once

#include "kz4ap/types.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

namespace kz4ap {

// Reads I/Q recordings stored as 16-bit PCM stereo WAV files (left = I, right = Q).
class WavIqReader {
public:
    // Throws std::runtime_error if the file cannot be opened or is not 16-bit stereo PCM.
    explicit WavIqReader(const std::filesystem::path& path);

    int sample_rate() const { return sample_rate_; }

    // Number of I/Q samples in the file; fewer than the header claims if the file is truncated.
    std::uint64_t total_samples() const { return total_samples_; }

    // Fills out with the next samples, scaled to [-1, 1). Returns how many were read; 0 at the end.
    std::size_t read(std::span<Sample> out);

private:
    std::ifstream file_;
    int sample_rate_ = 0;
    std::uint64_t total_samples_ = 0;
    std::uint64_t samples_read_ = 0;
    std::vector<std::int16_t> raw_;
};

}  // namespace kz4ap
