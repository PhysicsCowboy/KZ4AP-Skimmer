#include "kz4ap/engine.hpp"

#include <utility>

namespace kz4ap {
namespace {

DetectorConfig detector_config(const EngineConfig& c) {
    DetectorConfig d = c.detector;
    d.sample_rate = c.sample_rate;
    d.fft_size = c.fft_size;
    d.hop = c.fft_size / 2;
    return d;
}

}  // namespace

Engine::Engine(const EngineConfig& config, EventBus& bus)
    : config_(config),
      bus_(bus),
      hop_(config.fft_size / 2),
      spectrum_(config.sample_rate, config.fft_size, config.fft_size / 2),
      detector_(detector_config(config)),
      channelizer_(ChannelizerConfig{config.sample_rate, config.fft_size, config.channel_bins,
                                     config.channel_cutoff_hz}) {
    pending_.reserve(static_cast<std::size_t>(hop_));
}

Engine::~Engine() = default;

void Engine::process(std::span<const Sample> samples) {
    // Work in whole hops so the spectrum analyzer and channelizer stay in lockstep,
    // whatever size the caller's chunks are.
    for (const Sample& s : samples) {
        pending_.push_back(s);
        if (static_cast<int>(pending_.size()) == hop_) {
            process_hop(pending_);
            pending_.clear();
        }
    }
}

void Engine::finish() {
    if (!pending_.empty()) {
        pending_.resize(static_cast<std::size_t>(hop_), Sample{});
        process_hop(pending_);
        pending_.clear();
    }
    for (auto& [id, channel] : channels_) publish_update(id, channel.decoder->flush());
}

void Engine::process_hop(std::span<const Sample> hop) {
    for (auto& frame : spectrum_.push(hop)) {
        const auto update = detector_.process(frame);
        bus_.publish(Event{std::move(frame)});
        for (const auto id : update.died) close_channel(id);
        for (const auto& track : update.born) {
            channelizer_.add_channel(track.id, channelizer_.hz_to_bin(track.freq_hz));
            channels_.emplace(track.id, Channel{track, std::make_unique<ClassicalDecoder>(
                                                           channelizer_.output_rate(), config_.decoder)});
            bus_.publish(Event{TrackEvent{TrackEvent::Kind::Born, track}});
        }
    }
    channelizer_.push(hop, [this](std::uint32_t id, std::uint64_t first_index, std::span<const Sample> s) {
        const auto it = channels_.find(id);
        if (it == channels_.end()) return;
        const double t0 = static_cast<double>(first_index) / channelizer_.output_rate();
        publish_update(id, it->second.decoder->process(s, t0));
    });
}

void Engine::close_channel(std::uint32_t id) {
    const auto it = channels_.find(id);
    if (it == channels_.end()) return;
    publish_update(id, it->second.decoder->flush());
    bus_.publish(Event{TrackEvent{TrackEvent::Kind::Died, it->second.track}});
    channelizer_.remove_channel(id);
    channels_.erase(it);
}

void Engine::publish_update(std::uint32_t id, DecodeUpdate&& update) {
    if (update.chars.empty()) return;
    const auto& channel = channels_.at(id);
    bus_.publish(Event{DecodedTextEvent{id, channel.track.freq_hz, std::move(update.chars), update.wpm,
                                        update.confidence}});
}

}  // namespace kz4ap
