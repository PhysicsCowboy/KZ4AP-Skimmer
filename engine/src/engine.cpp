#include "kz4ap/engine.hpp"

#include "kz4ap/bank_decoder.hpp"
#include "kz4ap/viterbi/hiss.hpp"
#include "kz4ap/viterbi_decoder.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace kz4ap {

int choose_fft_size(int sample_rate) {
    if (sample_rate < 8000) throw std::invalid_argument("sample rate must be at least 8000 Hz");
    int n = 1;
    while (sample_rate / (2.0 * n) >= 20.0) n *= 2;
    return n;
}

namespace {

EngineConfig resolved(EngineConfig c) {
    const int automatic = choose_fft_size(c.sample_rate);  // also validates the sample rate
    if (c.fft_size == 0) c.fft_size = automatic;
    if (!(c.channel_distance_hz > 0)) throw std::invalid_argument("channel distance must be positive");
    c.detector.attribution_distance_hz = c.channel_distance_hz;
    return c;
}

DetectorConfig detector_config(const EngineConfig& c) {
    DetectorConfig d = c.detector;
    d.sample_rate = c.sample_rate;
    d.fft_size = c.fft_size;
    d.hop = c.fft_size / 2;
    return d;
}

}  // namespace

Engine::Engine(const EngineConfig& config, EventBus& bus)
    : config_(resolved(config)),
      bus_(bus),
      hop_(config_.fft_size / 2),
      spectrum_(config_.sample_rate, config_.fft_size, config_.fft_size / 2),
      detector_(detector_config(config_)),
      channelizer_(ChannelizerConfig{config_.sample_rate, config_.fft_size, config_.channel_bins,
                                     config_.channel_cutoff_hz}) {
    pending_.reserve(static_cast<std::size_t>(hop_));
    oracle_ = !config_.oracle_frequencies_hz.empty();
    if (config_.decoder.front_end == FrontEnd::Viterbi) {
        // The new decoder: the hiss estimator reads every spectrum frame (in oracle mode too), and its decoders share
        // one set of parts built for every channel's configuration (the genie's spreads may lengthen the blocks).
        if (!config_.viterbi_genie.empty() && config_.viterbi_genie.size() != config_.oracle_frequencies_hz.size())
            throw std::invalid_argument("viterbi_genie needs oracle mode and one entry per oracle frequency");
        hiss_ = std::make_unique<viterbi::HissEstimator>(config_.sample_rate, config_.fft_size, config_.viterbi);
        std::vector<viterbi::ViterbiConfig> configs = {config_.viterbi};
        for (const viterbi::ViterbiGenie& genie : config_.viterbi_genie)
            configs.push_back(viterbi::with_genie(config_.viterbi, genie, 0.0));  // the frequency does not matter here
        viterbi_shared_ = make_viterbi_shared(configs, ChannelizerConfig{config_.sample_rate, config_.fft_size,
                                                                          config_.channel_bins,
                                                                          config_.channel_cutoff_hz});
    } else if (!config_.viterbi_genie.empty()) {
        throw std::invalid_argument("viterbi_genie needs the new decoder (FrontEnd::Viterbi)");
    }
    for (std::size_t i = 0; i < config_.oracle_frequencies_hz.size(); ++i) {
        const int bin = channelizer_.hz_to_bin(config_.oracle_frequencies_hz[i]);
        Track track;
        track.id = static_cast<std::uint32_t>(i + 1);
        track.freq_hz = channelizer_.bin_to_hz(bin);
        open_channel(track, bin);
        // Oracle mode: the tracker's anchor is the labeled frequency itself, not the bin center the
        // channel is rounded to (the NCO still starts at the bin center, so the tracker must find the
        // residual; it fine-tunes within +/-12 Hz of the label).
        channels_.at(track.id).detector_freq_hz = config_.oracle_frequencies_hz[i];
    }
}

Engine::~Engine() = default;

EngineStats Engine::stats() const {
    EngineStats s;
    s.channel_samples = channel_samples_;
    s.channel_seconds = static_cast<double>(channel_samples_) / channelizer_.output_rate();
    s.decoder_seconds = decoder_seconds_;
    return s;
}

double Engine::channel_rate() const { return channelizer_.output_rate(); }

