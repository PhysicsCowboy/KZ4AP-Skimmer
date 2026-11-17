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
#include <deque>
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
    if (!c.as_given) apply_path_defaults(c);
    if (!(c.dc_block_s >= 0.0) || !std::isfinite(c.dc_block_s)) throw std::invalid_argument("dc_block_s must be finite and not negative (s)");
    return c;
}

DetectorConfig detector_config(const EngineConfig& c) {
    DetectorConfig d = c.detector;
    d.sample_rate = c.sample_rate;
    if (d.fft_factor < 1 || (d.fft_factor & (d.fft_factor - 1)) != 0) throw std::invalid_argument("invalid signal detector config");
    d.fft_size = c.fft_size * d.fft_factor;
    d.hop = d.fft_size / 2;
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

void apply_path_defaults(EngineConfig& c) {
    c.detector.attribution_distance_hz = c.channel_distance_hz;
    if (c.decoder.front_end != FrontEnd::Segmental) return;
    // The segmental decoder runs on the air, where wide signals are: 1 kHz either side (heuristic: wide against a CW station's
    // few bins, so that ten stations in it leave the lower quartile at the noise; on real 40 m I/Q it took out most of the false
    // tracks). The other decoders keep the detector their test-set results were measured with.
    if (c.detector.local_floor_radius_hz == 0.0) c.detector.local_floor_radius_hz = 1000.0;
    c.detector.one_track_per_peak = true;
    // The detector's own spectrum, 4 times finer than the band's: 5.86 Hz bins and a window of 170.7 ms at every usual rate, a frame
    // every 85.3 ms (docs/signal-processing.md, section 6). On the band's 23.4 Hz bins a neighbor's main lobe reaches 47 Hz, so a
    // station within about 60 Hz of a stronger one was never a peak.
    c.detector.fft_factor = 4;
    // A peak the highest within +/-12 Hz (2 bins), its level read within +/-6 Hz, a candidate moving at most 6 Hz a frame (70 Hz/s):
    // the Hann main lobe's half width is 2 bins, 11.7 Hz. Heuristics.
    c.detector.peak_radius_hz = 12.0;
    c.detector.level_radius_hz = 6.0;
    c.detector.candidate_step_hz = 6.0;
    // A peak beside a stronger one must stand 6 dB above the valley between them, within 150 Hz (the channel's half width), and is not
    // a keyed carrier's sideband: no mirror as high about a bin 10 dB above it, within 3 dB. Heuristics, chosen by measurement.
    c.detector.prominence_db = 6.0f;
    c.detector.prominence_radius_hz = 150.0;
    c.detector.sideband_db = 10.0f;
    c.detector.sideband_mirror_db = 3.0f;
    c.detector.sideband_radius_hz = 150.0;
    // 30 Hz between tracks, a track following its own peak within 12 Hz a frame: a station 30 Hz or more from another opens its own
    // channel. Heuristics, chosen by measurement.
    c.detector.attribution_distance_hz = 30.0;
    c.detector.follow_radius_hz = 12.0;
    // 7 dB above the floor on the finer bins, where a keyed carrier stands 5 to 6 dB higher per bin than on the band's (measured, one
    // station at 22 WPM): heuristic, chosen by measurement.
    c.detector.threshold_db = 7.0f;
    // Every 2nd frame, 171 ms, against an average over average_s: heuristic. 32 768 bins selected each frame is most of the detector's CPU.
    c.detector.floor_every_frames = 2;
    // A radio's DC offset is removed (its notch at 0 Hz, -3 dB re the passband at +/-0.016 Hz), so no guard keeps a station at the
    // center from its track. 10 s, not 1 s: the average follows a keyed station within about 1 / (2 pi tau) of 0 Hz and takes its
    // carrier out of its marks (at 1 s a station drifting from 0 Hz read 57 edits of 99, at 10 s none). Heuristic, chosen by measurement.
    c.dc_block_s = 10.0;
    c.detector.dc_guard_hz = 0.0;
    // The first retirement at a place leaves it open: a station read badly at first (beside a neighbor) or listening gets a second
    // track; the next retirement there closes it as before. Heuristic, chosen by measurement.
    c.first_retirement_open = 2;
    // A track whose keying is an older, proven neighbor's is retired as its copy (keying_coherence.hpp): the neighbor within 150 Hz
    // and 15 dB or more above it at the detector, their envelopes over the last 6 s correlated (rho >= 0.5) at a gain of at most 1, and
    // the track holding power of its own of at most a quarter of what the leak and its hiss explain. Heuristics, chosen by
    // measurement; the forms of rho, the gain and the own power derived (docs/signal-processing.md, section 6 and A.6).
    c.copy_window_s = 6.0;
    // A symbol whose keying, over its own span and 0.5 s either side, is a proven neighbor's, heard more strongly there, is that
    // neighbor's (DecodedSymbol::heard_from): the copy test's conditions on a stretch of text, where a track that is not a copy still
    // prints its neighbor's over between its own. Heuristic, chosen by measurement; the forms derived as the copy test's (section 6).
    c.attribute_pad_s = 0.5;
}

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
    if (config_.detector.fft_factor > 1 && !oracle_) {
        const int n = config_.fft_size * config_.detector.fft_factor;
        detector_spectrum_.emplace(config_.sample_rate, n, n / 2, false);
    }
    if (config_.dc_block_s > 0.0) dc_alpha_ = 1.0 - std::exp(-1.0 / (config_.dc_block_s * config_.sample_rate));
    if (config_.decoder.front_end == FrontEnd::Segmental) {
        // A channel rate that is no whole multiple of the decoder's samples would fail deep inside it, in terms an operator cannot use.
        const double channel_samples = channelizer_.output_rate() * config_.segmental.model.sample_interval_s;
        if (std::abs(channel_samples - std::round(channel_samples)) > 1e-9)
            throw std::invalid_argument("this decoder needs a sample rate of 48, 96, 192, 384 or 768 kHz, not " +
                                        std::to_string(config_.sample_rate) + " Hz");
    }
    if (!(config_.replay_s >= 0.0) || !std::isfinite(config_.replay_s)) throw std::invalid_argument("replay_s must be finite and not negative (s)");
    if (!(config_.replay_tracks_per_s > 0.0)) throw std::invalid_argument("replay_tracks_per_s must be positive (tracks/s)");
    if (!(config_.copy_window_s >= 0.0) || !std::isfinite(config_.copy_window_s) || !(config_.copy_detrend_s >= 0.0) ||
        !std::isfinite(config_.copy_detrend_s) || !(config_.copy_radius_hz >= 0.0) || !std::isfinite(config_.copy_level_db) ||
        !(config_.copy_rho > -1.0 && config_.copy_rho <= 1.0) || !(config_.copy_gain_max > 0.0) || std::isnan(config_.copy_own_snr_max))
        throw std::invalid_argument("copy_window_s, copy_detrend_s and copy_radius_hz must be finite and not negative (s, s, Hz), "
                                    "copy_level_db finite (dB), copy_rho in (-1, 1], copy_gain_max positive and copy_own_snr_max a number");
    if (!(config_.attribute_pad_s >= 0.0) || !std::isfinite(config_.attribute_pad_s) || !(config_.attribute_radius_hz >= 0.0) ||
        !(config_.attribute_rho > -1.0 && config_.attribute_rho <= 1.0) || std::isnan(config_.attribute_own_max) ||
        !(config_.attribute_min_s >= 0.0) || !std::isfinite(config_.attribute_min_s) || !(config_.attribute_history_s > 0.0) ||
        !std::isfinite(config_.attribute_history_s))
        throw std::invalid_argument("attribute_pad_s, attribute_min_s and attribute_radius_hz must be finite and not negative (s, s, Hz), "
                                    "attribute_rho in (-1, 1], attribute_own_max a number and attribute_history_s finite and positive (s)");
    const bool judged = !oracle_ && config_.decoder.front_end == FrontEnd::Segmental && config_.judge_tracks;
    if (judged && (config_.copy_window_s > 0.0 || config_.attribute_pad_s > 0.0)) {
        block_s_ = static_cast<double>(hop_) / config_.sample_rate;
        detrend_blocks_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(config_.copy_detrend_s / block_s_)));
        block_samples_ = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::lround(channelizer_.output_rate() * block_s_)));
        if (config_.copy_window_s > 0.0)
            copy_blocks_ = std::max<std::size_t>(2, static_cast<std::size_t>(std::lround(config_.copy_window_s / block_s_)));
        if (config_.attribute_pad_s > 0.0) {
            heard_blocks_ = static_cast<std::size_t>(std::lround(config_.attribute_history_s / block_s_)) + 1;
            heard_min_blocks_ = std::max<std::size_t>(2, static_cast<std::size_t>(std::lround(config_.attribute_min_s / block_s_)));
        }
    }
    replay_on_ = !oracle_ && config_.decoder.front_end == FrontEnd::Segmental && config_.replay_s > 0.0;
    if (replay_on_) {
        const double block_s = static_cast<double>(hop_) / config_.sample_rate;
        channelizer_.keep_past(static_cast<std::size_t>(std::ceil(config_.replay_s / block_s)) + 1);  // the current block's too
        replay_per_block_ = config_.replay_tracks_per_s * block_s;
    }
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
    s.replayed_channel_seconds = static_cast<double>(replayed_samples_) / channelizer_.output_rate();
    s.longest_replay_wait_s = static_cast<double>(longest_replay_wait_blocks_) * hop_ / config_.sample_rate;
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
    for (auto& [id, channel] : channels_) {
        if (channel.replay_waiting) {
            // The stream ended while it waited for its turn.
            channel.replay_waiting = false;
            replay_channel(id, channel, channelizer_.past().size());
            publish_channel(id, channel);
        }
        publish_update(id, channel.decoder->flush());
    }
}

