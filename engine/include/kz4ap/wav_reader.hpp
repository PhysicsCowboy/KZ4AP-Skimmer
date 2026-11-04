#pragma once

#include "kz4ap/types.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kz4ap {

// A path as UTF-8, for a message or a display: path.string() throws on Windows for a name outside the system's code page.
inline std::string path_text(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

// How a recording's samples are stored. Integers are scaled by 2^-(bits - 1), so full scale is 1 FS whatever the width.
enum class WavSampleFormat { Int16, Int24, Int32, Float32 };

// What a recording's header says beyond its samples: SpectraVue's and HDSDR's `auxi` chunk, which CWSL_File and other
// skimmer recorders write too. Each field is absent when the header has none or holds 0 there.
struct WavRecordingInfo {
    std::optional<double> center_hz;            // the radio's frequency at 0 Hz in the samples, Hz
    std::optional<std::int64_t> start_unix_ms;  // the first sample's time, ms since 1970-01-01 00:00 UTC
    std::optional<std::int64_t> stop_unix_ms;   // the writer's stop time, same clock
};

// The radio's center frequency as a recording's file name gives it, Hz, or none. Two forms: the span's edges,
// `<low>_<high>kHz` (as `7000_7192kHz`), taken when the span is between 0.8 and 1 times the sample rate; otherwise the
// first word that is a frequency with a unit, `<f>Hz`, `<f>kHz` or `<f>MHz` (as `14091kHz`, `7030.000kHz`,
// `7086000Hz`), that is not the sample rate itself. Words are separated by `_`, `-` or spaces, after an extension of one
// to four letters is dropped. sample_rate: complex samples/s, > 0.
std::optional<double> center_hz_from_file_name(std::string_view file_name, int sample_rate);

// Reads I/Q recordings stored as stereo WAV files (left = I, right = Q): integer PCM of 16, 24 or 32 bits, or 32-bit
// IEEE float, plain or WAVE_FORMAT_EXTENSIBLE.
class WavIqReader {
public:
    // Throws std::runtime_error if the file cannot be opened or is not one of the forms above.
    explicit WavIqReader(const std::filesystem::path& path);

    int sample_rate() const { return sample_rate_; }
    WavSampleFormat sample_format() const { return format_; }
    const WavRecordingInfo& info() const { return info_; }

    // Number of I/Q samples in the file; fewer than the header claims if the file is truncated.
    std::uint64_t total_samples() const { return total_samples_; }

    // Fills out with the next samples, in FS: integers scaled to [-1, 1), floats as stored. Returns how many were read;
    // 0 at the end.
    std::size_t read(std::span<Sample> out);

    // The next read starts at this sample, clipped to the end. Throws std::runtime_error if the file cannot seek.
    void seek(std::uint64_t sample);

private:
    std::ifstream file_;
    std::streamoff data_start_ = 0;  // the data chunk's first byte
    int sample_rate_ = 0;
    WavSampleFormat format_ = WavSampleFormat::Int16;
    int frame_bytes_ = 4;  // bytes of one I/Q sample: both channels
    WavRecordingInfo info_;
    std::uint64_t total_samples_ = 0;
    std::uint64_t samples_read_ = 0;
    std::vector<unsigned char> raw_;
};

}  // namespace kz4ap
