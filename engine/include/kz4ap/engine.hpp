#pragma once

#include "kz4ap/bank/bank_config.hpp"
#include "kz4ap/channel_follower.hpp"
#include "kz4ap/channelizer.hpp"
#include "kz4ap/classical_decoder.hpp"
#include "kz4ap/event_bus.hpp"
#include "kz4ap/segmental/config.hpp"
#include "kz4ap/signal_detector.hpp"
#include "kz4ap/spectrum.hpp"
#include "kz4ap/track_evidence.hpp"
#include "kz4ap/viterbi/config.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace kz4ap {

namespace viterbi {
class HissEstimator;
struct SearchStats;
}  // namespace viterbi
class ViterbiShared;
struct SegmentalShared;
class WorkerPool;

// The FFT size the engine uses when EngineConfig::fft_size is 0: the largest power
// of two with sample_rate / fft_size >= 20 Hz, so bins stay about 23 Hz wide at
// 48, 96, 192 or 768 kHz. Throws std::invalid_argument for sample rates below 8000 Hz.
int choose_fft_size(int sample_rate);

// Benchmark tooling (kz4ap-bench --record-channels): one channel block as the channel's decoder receives it, before
// the decoder sees it. Observation only. For Envelope, Matched and the bank it is the channelizer's output at a fixed
// bin; for the new decoder (FrontEnd::Viterbi), whose channel follows the detector (ChannelFollower), it is the
// channelizer's output after the follower's shift, centered on the detector's frequency.
struct ChannelBlock {
    std::uint32_t track_id;
    std::uint64_t first_index;        // index of samples[0] in the channelizer's output stream, from its start
    double center_hz;                 // the frequency at 0 Hz in the samples, Hz from the span's center: the channel's
                                      // FFT bin; for the new decoder the detector's frequency held over the block
    std::span<const Sample> samples;  // the channel at Engine::channel_rate(), FS
    double anchor_hz = 0;             // the detector's current frequency for the track (oracle: the label), Hz from
                                      // the span's center: the anchor the Matched path gives the tracker
};
using ChannelTap = std::function<void(const ChannelBlock&)>;

// One stretch of an oracle channel's frequency (benchmark tooling: the label's truth, design Part 1, plan O3): from
// stream time start_s, s, the carrier is at frequency_hz + drift_hz_per_s x (t - drift_origin_s) at stream time t,
// frequency_hz in Hz from the span's center at drift_origin_s, s, and drift_hz_per_s in Hz/s.
struct OracleSegment {
    double start_s;
    double frequency_hz;
    double drift_hz_per_s;
    double drift_origin_s;
};

// Hz from the span's center: an oracle channel's detector frequency at stream time t_s, s, given its segments (in the
// order of their starts, the first starting at or before t_s): the last segment with start_s <= t_s evaluated at t_s
// (the first segment, if none has started). The engine evaluates it at each channel block's start and holds it over
// the block (plan O3). Throws std::invalid_argument for no segment.
double oracle_frequency_hz(std::span<const OracleSegment> track, double t_s);

struct EngineConfig {
    int sample_rate = 192000;
    int fft_size = 0;  // 0 = choose automatically: the largest power of two with sample_rate / fft_size >= 20 Hz
    int channel_bins = 64;
    double channel_cutoff_hz = 150.0;
    DetectorConfig detector;  // sample_rate, fft_size, hop and attribution_distance_hz (from
                              // channel_distance_hz) are overwritten by the engine
    // decoder.front_end selects the decoder: Envelope, Matched, Bank, Viterbi or Segmental
    ClassicalDecoderConfig decoder;
    bank::BankConfig bank;           // the bank decoder's configuration (decoder.front_end == FrontEnd::Bank)
    // The new decoder's configuration (decoder.front_end == FrontEnd::Viterbi); its hiss_* fields also configure the
    // engine's hiss estimator, which then reads every spectrum frame (in oracle mode too).
    viterbi::ViterbiConfig viterbi;
    // The segmental decoder's configuration (decoder.front_end == FrontEnd::Segmental); `viterbi`'s hiss_* fields configure
    // the hiss estimator for it too.
    segmental::SegmentalConfig segmental;
    // Genie runs only (FrontEnd::Viterbi, oracle mode): empty, or one entry per oracle channel, in the order of
    // oracle_frequencies_hz, whose genie values replace those of `viterbi` for that channel (viterbi::with_genie),
    // since each channel's station has its own true spread and fist.
    std::vector<viterbi::ViterbiGenie> viterbi_genie;
    // FrontEnd::Viterbi only: the path of a default Omega_bar table for viterbi.level_min_db and level_max_db
    // (kz4ap-viterbi-tables omega-bar --lo-db L --hi-db H), or empty for the compiled table, which serves -15 and
    // +85 dB S500 only; another level range without a table for it is refused (make_viterbi_shared).
    std::string viterbi_omega_bar_default_table;