void Engine::process_hop(std::span<const Sample> hop) {
    double mark = thread_cpu_precise_seconds();
    // Seconds since the last call, added to one stage's total.
    const auto lap = [&mark](double& stage_s) {
        const double now = thread_cpu_precise_seconds();
        stage_s += now - mark;
        mark = now;
    };
    if (dc_alpha_ > 0.0) {
        // The DC blocker; its average starts at 0 FS. (Started at the first hop's mean, the noise in that mean stayed in the input
        // as a carrier at 0 Hz for about dc_block_s, about 21 dB below a station of +10 dB S500 at 48 kHz.)
        dc_hop_.resize(hop.size());
        for (std::size_t i = 0; i < hop.size(); ++i) {
            const std::complex<double> x(hop[i]);
            dc_ += dc_alpha_ * (x - dc_);
            dc_hop_[i] = Sample(static_cast<float>(x.real() - dc_.real()), static_cast<float>(x.imag() - dc_.imag()));
        }
        hop = dc_hop_;
    }
    std::vector<SpectrumFrame> frames = spectrum_.push(hop);
    std::vector<SpectrumFrame> detector_frames;
    if (detector_spectrum_) detector_frames = detector_spectrum_->push(hop);
    lap(stages_.spectrum_s);
    // What the detector decided on one of its frames: channels closed and opened, and each channel's detector frequency.
    const auto apply = [&](const DetectorUpdate& update) {
        for (const auto id : update.died) {
            const bool merged = std::find(update.merged.begin(), update.merged.end(), id) != update.merged.end();
            const bool copy = std::find(update.copies.begin(), update.copies.end(), id) != update.copies.end();
            close_channel(id, merged ? TrackEnd::Merged : copy ? TrackEnd::Copy : TrackEnd::Faded);
        }
        for (const auto& track : update.born) open_channel(track, channelizer_.hz_to_bin(track.freq_hz));
        lap(stages_.lifecycle_s);
        if (!oracle_ && config_.decoder.front_end != FrontEnd::Envelope) {
            // Option 1: the detector decides where each channel's station is (its track follows its own
            // peak); the channel's tracker (Matched) is anchored there, or the bank's mixing set there, before
            // its next block.
            for (const auto& t : detector_.tracks()) {
                if (const auto c = channels_.find(t.id); c != channels_.end()) {
                    c->second.detector_freq_hz = t.freq_hz;
                    c->second.detector_snr_db = t.snr_db;
                    c->second.track.snr_db = t.snr_db;  // a track's end reports its last level
                }
            }
        }
        lap(stages_.detector_s);
    };
    for (auto& frame : frames) {
        DetectorUpdate update;
        if (!oracle_ && !detector_spectrum_) update = detector_.process(frame);
        lap(stages_.detector_s);
        if (hiss_) hiss_->push(frame);  // the new decoder's hiss, before the frame moves on
        if (segmental_shared_) band_power_fs2_ = frame.power_fs2;
        lap(stages_.hiss_s);
        bus_.publish(Event{std::move(frame)});
        lap(stages_.publish_s);
        apply(update);
    }
    for (const SpectrumFrame& frame : detector_frames) {
        const DetectorUpdate update = detector_.process(frame);
        lap(stages_.detector_s);
        apply(update);
    }
    const Channelizer::Block block = channelizer_.begin_block(hop);
    lap(stages_.channelizer_s);

    // The channels' work, in any order and on any thread: each job touches only its own Channel and reads what no job
    // writes (the block's spectrum, the hiss estimator, the configuration). The results go out afterward, one thread and
    // in the order of the track ids, whatever the pool did, so the events are what one thread would give. A tap is the
    // benchmark's and may not expect threads.
    active_.clear();
    for (auto& entry : channels_) active_.push_back(&entry);
    if (replay_on_) grant_replays();
    const std::function<void(std::size_t)> job = [&](std::size_t i) { process_channel(active_[i]->first, active_[i]->second, block); };
    if (pool_ && !tap_) {
        pool_->run(active_.size(), job);
    } else {
        for (std::size_t i = 0; i < active_.size(); ++i) job(i);
    }
    lap(stages_.channels_wall_s);
    for (auto* entry : active_) publish_channel(entry->first, entry->second);
    lap(stages_.publish_s);
    if (config_.judge_tracks && !oracle_) judge_channels(block.first_index);
    lap(stages_.lifecycle_s);
}

