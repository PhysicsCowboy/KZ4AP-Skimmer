// A recording as the engine takes it: WavIqReader's samples, mirrored if asked, and resampled to a rate the decoders take when
// the file's is not one (HDSDR's 250 kHz becomes 192 kHz, exactly by 96/125). docs/signal-processing.md, section 1 and A.1.
#pragma once

#include "kz4ap/resampler.hpp"
#include "kz4ap/wav_reader.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kz4ap {

// The rate the engine runs a file of file_rate at: file_rate itself if it is 48 000 x 2^k samples/s (k = 0 to 4, the rates whose
// channel stream holds a whole number of the decoder's samples) or below 48 000; otherwise the largest of those below it.
int decoder_rate_for(int file_rate);

struct RecordingOptions {
    // Conjugate every sample (Q to -Q): for a receiver whose spectrum is mirrored, as an SDR on a transceiver's IF output is. A
    // signal the file has at offset +f is then at -f, so its true frequency is the center minus the file's offset.
    bool mirror = false;
    // Resample to decoder_rate_for(file rate); false keeps the file's rate whatever it is.
    bool resample = true;
};

class RecordingReader {
public:
    // Throws std::runtime_error as WavIqReader does, std::invalid_argument if the rate cannot be resampled.
    explicit RecordingReader(const std::filesystem::path& path, RecordingOptions options = {});

    // The rate the samples come out at, samples/s.
    int sample_rate() const { return rate_; }
    int file_rate() const { return reader_.sample_rate(); }
    bool resampled() const { return resampler_ != nullptr; }
    bool mirrored() const { return options_.mirror; }
    const RationalResampler* resampler() const { return resampler_.get(); }
    WavSampleFormat sample_format() const { return reader_.sample_format(); }
    const WavRecordingInfo& info() const { return reader_.info(); }
    // Output samples in the whole recording.
    std::uint64_t total_samples() const { return total_; }
    // As WavIqReader::read, at sample_rate().
    std::size_t read(std::span<Sample> out);
    // The next read starts at this output sample, clipped to the end.
    void seek(std::uint64_t sample);

private:
    WavIqReader reader_;
    RecordingOptions options_;
    int rate_ = 0;
    std::unique_ptr<RationalResampler> resampler_;
    std::uint64_t total_ = 0;
    std::uint64_t position_ = 0;  // output samples delivered or skipped
    std::vector<Sample> input_, ready_;
    std::size_t ready_at_ = 0;
};

}  // namespace kz4ap