    // Oracle mode, for the benchmark: when not empty, a channel is opened at each of
    // these frequencies (Hz from the span's center, rounded to the nearest FFT bin)
    // from the first sample, with track ids 1, 2, ... in this order, and the signal
    // detector is bypassed: no other track is born and none dies. Each is that channel's
    // detector frequency throughout, unless oracle_frequency_tracks gives the channel a track.
    std::vector<double> oracle_frequencies_hz;
    // Oracle mode with the new decoder (FrontEnd::Viterbi) only: empty, or one track per oracle channel, in the order of
    // oracle_frequencies_hz. A channel's track, when not empty, is its detector frequency (oracle_frequency_hz at each
    // block's start), which its channel follows, and the channel opens at the bin nearest the track's frequency at 0 s;
    // an empty track leaves the channel at its constant oracle frequency. A track's segments are in the order of their
    // starts, the first starting at or before 0 s, every value finite.
    std::vector<std::vector<OracleSegment>> oracle_frequency_tracks;

    // D_ch, Hz (owner decisions 2026-09-29, option 1; Matched path, Attribution::Distance): each detector
    // track follows its own spectral peak within D_ch of its current frequency, and a new peak within
    // D_ch of a track belongs to it; a channel's
    // frequency tracker fine-tunes around the detector's frequency (FrequencyTrackerConfig::fine_tune_hz).
    double channel_distance_hz = 47.0;

    // Threads that run the channels (the channel filter, the decoder), the caller's included, at least 1 (design section
    // 4.1, step 4). The output does not depend on it: a channel's work reads nothing another channel's writes, and the
    // results are published in the order of the track ids once a block's channels are done.
    int worker_threads = 1;

    // FrontEnd::Segmental on the detector's path: each track's evidence is judged once a second and a track without keyed
    // Morse retired (track_evidence.hpp). False for a run that keeps every track as the detector has it.
    bool judge_tracks = true;

    // FrontEnd::Segmental on the detector's path: a new channel's decoder first reads the last replay_s of the band, s, so that it
    // has the signal's start, which the detector was still making up its mind about. 0 for no replay. Heuristic: a keyed
    // signal crosses the threshold within about 1 s and is then 0.5 s a candidate; 2 s measured enough, and costs 2 channel-seconds
    // of decoding a track and about replay_s x 16 bytes x sample_rate of memory (6 MB at 192 kHz: whole spectra, 2 samples of FFT per sample of hop).
    double replay_s = 2.0;
    // The most replays that may start per second of stream, tracks/s: what bounds the burst of CPU when many tracks open in one
    // block. A track past that waits its turn without decoding and then reads the last replay_s all the same, so what it
    // loses is only the wait out of the start of its replay. See docs/signal-processing.md, section 10. Heuristic.
    double replay_tracks_per_s = 100.0;
};

// The milestone-1 pipeline (kept selectable; owner, 2026-09-29): the Envelope decoder and the
// milestone-1 detector rules (frequency fixed at birth, bin attribution). CI (bench/smoke.sh) bounds its
// smoke CER at 0.09 and checks that two runs agree; it does not pin bit-identity with milestone 1. That
// rests on the generator's frozen-copy tests (training/tests/test_generate.py), reading this path, and
// its smoke CER, 34 edits in 964 symbols, unchanged since Task 2 (docs/signal-processing.md, appendix A.11,
// "Smoke check").
inline EngineConfig with_envelope_path(EngineConfig config) {
    config.decoder.front_end = FrontEnd::Envelope;
    config.detector.attribution = Attribution::Bins;
    return config;
}

