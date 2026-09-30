#pragma once

#include "kz4ap/channelizer.hpp"
#include "kz4ap/classical_decoder.hpp"
#include "kz4ap/event_bus.hpp"
#include "kz4ap/signal_detector.hpp"
#include "kz4ap/spectrum.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace kz4ap {

// The FFT size the engine uses when EngineConfig::fft_size is 0: the largest power
// of two with sample_rate / fft_size >= 20 Hz, so bins stay about 23 Hz wide at
// 48, 96, 192 or 768 kHz. Throws std::invalid_argument for sample rates below 8000 Hz.
int choose_fft_size(int sample_rate);

// Benchmark tooling (kz4ap-bench --record-channels): one channel block as the channelizer delivered it,
// before the channel's decoder (and so before its frequency tracker) sees it. Observation only.
struct ChannelBlock {
    std::uint32_t track_id;
    std::uint64_t first_index;        // index of samples[0] in the channelizer's output stream, from its start
    double center_hz;                 // the channel's center (its FFT bin), Hz from the span's center
    std::span<const Sample> samples;  // channelizer output at Engine::channel_rate(), FS
};
using ChannelTap = std::function<void(const ChannelBlock&)>;

struct EngineConfig {
    int sample_rate = 192000;
    int fft_size = 0;  // 0 = choose automatically: the largest power of two with sample_rate / fft_size >= 20 Hz
    int channel_bins = 64;
    double channel_cutoff_hz = 150.0;
    DetectorConfig detector;  // sample_rate, fft_size, hop and attribution_distance_hz (from
                              // channel_distance_hz) are overwritten by the engine
    ClassicalDecoderConfig decoder;

    // Oracle mode, for the benchmark: when not empty, a channel is opened at each of
    // these frequencies (Hz from the span's center, rounded to the nearest FFT bin)
    // from the first sample, with track ids 1, 2, ... in this order, and the signal
    // detector is bypassed: no other track is born and none dies.
    std::vector<double> oracle_frequencies_hz;

    // D_ch, Hz (owner decisions 2026-09-29, option 1; Matched path, Attribution::Distance): each detector
    // track follows its own spectral peak within D_ch of its current frequency, and a new peak within
    // D_ch of a track belongs to it; a channel's
    // frequency tracker fine-tunes around the detector's frequency (FrequencyTrackerConfig::fine_tune_hz).
    double channel_distance_hz = 47.0;
};

// The milestone-1 pipeline (kept selectable; owner, 2026-09-29): the Envelope decoder and the
// milestone-1 detector rules (frequency fixed at birth, bin attribution). CI (bench/smoke.sh) bounds its
// smoke CER at 0.09 and checks that two runs agree; it does not pin bit-identity with milestone 1. That
// rests on the generator's frozen-copy tests (training/tests/test_generate.py), reading this path, and
// its smoke CER, 34 edits in 964 symbols, unchanged since Task 2 (docs/signal-processing.md, section 11,
// "Smoke check").
inline EngineConfig with_envelope_path(EngineConfig config) {
    config.decoder.front_end = FrontEnd::Envelope;
    config.detector.attribution = Attribution::Bins;
    return config;
}

struct EngineStats {
    std::uint64_t channel_samples = 0;  // channel samples delivered to decoders, summed over channels
    double channel_seconds = 0;         // the same, in s of channel output
    double decoder_seconds = 0;         // steady-clock time spent inside decoders, s
};

// Runs the decoding pipeline synchronously on the caller's thread. Output is
// identical however the input is split across calls to process().
class Engine {
public:
    // Throws std::invalid_argument if config.sample_rate is below 8000 Hz or
    // config.channel_distance_hz is not positive.
    Engine(const EngineConfig& config, EventBus& bus);
    ~Engine();

    void process(std::span<const Sample> samples);

    // End of input: processes buffered samples and flushes partly decoded characters.
    void finish();

    EngineStats stats() const;

    // Benchmark tooling: called with every channel block before the channel's decoder sees it. It
    // observes only; decoding is the same with or without it. Set it before process().
    void set_channel_tap(ChannelTap tap) { tap_ = std::move(tap); }
    double channel_rate() const;  // the channelizer's output rate, samples/s

private:
    struct Channel {
        Track track;
        int bin;  // the channel's center bin
        std::unique_ptr<Decoder> decoder;
        double detector_freq_hz = 0;  // the detector's current frequency for this track, Hz from the span's center
    };

    void process_hop(std::span<const Sample> hop);
    void open_channel(const Track& track, int bin);
    void close_channel(std::uint32_t id);
    void publish_update(std::uint32_t id, DecodeUpdate&& update);

    EngineConfig config_;  // fft_size resolved to the size in use
    EventBus& bus_;
    int hop_;
    SpectrumAnalyzer spectrum_;
    SignalDetector detector_;
    Channelizer channelizer_;
    std::vector<Sample> pending_;
    std::map<std::uint32_t, Channel> channels_;
    bool oracle_ = false;
    std::uint64_t channel_samples_ = 0;
    double decoder_seconds_ = 0;
    ChannelTap tap_;
};

}  // namespace kz4ap
