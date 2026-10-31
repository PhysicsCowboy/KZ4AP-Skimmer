#include "kz4ap/engine.hpp"

#include "kz4ap/bank_decoder.hpp"
#include "kz4ap/segmental_decoder.hpp"
#include "kz4ap/viterbi/hiss.hpp"
#include "kz4ap/viterbi_decoder.hpp"
#include "worker_pool.hpp"
#include "kz4ap/cpu_time.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
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

// The stages are timed on the thread's CPU clock, so a loaded machine does not inflate them.

EngineConfig resolved(EngineConfig c) {
    const int automatic = choose_fft_size(c.sample_rate);  // also validates the sample rate
    if (c.fft_size == 0) c.fft_size = automatic;
    if (!(c.channel_distance_hz > 0)) throw std::invalid_argument("channel distance must be positive");
    c.detector.attribution_distance_hz = c.channel_distance_hz;
    // The segmental decoder runs on the air, where wide signals are: 1 kHz either side (heuristic: wide against a CW station's
    // few bins, so that ten stations in it leave the median at the noise; on real 40 m I/Q it took out most of the false
    // tracks). The other decoders keep the detector their test-set results were measured with.
    if (c.decoder.front_end == FrontEnd::Segmental && c.detector.local_floor_radius_hz == 0.0) c.detector.local_floor_radius_hz = 1000.0;
    if (c.decoder.front_end == FrontEnd::Segmental) {
        c.detector.one_track_per_peak = true;
        c.detector.dc_guard_hz = c.channel_distance_hz;
    }
    return c;
}

DetectorConfig detector_config(const EngineConfig& c) {
    DetectorConfig d = c.detector;
    d.sample_rate = c.sample_rate;
    d.fft_size = c.fft_size;
    d.hop = c.fft_size / 2;
    return d;
}

// The oracle tracks' rules (EngineConfig::oracle_frequency_tracks).
void check_oracle_tracks(const EngineConfig& c) {
    if (c.oracle_frequency_tracks.empty()) return;
    if (c.decoder.front_end != FrontEnd::Viterbi && c.decoder.front_end != FrontEnd::Segmental)
        throw std::invalid_argument("oracle_frequency_tracks needs the new decoder (FrontEnd::Viterbi)");
    if (c.oracle_frequency_tracks.size() != c.oracle_frequencies_hz.size())
        throw std::invalid_argument("oracle_frequency_tracks needs oracle mode and one track per oracle frequency");
    for (const std::vector<OracleSegment>& track : c.oracle_frequency_tracks) {
        if (!track.empty() && !(track.front().start_s <= 0.0))
            throw std::invalid_argument("an oracle track's first segment must start at or before 0 s");
        for (std::size_t i = 0; i < track.size(); ++i) {
            const OracleSegment& s = track[i];
            if (!std::isfinite(s.start_s) || !std::isfinite(s.frequency_hz) || !std::isfinite(s.drift_hz_per_s) ||
                !std::isfinite(s.drift_origin_s))
                throw std::invalid_argument("an oracle segment's values must be finite (s, Hz, Hz/s, s)");
            if (i > 0 && s.start_s < track[i - 1].start_s)
                throw std::invalid_argument("an oracle track's segments must be in the order of their starts");
        }
    }
}

}  // namespace

double oracle_frequency_hz(std::span<const OracleSegment> track, double t_s) {
    if (track.empty()) throw std::invalid_argument("oracle_frequency_hz: no segment");
    const OracleSegment* in_force = &track.front();
    for (const OracleSegment& s : track) {
        if (s.start_s > t_s) break;
        in_force = &s;
    }
    return in_force->frequency_hz + in_force->drift_hz_per_s * (t_s - in_force->drift_origin_s);
}