// Where the pipeline's CPU goes: per stage, s, summed over threads, on each thread's own CPU clock.
// The first six recur whatever the number of tracks (they also grow a little with the
// tracks, as publish_s and lifecycle_s do); filter_s and decoder_s are per channel.
struct EngineStageTimes {
    double spectrum_s = 0;     // wideband spectrum frames: window, FFT, dB
    double detector_s = 0;     // the signal detector, and handing its frequencies to the channels
    double hiss_s = 0;         // the noise estimator's per-frame update
    double publish_s = 0;      // the bus: the subscribers' work on the frames and the text
    double lifecycle_s = 0;    // opening and closing channels (a decoder's construction and its last flush)
    double channelizer_s = 0;  // the channelizer's shared FFT of each block
    double channels_wall_s = 0;  // the caller's own CPU around all the channels of a block: its share of filter_s + decoder_s
    double filter_s = 0;       // one channel's fast-convolution cut, follower shift and noise read, summed over channels
    double decoder_s = 0;      // inside the decoders, summed over channels
};

struct EngineStats {
    std::uint64_t channel_samples = 0;  // channel samples delivered to decoders, summed over channels
    double channel_seconds = 0;         // the same, in s of channel output: live time only, a replay's seconds not counted
    double replayed_channel_seconds = 0;  // what the replays read besides, s of channel output, whose CPU is in the stages below
    double longest_replay_wait_s = 0;   // the longest any replay has waited for its turn (EngineConfig::replay_tracks_per_s), s
    double decoder_seconds = 0;         // CPU inside decoders, s (stages.decoder_s)
    EngineStageTimes stages;
};

// Runs the decoding pipeline synchronously on the caller's thread. Output is
// identical however the input is split across calls to process().
class Engine {
public:
    // Throws std::invalid_argument if config.sample_rate is below 8000 Hz or
    // config.channel_distance_hz is not positive; for viterbi_genie or oracle_frequency_tracks with another decoder
    // than FrontEnd::Viterbi; with FrontEnd::Viterbi, for viterbi_genie entries or oracle tracks without oracle mode or
    // not one per oracle frequency, for an oracle track out of order, starting after 0 s or with a value that is not
    // finite, and for a new decoder's configuration that HissEstimator, make_viterbi_shared or ViterbiDecoder refuses.
    //
    // The new decoder's channels follow the detector (FrontEnd::Viterbi only; plan O2): before each block a channel is
    // shifted by its ChannelFollower to the detector's frequency held over the block (oracle mode: its oracle frequency
    // or track, at the block's start; otherwise the detector's track), then re-binned by ChannelFollower::bin_for from
    // the next block on; the tap and the decoder see the shifted block, the decoder's N0 is read at the bin the block
    // was cut at, and the track's reported frequency is the detector's frequency. The other decoders' channels keep
    // their bin.
    Engine(const EngineConfig& config, EventBus& bus);
    ~Engine();

    void process(std::span<const Sample> samples);

    // End of input: processes buffered samples and flushes partly decoded characters.
    void finish();

    EngineStats stats() const;
    // The most live tracks from now on, for a pipeline that falls behind (the stream runner): the tracks with the least evidence
    // close at the next block (TrackEnd::Shed), those that have read nothing plausible first, and a new one must replace a weaker.
    // The engine's cap (DetectorConfig::max_tracks) is the limit otherwise, and at most. Only with judge_tracks.
    void set_track_budget(std::size_t tracks);
    std::size_t live_tracks() const { return channels_.size(); }
    // CPU of the pool's threads so far, s, not counting the caller's (0 with worker_threads 1).
    double worker_cpu_seconds() const;

    // Benchmark tooling: called with every channel block before the channel's decoder sees it. It
    // observes only; decoding is the same with or without it. Set it before process().
    void set_channel_tap(ChannelTap tap) { tap_ = std::move(tap); }
    double channel_rate() const;  // the channelizer's output rate, samples/s

    // Benchmark tooling: CPU inside each track's decoder so far, s (the thread's own clock), by track id, closed tracks too.
    const std::map<std::uint32_t, double>& track_decoder_seconds() const { return track_decoder_s_; }