const viterbi::SearchStats* Engine::viterbi_stats(std::uint32_t track_id) const {
    const auto it = channels_.find(track_id);
    if (it == channels_.end()) return nullptr;
    const auto* decoder = dynamic_cast<const ViterbiDecoder*>(it->second.decoder.get());
    return decoder ? &decoder->stats() : nullptr;
}

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
        DetectorUpdate update;
        if (!oracle_) update = detector_.process(frame);
        if (hiss_) hiss_->push(frame);  // the new decoder's hiss, before the frame moves on
        bus_.publish(Event{std::move(frame)});
        for (const auto id : update.died) close_channel(id);
        for (const auto& track : update.born) open_channel(track, channelizer_.hz_to_bin(track.freq_hz));
        if (!oracle_ && config_.decoder.front_end != FrontEnd::Envelope) {
            // Option 1: the detector decides where each channel's station is (its track follows its own
            // peak); the channel's tracker (Matched) is anchored there, or the bank's mixing set there, before
            // its next block.
            for (const auto& t : detector_.tracks()) {
                if (const auto c = channels_.find(t.id); c != channels_.end()) c->second.detector_freq_hz = t.freq_hz;
            }
        }
    }
    channelizer_.push(hop, [this](std::uint32_t id, std::uint64_t first_index, std::span<const Sample> s) {
        const auto it = channels_.find(id);
        if (it == channels_.end()) return;
        auto& channel = it->second;
        const double t0 = static_cast<double>(first_index) / channelizer_.output_rate();
        const double center_hz = channelizer_.bin_to_hz(channel.bin);
        if (tap_) tap_(ChannelBlock{id, first_index, center_hz, s, channel.detector_freq_hz});
        channel.decoder->set_frequency_anchor_hz(channel.detector_freq_hz - center_hz);  // Envelope: ignored
        // Bank: the block is mixed down by this anchor. The new decoder: its N0, the hiss estimator's density at the
        // channel's center bin, set before every block.
        if (hiss_) channel.decoder->set_noise_density_fs2_per_hz(hiss_->noise_density_fs2_per_hz(channel.bin));
        const auto started = std::chrono::steady_clock::now();
        auto update = channel.decoder->process(s, t0);
        decoder_seconds_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        channel_samples_ += s.size();
        // Matched mode: the decoder re-centers on the carrier; report the station there. Bank: the anchor.
        if (update.freq_offset_hz) channel.track.freq_hz = center_hz + *update.freq_offset_hz;
        publish_update(id, std::move(update));
    });
}

void Engine::open_channel(const Track& track, int bin) {
    channelizer_.add_channel(track.id, bin);
    const double residual_hz = track.freq_hz - channelizer_.bin_to_hz(bin);
    std::unique_ptr<Decoder> decoder;
    if (config_.decoder.front_end == FrontEnd::Bank) {
        decoder = std::make_unique<BankDecoder>(channelizer_.output_rate(), config_.bank, residual_hz);
    } else if (config_.decoder.front_end == FrontEnd::Viterbi) {
        // Oracle channels are tracks 1, 2, ... in the order of oracle_frequencies_hz, and so of viterbi_genie.
        viterbi::ViterbiConfig channel_config = config_.viterbi;
        if (oracle_ && !config_.viterbi_genie.empty())
            channel_config = viterbi::with_genie(config_.viterbi, config_.viterbi_genie.at(track.id - 1),
                                                 channelizer_.bin_to_hz(bin));
        decoder = std::make_unique<ViterbiDecoder>(channelizer_.output_rate(), channel_config, residual_hz,
                                                   viterbi_shared_);
    } else {
        decoder = std::make_unique<ClassicalDecoder>(channelizer_.output_rate(), config_.decoder, residual_hz);
    }
    channels_.emplace(track.id, Channel{track, bin, std::move(decoder), track.freq_hz});
    bus_.publish(Event{TrackEvent{TrackEvent::Kind::Born, track}});
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
    if (update.chars.empty() && update.corrections.empty()) return;  // Envelope and Matched: corrections always empty
    const auto& channel = channels_.at(id);
    bus_.publish(Event{DecodedTextEvent{id, channel.track.freq_hz, std::move(update.chars), update.wpm,
                                        update.confidence, std::move(update.corrections)}});
}

}  // namespace kz4ap
