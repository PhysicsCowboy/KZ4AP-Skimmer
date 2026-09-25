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

struct EngineConfig {
    int sample_rate = 192000;
    int fft_size = 8192;
    int channel_bins = 64;
    double channel_cutoff_hz = 150.0;
    DetectorConfig detector;  // sample_rate, fft_size and hop are overwritten by the engine
    ClassicalDecoderConfig decoder;
};

// Runs the decoding pipeline synchronously on the caller's thread. Output is
// identical however the input is split across calls to process().
class Engine {
public:
    Engine(const EngineConfig& config, EventBus& bus);
    ~Engine();

    void process(std::span<const Sample> samples);

    // End of input: processes buffered samples and flushes partly decoded characters.
    void finish();

private:
    struct Channel {
        Track track;
        std::unique_ptr<Decoder> decoder;
    };

    void process_hop(std::span<const Sample> hop);
    void close_channel(std::uint32_t id);
    void publish_update(std::uint32_t id, DecodeUpdate&& update);

    EngineConfig config_;
    EventBus& bus_;
    int hop_;
    SpectrumAnalyzer spectrum_;
    SignalDetector detector_;
    Channelizer channelizer_;
    std::vector<Sample> pending_;
    std::map<std::uint32_t, Channel> channels_;
};

}  // namespace kz4ap