Engine::Engine(const EngineConfig& config, EventBus& bus)
    : config_(resolved(config)),
      bus_(bus),
      hop_(config_.fft_size / 2),
      spectrum_(config_.sample_rate, config_.fft_size, config_.fft_size / 2),
      detector_(detector_config(config_)),
      channelizer_(ChannelizerConfig{config_.sample_rate, config_.fft_size, config_.channel_bins,
                                     config_.channel_cutoff_hz}) {
    pending_.reserve(static_cast<std::size_t>(hop_));
    if (config_.worker_threads < 1) throw std::invalid_argument("worker_threads must be at least 1");
    if (config_.worker_threads > 1) pool_ = std::make_unique<WorkerPool>(config_.worker_threads);
    oracle_ = !config_.oracle_frequencies_hz.empty();
    check_oracle_tracks(config_);
    if (config_.decoder.front_end == FrontEnd::Viterbi) {
        // The new decoder: the hiss estimator reads every spectrum frame (in oracle mode too), and its decoders share
        // one set of parts built for every channel's configuration (the genie's spreads may lengthen the blocks).
        if (!config_.viterbi_genie.empty() && config_.viterbi_genie.size() != config_.oracle_frequencies_hz.size())
            throw std::invalid_argument("viterbi_genie needs oracle mode and one entry per oracle frequency");
        hiss_ = std::make_unique<viterbi::HissEstimator>(config_.sample_rate, config_.fft_size, config_.viterbi);
        std::vector<viterbi::ViterbiConfig> configs = {config_.viterbi};
        for (const viterbi::ViterbiGenie& genie : config_.viterbi_genie)
            configs.push_back(viterbi::with_genie(config_.viterbi, genie));
        viterbi_shared_ = make_viterbi_shared(configs, ChannelizerConfig{config_.sample_rate, config_.fft_size,
                                                                          config_.channel_bins,
                                                                          config_.channel_cutoff_hz},
                                              config_.viterbi_omega_bar_default_table);
    } else if (config_.decoder.front_end == FrontEnd::Segmental) {
        // The hiss estimator and a channel that follows the detector, as the new decoder; its own shared parts.
        if (!config_.viterbi_genie.empty()) throw std::invalid_argument("viterbi_genie needs FrontEnd::Viterbi");
        hiss_ = std::make_unique<viterbi::HissEstimator>(config_.sample_rate, config_.fft_size, config_.segmental.model);
        segmental_shared_ = std::make_shared<const SegmentalShared>(
            config_.segmental, ChannelizerConfig{config_.sample_rate, config_.fft_size, config_.channel_bins,
                                                 config_.channel_cutoff_hz});
    } else if (!config_.viterbi_genie.empty()) {
        throw std::invalid_argument("viterbi_genie needs the new decoder (FrontEnd::Viterbi)");
    }
    const bool following =
        config_.decoder.front_end == FrontEnd::Viterbi || config_.decoder.front_end == FrontEnd::Segmental;
    for (std::size_t i = 0; i < config_.oracle_frequencies_hz.size(); ++i) {
        // The detector's frequency at the stream's start: the label's (its track's at 0 s, if it has one).
        const bool tracked = !config_.oracle_frequency_tracks.empty() && !config_.oracle_frequency_tracks[i].empty();
        const double f_det = tracked ? oracle_frequency_hz(config_.oracle_frequency_tracks[i], 0.0)
                                     : config_.oracle_frequencies_hz[i];
        const int bin = channelizer_.hz_to_bin(f_det);
        Track track;
        track.id = static_cast<std::uint32_t>(i + 1);
        // The new decoder's channel is centered on the detector's frequency (ChannelFollower); the others on the bin.
        track.freq_hz = following ? f_det : channelizer_.bin_to_hz(bin);
        open_channel(track, bin);
        // Oracle mode: the anchor is the labeled frequency itself, not the bin center the channel is rounded to (on the
        // Matched path the NCO still starts at the bin center, so the tracker must find the residual; it fine-tunes
        // within +/-12 Hz of the label).
        channels_.at(track.id).detector_freq_hz = f_det;
    }
}

Engine::~Engine() = default;

EngineStats Engine::stats() const {
    EngineStats s;
    s.channel_samples = channel_samples_;
    s.channel_seconds = static_cast<double>(channel_samples_) / channelizer_.output_rate();
    s.decoder_seconds = stages_.decoder_s;
    s.stages = stages_;
    return s;
}