void Engine::grant_replays() {
    replay_turns_ = std::min(replay_turns_ + replay_per_block_, replay_per_block_ + 1.0);
    for (auto* entry : active_) {
        Channel& channel = entry->second;
        if (!channel.replay_waiting) continue;
        if (replay_turns_ < 1.0) {
            ++channel.replay_wait_blocks;  // by track id, and so the strongest of one block first: the detector's order
            continue;
        }
        replay_turns_ -= 1.0;
        channel.replay_waiting = false;
        channel.replay_now = track_budget_ == 0;  // a pipeline already behind has no time for the past
        longest_replay_wait_blocks_ = std::max(longest_replay_wait_blocks_, channel.replay_wait_blocks);
    }
}

void Engine::replay_channel(std::uint32_t id, Channel& channel, std::size_t blocks) {
    const std::deque<Channelizer::Past>& past = channelizer_.past();
    for (std::size_t i = 0; i < std::min(blocks, past.size()); ++i) {
        DecodeUpdate update = decode_block(id, channel, past[i].block, &past[i]);
        if (heard_blocks_ > 0) {
            // The replay's keying too: its text is attributed as the live text is (the copy test reads live blocks only).
            const double heard = channel.decoder->block_envelope_fs2();
            if (heard >= 0.0) channel.heard.push(past[i].block.first_index / block_samples_, static_cast<float>(heard));
        }
        if (!update.chars.empty() || !update.corrections.empty()) channel.replayed.push_back(std::move(update));
    }
}

