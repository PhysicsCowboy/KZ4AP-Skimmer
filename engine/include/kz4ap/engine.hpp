#pragma once

#include "kz4ap/channelizer.hpp"
#include "kz4ap/classical_decoder.hpp"
#include "kz4ap/event_bus.hpp"
#include "kz4ap/signal_detector.hpp"
#include "kz4ap/spectrum.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace kz4ap {

// The FFT size the engine uses when EngineConfig::fft_size is 0: the largest power
// of two with sample_rate / fft_size >= 20 Hz, so bins stay about 23 Hz wide at
// 48, 96, 192 or 768 kHz. Throws std::invalid_argument for sample rates below 8000 Hz.
int choose_fft_size(int sample_rate);

struct EngineConfig {
    int sample_rate = 192000;
    int fft_size = 0;  // 0 = choose automatically: the largest power of two with sample_rate / fft_size >= 20 Hz
    int channel_bins = 64;
    double channel_cutoff_hz = 150.0;
    DetectorConfig detector;  // sample_rate, fft_size and hop are overwritten by the engine
    ClassicalDecoderConfig decoder;

    // Oracle mode, for the benchmark: when not empty, a channel is opened at each of
    // these frequencies (Hz from the span's center, rounded to the nearest FFT bin)
    // from the first sample, with track ids 1, 2, ... in this order, and the signal
    // detector is bypassed: no other track is born and none dies.
    std::vector<double> oracle_frequencies_hz;
};

struct EngineStats {
    std::uint64_t channel_samples = 0;  // channel samples delivered to decoders, summed over channels
    double channel_seconds = 0;         // the same, in s of channel output
    double decoder_seconds = 0;         // steady-clock time spent inside decoders, s
};

// Runs the decoding pipeline synchronously on the caller's thread. Output is
// identical however the input is split across calls to process().
class Engine {
public:
    // Throws std::invalid_argument if config.sample_rate is below 8000 Hz.
    Engine(const EngineConfig& config, EventBus& bus);
    ~Engine();

    void process(std::span<const Sample> samples);

    // End of input: processes buffered samples and flushes partly decoded characters.
    void finish();

    EngineStats stats() const;

private:
    struct Channel {
        Track track;
        int bin;  // the channel's center bin
        std::unique_ptr<Decoder> decoder;
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
};

}  // namespace kz4ap
