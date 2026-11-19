// A recording as the engine takes it: WavIqReader's samples, mirrored if asked, and resampled to a rate the decoders take when
// the file's is not one (HDSDR's 250 kHz becomes 192 kHz, exactly by 96/125); or receiver audio, one channel of a WAV file made
// into the analytic signal at 48 kHz (audio_input.hpp). docs/signal-processing.md, section 1 and A.1.
#pragma once

#include "kz4ap/audio_input.hpp"
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

// What a recording holds.
enum class RecordingInput {
    Auto,   // a one-channel file is receiver audio, any other I/Q (a stereo file is taken as I/Q: say Audio for stereo audio)
    Iq,     // two channels, I left and Q right
    Audio,  // receiver audio: real samples, channel RecordingOptions::audio_channel
};

struct RecordingOptions {
    // Conjugate every sample (Q to -Q): for a receiver whose spectrum is mirrored, as an SDR on a transceiver's IF output is. A
    // signal the file has at offset +f is then at -f, so its true frequency is the center minus the file's offset. Receiver audio:
    // its analytic signal conjugated, a tone at f Hz read at -f Hz, for a receiver on the lower sideband, whose RF falls as the
    // audio frequency rises (docs/signal-processing.md, section 1).
    bool mirror = false;
    // Resample to decoder_rate_for(file rate); false keeps the file's rate whatever it is. I/Q only: receiver audio always comes
    // out at kAudioEngineRate.
    bool resample = true;
    RecordingInput input = RecordingInput::Auto;
    int audio_channel = 0;  // receiver audio: the channel read, 0 the first (left)
};

class RecordingReader {
public:
    // Throws std::runtime_error as WavIqReader or WavAudioReader does, std::invalid_argument if the rate cannot be resampled.
    explicit RecordingReader(const std::filesystem::path& path, RecordingOptions options = {});
    ~RecordingReader();

    // The rate the samples come out at, samples/s (complex).
    int sample_rate() const { return rate_; }
    int file_rate() const;
    // Receiver audio (RecordingInput::Audio, or a one-channel file under Auto): the samples are its analytic signal.
    bool is_audio() const { return audio_.has_value(); }
    int audio_channel() const { return audio_ ? audio_->channel() : 0; }
    int file_channels() const { return audio_ ? audio_->channels() : 2; }
    const AudioAnalytic* analytic() const { return analytic_.get(); }
    bool resampled() const { return resampler() != nullptr; }
    bool mirrored() const { return options_.mirror; }
    // I/Q: its resampler; receiver audio: the analytic step's, before its filter.
    const RationalResampler* resampler() const { return analytic_ ? analytic_->resampler() : resampler_.get(); }
    WavSampleFormat sample_format() const;
    const WavRecordingInfo& info() const;
    // Output samples in the whole recording.
    std::uint64_t total_samples() const { return total_; }
    // As WavIqReader::read, at sample_rate().
    std::size_t read(std::span<Sample> out);
    // The next read starts at this output sample, clipped to the end. Receiver audio: the same samples as reading through, to
    // float rounding (its analytic step starts over a little before, from a whole number of the resampler's input periods).
    void seek(std::uint64_t sample);

private:
    std::size_t read_audio(std::span<Sample> out);
    void start_audio_at(std::uint64_t sample);

    std::optional<WavIqReader> iq_;
    std::optional<WavAudioReader> audio_;
    RecordingOptions options_;
    int rate_ = 0;
    std::unique_ptr<RationalResampler> resampler_;
    std::unique_ptr<AudioAnalytic> analytic_;
    std::uint64_t total_ = 0;
    std::uint64_t position_ = 0;  // output samples delivered or skipped
    std::vector<Sample> input_, ready_;
    std::size_t ready_at_ = 0;
    std::uint64_t ready_index_ = 0;  // receiver audio: the output index of ready_[0]
    std::vector<float> audio_in_;
};

}  // namespace kz4ap