void Engine::process_channel(std::uint32_t id, Channel& channel, const Channelizer::Block& block) {
    if (channel.replay_waiting) return;
    if (channel.replay_now) {
        channel.replay_now = false;
        // All but the newest, which is this block.
        replay_channel(id, channel, channelizer_.past().size() - 1);
    }
    channel.update = decode_block(id, channel, block, nullptr);
    if (copy_blocks_ > 0 || heard_blocks_ > 0) {
        // One value a live block, numbered by the block's place in the stream, so that neighbors' values line up.
        const double heard = channel.decoder->block_envelope_fs2();
        if (heard >= 0.0) {
            if (copy_blocks_ > 0) channel.envelope.push(block.first_index / block_samples_, static_cast<float>(heard));
            if (heard_blocks_ > 0) channel.heard.push(block.first_index / block_samples_, static_cast<float>(heard));
        }
        channel.envelope_noise_fs2 = channel.decoder->block_noise_fs2();
    }
}

DecodeUpdate Engine::decode_block(std::uint32_t id, Channel& channel, const Channelizer::Block& block, const Channelizer::Past* past) {
    const double started = thread_cpu_precise_seconds();
    const std::span<const Sample> s = past != nullptr ? channelizer_.cut_past(*past, channel.bin, channel.cut_in, channel.cut_out)
                                                      : channelizer_.cut(block, channel.bin, channel.cut_in, channel.cut_out);
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
    // What stands beside the channel now; a replay has no spectrum of its past.
    if (past == nullptr && !band_power_fs2_.empty()) {
        const double bin_hz = static_cast<double>(config_.sample_rate) / config_.fft_size;
        channel.decoder->set_band_spectrum(band_power_fs2_, static_cast<int>(std::lround(block_center_hz / bin_hz)) + config_.fft_size / 2);
    }
    const double filtered = thread_cpu_precise_seconds();
    DecodeUpdate update = channel.decoder->process(samples, t0);
    const double decoded = thread_cpu_precise_seconds();
    channel.filter_s += filtered - started;
    channel.decoder_s += decoded - filtered;
    (past != nullptr ? channel.replayed_samples : channel.block_samples) += s.size();
    for (const TextCorrection& correction : update.corrections) channel.judge.text_changed_from(correction.from_index);
    // Matched mode: the decoder re-centers on the carrier; report the station there. Bank: the anchor.
    if (update.freq_offset_hz) channel.track.freq_hz = center_hz + *update.freq_offset_hz;
    return update;
}