double Engine::worker_cpu_seconds() const { return pool_ ? pool_->worker_cpu_seconds() : 0.0; }

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
    const auto hop = static_cast<std::size_t>(hop_);
    while (!samples.empty()) {
        if (pending_.empty() && samples.size() >= hop) {
            process_hop(samples.first(hop));
            samples = samples.subspan(hop);
            continue;
        }
        const std::size_t take = std::min(hop - pending_.size(), samples.size());
        pending_.insert(pending_.end(), samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(take));
        samples = samples.subspan(take);
        if (pending_.size() == hop) {
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
    double mark = thread_cpu_precise_seconds();
    // Seconds since the last call, added to one stage's total.
    const auto lap = [&mark](double& stage_s) {
        const double now = thread_cpu_precise_seconds();
        stage_s += now - mark;
        mark = now;
    };
    std::vector<SpectrumFrame> frames = spectrum_.push(hop);
    lap(stages_.spectrum_s);
    for (auto& frame : frames) {
        DetectorUpdate update;
        if (!oracle_) update = detector_.process(frame);
        lap(stages_.detector_s);
        if (hiss_) hiss_->push(frame);  // the new decoder's hiss, before the frame moves on
        lap(stages_.hiss_s);
        bus_.publish(Event{std::move(frame)});
        lap(stages_.publish_s);
        for (const auto id : update.died) {
            const bool merged = std::find(update.merged.begin(), update.merged.end(), id) != update.merged.end();
            close_channel(id, merged ? TrackEnd::Merged : TrackEnd::Faded);
        }
        for (const auto& track : update.born) open_channel(track, channelizer_.hz_to_bin(track.freq_hz));
        lap(stages_.lifecycle_s);
        if (!oracle_ && config_.decoder.front_end != FrontEnd::Envelope) {
            // Option 1: the detector decides where each channel's station is (its track follows its own
            // peak); the channel's tracker (Matched) is anchored there, or the bank's mixing set there, before
            // its next block.
            for (const auto& t : detector_.tracks()) {
                if (const auto c = channels_.find(t.id); c != channels_.end()) c->second.detector_freq_hz = t.freq_hz;
            }
        }
        lap(stages_.detector_s);
    }
    const Channelizer::Block block = channelizer_.begin_block(hop);
    lap(stages_.channelizer_s);

    // The channels' work, in any order and on any thread: each job touches only its own Channel and reads what no job
    // writes (the block's spectrum, the hiss estimator, the configuration). The results go out afterward, one thread and
    // in the order of the track ids, whatever the pool did, so the events are what one thread would give. A tap is the
    // benchmark's and may not expect threads.
    active_.clear();
    for (auto& entry : channels_) active_.push_back(&entry);
    const std::function<void(std::size_t)> job = [&](std::size_t i) { process_channel(active_[i]->first, active_[i]->second, block); };
    if (pool_ && !tap_) {
        pool_->run(active_.size(), job);
    } else {
        for (std::size_t i = 0; i < active_.size(); ++i) job(i);
    }
    lap(stages_.channels_wall_s);
    for (auto* entry : active_) {
        Channel& channel = entry->second;
        stages_.filter_s += channel.filter_s;
        stages_.decoder_s += channel.decoder_s;
        track_decoder_s_[entry->first] += channel.decoder_s;
        channel.filter_s = 0;
        channel.decoder_s = 0;
        channel_samples_ += channel.block_samples;
        publish_update(entry->first, std::move(channel.update));
        channel.update = DecodeUpdate{};
    }
    lap(stages_.publish_s);
    if (config_.judge_tracks && !oracle_) judge_channels(block.first_index);
    lap(stages_.lifecycle_s);
}

void Engine::process_channel(std::uint32_t id, Channel& channel, const Channelizer::Block& block) {
    const double started = thread_cpu_precise_seconds();
    const std::span<const Sample> s = channelizer_.cut(block, channel.bin, channel.cut_in, channel.cut_out);
    const std::uint64_t first_index = block.first_index;
    const double t0 = static_cast<double>(first_index) / channelizer_.output_rate();
    const int block_bin = channel.bin;  // the bin this block was cut at
    const double center_hz = channelizer_.bin_to_hz(block_bin);
    std::span<const Sample> samples = s;
    double block_center_hz = center_hz;
    if (channel.follower) {
        // The new decoder: the channel follows the detector (design Part 1; plan O2, O3). An oracle channel's
        // detector frequency is its track's at the block's start, held over the block.
        if (oracle_ && !config_.oracle_frequency_tracks.empty() && !config_.oracle_frequency_tracks.at(id - 1).empty())
            channel.detector_freq_hz = oracle_frequency_hz(config_.oracle_frequency_tracks[id - 1], t0);
        const double f_det = channel.detector_freq_hz;
        channel.shifted.assign(s.begin(), s.end());
        channel.follower->shift(channel.shifted, first_index, f_det, center_hz);
        samples = channel.shifted;
        block_center_hz = f_det;
        // The coarse step, from the next block on.
        channel.bin = channel.follower->bin_for(f_det, block_bin, channelizer_);
        channel.track.freq_hz = f_det;  // the station is reported where its channel is centered
    } else {
        channel.decoder->set_frequency_anchor_hz(channel.detector_freq_hz - center_hz);  // Envelope: ignored
        // Bank: the block is mixed down by this anchor.
    }
    if (tap_) tap_(ChannelBlock{id, first_index, block_center_hz, samples, channel.detector_freq_hz});
    // The new decoder: its N0, the hiss estimator's density at the bin this block was cut at, set before every block.
    if (hiss_) channel.decoder->set_noise_density_fs2_per_hz(hiss_->noise_density_fs2_per_hz(block_bin));
    const double filtered = thread_cpu_precise_seconds();
    channel.update = channel.decoder->process(samples, t0);
    const double decoded = thread_cpu_precise_seconds();
    channel.filter_s = filtered - started;
    channel.decoder_s = decoded - filtered;
    channel.block_samples = s.size();
    // Matched mode: the decoder re-centers on the carrier; report the station there. Bank: the anchor.
    if (channel.update.freq_offset_hz) channel.track.freq_hz = center_hz + *channel.update.freq_offset_hz;
}

void Engine::open_channel(const Track& track, int bin) {
    const double residual_hz = track.freq_hz - channelizer_.bin_to_hz(bin);
    std::unique_ptr<Decoder> decoder;
    if (config_.decoder.front_end == FrontEnd::Bank) {
        decoder = std::make_unique<BankDecoder>(channelizer_.output_rate(), config_.bank, residual_hz);
    } else if (config_.decoder.front_end == FrontEnd::Segmental) {
        decoder = std::make_unique<SegmentalDecoder>(channelizer_.output_rate(), segmental_shared_);
    } else if (config_.decoder.front_end == FrontEnd::Viterbi) {
        // Oracle channels are tracks 1, 2, ... in the order of oracle_frequencies_hz, and so of viterbi_genie.
        viterbi::ViterbiConfig channel_config = config_.viterbi;
        if (oracle_ && !config_.viterbi_genie.empty())
            channel_config = viterbi::with_genie(config_.viterbi, config_.viterbi_genie.at(track.id - 1));
        decoder = std::make_unique<ViterbiDecoder>(channelizer_.output_rate(), channel_config, viterbi_shared_);
    } else {
        decoder = std::make_unique<ClassicalDecoder>(channelizer_.output_rate(), config_.decoder, residual_hz);
    }
    Channel channel;
    channel.track = track;
    channel.bin = bin;
    channel.decoder = std::move(decoder);
    channel.detector_freq_hz = track.freq_hz;
    channel.cut_in.resize(static_cast<std::size_t>(channelizer_.channel_bins()));
    channel.cut_out.resize(static_cast<std::size_t>(channelizer_.channel_bins()));
    // The new decoder's channel follows the detector (plan O2: its channels only).
    if (config_.decoder.front_end == FrontEnd::Viterbi || config_.decoder.front_end == FrontEnd::Segmental)
        channel.follower.emplace(channelizer_.output_rate(), channelizer_.bin_to_hz(1) - channelizer_.bin_to_hz(0));
    channels_.emplace(track.id, std::move(channel));
    bus_.publish(Event{TrackEvent{TrackEvent::Kind::Born, track}});
}

void Engine::judge_channels(std::uint64_t first_index) {
    const double now_s = static_cast<double>(first_index) / channelizer_.output_rate();
    retiring_.clear();
    for (auto* entry : active_) {
        Channel& channel = entry->second;
        if (now_s < channel.next_judged_s) continue;
        channel.next_judged_s = now_s + 1.0;  // the judge's clock: a second of stream
        const std::optional<KeyingEvidence> evidence = channel.decoder->keying_evidence();
        if (!evidence) continue;
        const TrackJudge::Verdict verdict = channel.judge.update(*evidence, channel.decoder->published_symbols());
        if (verdict.retire) retiring_.emplace_back(entry->first, verdict);
    }
    for (const auto& [id, verdict] : retiring_) {
        detector_.retire(id, now_s, verdict.hold_s);
        close_channel(id, verdict.end);
    }
    while (track_budget_ > 0 && channels_.size() > track_budget_) {
        // The least evidence: what has read nothing plausible first, then what earns the least.
        auto worst = channels_.begin();
        const auto rank = [](const Channel& c) { return std::make_pair(c.judge.proven(), c.judge.earning()); };
        for (auto it = channels_.begin(); it != channels_.end(); ++it)
            if (rank(it->second) < rank(worst->second)) worst = it;
        const std::uint32_t id = worst->first;
        detector_.retire(id, now_s, TrackJudge::kHoldS);
        close_channel(id, TrackEnd::Shed);
    }
}

void Engine::set_track_budget(std::size_t tracks) {
    track_budget_ = tracks;
    detector_.set_track_limit(tracks > 0 ? tracks : config_.detector.max_tracks);
}

void Engine::close_channel(std::uint32_t id, TrackEnd end) {
    const auto it = channels_.find(id);
    if (it == channels_.end()) return;
    publish_update(id, it->second.decoder->flush());
    bus_.publish(Event{TrackEvent{TrackEvent::Kind::Died, it->second.track, end}});
    channels_.erase(it);
}

void Engine::publish_update(std::uint32_t id, DecodeUpdate&& update) {
    if (update.chars.empty() && update.corrections.empty()) return;  // Envelope and Matched: corrections always empty
    const auto& channel = channels_.at(id);
    bus_.publish(Event{DecodedTextEvent{id, channel.track.freq_hz, std::move(update.chars), update.wpm,
                                        update.confidence, std::move(update.corrections)}});
}

}  // namespace kz4ap