    // Benchmark tooling: the statistics of the new decoder's search on the channel with this track id
    // (viterbi/search.hpp; they are empty unless ViterbiConfig::stats is true), or nullptr if no open channel has that
    // id (a channel of the detector path is gone once its track dies) or its decoder is not the new decoder. Read only;
    // the pointer stays valid until the channel closes or the engine is destroyed.
    const viterbi::SearchStats* viterbi_stats(std::uint32_t track_id) const;

private:
    struct Channel {
        Track track;
        int bin;  // the channel's center bin (the new decoder's: the bin of its next block)
        std::unique_ptr<Decoder> decoder;
        double detector_freq_hz = 0;  // the detector's current frequency for this track, Hz from the span's center
        std::optional<ChannelFollower> follower;  // the new decoder's channels only (FrontEnd::Viterbi)
        // One block's work, the channel's own so that channels can run on different threads.
        std::vector<Sample> cut_in;
        std::vector<Sample> cut_out;
        std::vector<Sample> shifted;  // a followed channel's block after its shift
        DecodeUpdate update;          // the block's result, published after all channels' blocks
        double filter_s = 0;
        double decoder_s = 0;
        std::size_t block_samples = 0;
        TrackJudge judge;
        double next_judged_s = 0;  // s of stream
        bool replay_waiting = false;  // opened, its replay not yet started: it decodes nothing
        bool replay_now = false;      // its turn has come: this block's job starts with the replay
        std::uint64_t replay_wait_blocks = 0;
        std::vector<DecodeUpdate> replayed;  // the replay's results, published before the block's
        std::size_t replayed_samples = 0;
    };

    void process_hop(std::span<const Sample> hop);
    void process_channel(std::uint32_t id, Channel& channel, const Channelizer::Block& block);
    // One block through the channel's cut, follower and decoder: the current block, or a past one (replay).
    DecodeUpdate decode_block(std::uint32_t id, Channel& channel, const Channelizer::Block& block, const Channelizer::Past* past);
    // The channel's decoder reads the first `blocks` of the channelizer's past (replay).
    void replay_channel(std::uint32_t id, Channel& channel, std::size_t blocks);
    // Starts the replays whose turn has come (EngineConfig::replay_tracks_per_s).
    void grant_replays();
    // What a channel's blocks did, into the engine's totals, and their text out.
    void publish_channel(std::uint32_t id, Channel& channel);
    void open_channel(const Track& track, int bin);
    // The verdicts on this block's channels (EngineConfig::judge_tracks), once a second of stream for each.
    void judge_channels(std::uint64_t first_index);
    void close_channel(std::uint32_t id, TrackEnd end = TrackEnd::Faded);
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
    EngineStageTimes stages_;
    std::map<std::uint32_t, double> track_decoder_s_;
    std::unique_ptr<WorkerPool> pool_;  // worker_threads > 1 only
    std::vector<std::pair<const std::uint32_t, Channel>*> active_;  // this block's channels, by track id (reused)
    std::vector<std::pair<std::uint32_t, TrackJudge::Verdict>> retiring_;  // this block's verdicts (reused)
    std::size_t track_budget_ = 0;  // 0: none
    bool replay_on_ = false;
    double replay_per_block_ = 0.0;  // replays that may start per block (EngineConfig::replay_tracks_per_s)
    double replay_turns_ = 0.0;      // replays that may start now; at most replay_per_block_ + 1 saved up
    std::uint64_t replayed_samples_ = 0;
    std::uint64_t longest_replay_wait_blocks_ = 0;
    ChannelTap tap_;
    // FrontEnd::Viterbi only: the hiss estimator, fed every spectrum frame, whose N0 at each channel's center bin the
    // channel's decoder gets before every block; and the parts its decoders share.
    std::unique_ptr<viterbi::HissEstimator> hiss_;
    std::vector<float> band_power_fs2_;  // FrontEnd::Segmental only: the latest spectrum frame's power, FS^2 per bin (Decoder::set_band_spectrum)
    std::shared_ptr<const ViterbiShared> viterbi_shared_;
    std::shared_ptr<const SegmentalShared> segmental_shared_;  // FrontEnd::Segmental only
};

}  // namespace kz4ap