void Engine::publish_channel(std::uint32_t id, Channel& channel) {
    stages_.filter_s += channel.filter_s;
    stages_.decoder_s += channel.decoder_s;
    track_decoder_s_[id] += channel.decoder_s;
    channel.filter_s = 0;
    channel.decoder_s = 0;
    channel_samples_ += channel.block_samples;
    replayed_samples_ += channel.replayed_samples;
    channel.block_samples = 0;
    channel.replayed_samples = 0;
    for (DecodeUpdate& update : channel.replayed) publish_update(id, std::move(update));
    channel.replayed.clear();
    publish_update(id, std::move(channel.update));
    channel.update = DecodeUpdate{};
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
    channel.replay_waiting = replay_on_;
    channel.detector_snr_db = track.snr_db;
    channel.envelope = EnvelopeHistory(copy_blocks_);
    channel.heard = EnvelopeHistory(heard_blocks_);
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
        if (channel.replay_waiting || now_s < channel.next_judged_s) continue;
        channel.next_judged_s = now_s + 1.0;  // the judge's clock: a second of stream
        if (copy_blocks_ > 0 && is_copy(entry->first, channel, first_index / block_samples_)) {
            // The neighbor's own track reads it; the place stays open for this station's next over.
            TrackJudge::Verdict copy;
            copy.retire = true;
            copy.end = TrackEnd::Copy;
            copy.hold_s = 0.0;
            retiring_.emplace_back(entry->first, copy);
            continue;
        }
        const std::optional<KeyingEvidence> evidence = channel.decoder->keying_evidence();
        if (!evidence) continue;
        const TrackJudge::Verdict verdict = channel.judge.update(*evidence, channel.decoder->published_symbols());
        if (verdict.retire) retiring_.emplace_back(entry->first, verdict);
    }
    for (const auto& [id, verdict] : retiring_) {
        detector_.retire(id, now_s, verdict.hold_s,
                         config_.first_retirement_open == 2 || (config_.first_retirement_open == 1 && verdict.end == TrackEnd::Quiet));
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

bool Engine::is_copy(std::uint32_t id, const Channel& channel, std::uint64_t block) const {
    if (channel.envelope.size() < copy_blocks_ || channel.envelope.newest_block() != block) return false;
    thread_local std::vector<float> weak, strong;
    bool loaded = false;
    for (const auto& [other_id, other] : channels_) {
        // Only an older track's: a runner's track is never the copy of a caller's who, louder for one over, it heard then.
        if (other_id == id || (config_.copy_older_only && other_id > id)) continue;
        if (other.envelope.size() < copy_blocks_ || other.envelope.newest_block() != block) continue;
        if (!(std::abs(other.detector_freq_hz - channel.detector_freq_hz) <= config_.copy_radius_hz)) continue;
        if (!(other.detector_snr_db - channel.detector_snr_db >= config_.copy_level_db)) continue;
        // S must read Morse itself (TrackJudge::proven): a track on a carrier beside the station hears the station too.
        if (config_.copy_source_proven && !other.judge.proven()) continue;
        if (!loaded) {
            channel.envelope.last(copy_blocks_, weak);
            loaded = true;
        }
        other.envelope.last(copy_blocks_, strong);
        const KeyingCoherence k = keying_coherence(weak, strong, detrend_blocks_);
        const double rho = k.rho;
        // W's own power, what neither S's leak nor its hiss explains, re what they do: about 0 for a copy at any SNR, a real station's
        // SNR against leak and hiss for one. Unknown hiss: no copy.
        const double explained = k.gain * k.strong_mean + channel.envelope_noise_fs2;
        const double own_snr = config_.copy_own_snr_max < 0.0           ? -1e300
                               : !(channel.envelope_noise_fs2 > 0.0) ? 1e300
                                                                      : (k.weak_mean - explained) / explained;
        if (rho >= config_.copy_rho && k.gain <= config_.copy_gain_max && own_snr <= config_.copy_own_snr_max) return true;
    }
    return false;
}

void Engine::set_track_budget(std::size_t tracks) {
    track_budget_ = tracks;
    detector_.set_track_limit(tracks > 0 ? tracks : config_.detector.max_tracks);
}

void Engine::close_channel(std::uint32_t id, TrackEnd end) {
    const auto it = channels_.find(id);
    if (it == channels_.end()) return;
    publish_update(id, it->second.decoder->flush());
    bus_.publish(Event{TrackEvent{TrackEvent::Kind::Died, it->second.track, end, it->second.judge.proven()}});
    channels_.erase(it);
}

void Engine::attribute(std::uint32_t id, DecodeUpdate& update) const {
    if (heard_blocks_ == 0 || (update.chars.empty() && update.corrections.empty())) return;
    const Channel& w = channels_.at(id);
    if (w.heard.size() == 0 || !(w.envelope_noise_fs2 > 0.0)) return;  // an unknown hiss: no own power, nothing attributed
    // The neighbors that may own a symbol: within the radius, and reading Morse themselves, so that their own rows show the text.
    thread_local std::vector<std::pair<std::uint32_t, const Channel*>> near;
    near.clear();
    for (const auto& [other_id, other] : channels_) {
        if (other_id == id || other.heard.size() == 0 || (config_.attribute_source_proven && !other.judge.proven())) continue;
        if (!(std::abs(other.detector_freq_hz - w.detector_freq_hz) <= config_.attribute_radius_hz)) continue;
        near.emplace_back(other_id, &other);
    }
    if (near.empty()) return;
    thread_local std::vector<float> weak, strong;
    const auto block_of = [&](double t_s) { return static_cast<std::int64_t>(std::floor(t_s / block_s_)); };
    const auto heard_from = [&](const DecodedSymbol& symbol) -> std::uint32_t {
        if (symbol.text == " ") return 0;  // a word space: the gap between words, whoever's they are
        // The symbol's span and attribute_pad_s either side, within the blocks W holds.
        const std::int64_t oldest = static_cast<std::int64_t>(w.heard.oldest_block());
        const std::int64_t newest = static_cast<std::int64_t>(w.heard.newest_block());
        const std::int64_t first = std::max(oldest, block_of(symbol.start_s - config_.attribute_pad_s));
        const std::int64_t last = std::min(newest, block_of(symbol.end_s + config_.attribute_pad_s));
        if (last < first || static_cast<std::size_t>(last - first + 1) < heard_min_blocks_) return 0;
        if (!w.heard.range(static_cast<std::uint64_t>(first), static_cast<std::uint64_t>(last), weak)) return 0;
        std::uint32_t best = 0;
        KeyingCoherence loudest{0.0, 0.0, 0.0, 0.0, 0.0, -1.0};
        for (const auto& [other_id, other] : near) {
            if (!other->heard.range(static_cast<std::uint64_t>(first), static_cast<std::uint64_t>(last), strong)) continue;
            const KeyingCoherence k = keying_coherence(weak, strong, detrend_blocks_);
            // Coherent keying, heard more strongly on S's channel than on W's: S's station, in W's channel through its skirt.
            if (!(k.rho >= config_.attribute_rho) || !(k.weak_keying < k.strong_keying)) continue;
            // W's own power, what neither S's leak nor its hiss explains, re what they do: about 0 when W's own station is silent.
            const double explained = k.gain * k.strong_mean + w.envelope_noise_fs2;
            if (config_.attribute_own_max >= 0.0 && !((k.weak_mean - explained) / explained <= config_.attribute_own_max)) continue;
            // Of several, the one that hears the keying most strongly: the channel centered nearest its station, its own.
            if (louder_owner(k, loudest)) {
                loudest = k;
                best = other_id;
            }
        }
        return best;
    };
    for (DecodedSymbol& symbol : update.chars) symbol.heard_from = heard_from(symbol);
    for (TextCorrection& correction : update.corrections)
        for (DecodedSymbol& symbol : correction.chars) symbol.heard_from = heard_from(symbol);
}

void Engine::publish_update(std::uint32_t id, DecodeUpdate&& update) {
    if (update.chars.empty() && update.corrections.empty()) return;  // Envelope and Matched: corrections always empty
    attribute(id, update);
    const auto& channel = channels_.at(id);
    bus_.publish(Event{DecodedTextEvent{id, channel.track.freq_hz, std::move(update.chars), update.wpm,
                                        update.confidence, std::move(update.corrections)}});
}

}  // namespace kz4ap
