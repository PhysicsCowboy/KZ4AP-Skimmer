#pragma once

#include "kz4ap/spectrum.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kz4ap {

struct Track {
    std::uint32_t id = 0;       // starts at 1, never reused
    double freq_hz = 0;         // offset from the center of the SDR span
    float snr_db = 0;           // averaged power above the noise floor, per FFT bin
    double start_time_s = 0;    // when the signal was first seen
    double last_active_s = 0;   // last time it stood above the floor
};

// How tracks keep their frequency and how a new spectral peak is attributed to an existing track.
enum class Attribution {
    Bins,      // milestone 1: a track's frequency is fixed at birth, and a peak less than
               // min_separation_bins from the track's bin belongs to it
    Distance,  // owner decisions 2026-09-29, option 1: each track follows its own peak within
               // attribution_distance_hz of its current frequency, and a peak whose interpolated
               // frequency is within that distance of a track's current frequency belongs to it
};

struct DetectorConfig {
    int sample_rate = 192000;
    int fft_size = 8192;
    int hop = 4096;                  // samples between frames
    float threshold_db = 6.0f;       // a new track needs this much above the noise floor
    float hysteresis_db = 3.0f;      // an existing track stays active down to threshold - hysteresis
    double average_s = 1.0;          // time constant of the per-bin power average; also the warm-up
    double birth_s = 0.5;            // how long a peak must persist to become a track
    double death_s = 10.0;           // how long a track may stay inactive before it dies
    std::size_t max_tracks = 400;    // heuristic: a band's worth of stations; the CPU is the track budget's to guard (StreamRunner)
    int min_separation_bins = 3;     // Attribution::Bins only: peaks closer than this to a track's bin belong to it
    Attribution attribution = Attribution::Distance;
    double attribution_distance_hz = 47.0;  // D_ch, Hz (the engine sets it from EngineConfig::channel_distance_hz)
    // Neighborhoods, Hz, converted to bins at the point of use (std::lround(hz / bin width)); at 23.4 Hz
    // bins they are milestone 1's 2, 1 and 1 bins. The Envelope path's bit-identity to milestone 1
    // rests on that rounding: lround(47 Hz / b) = 2 and lround(23 Hz / b) = 1 for bin widths b from
    // 18.8 to 31.3 Hz, which covers every usual rate (8, 11.025, 32, 44.1, 48, 96, 192 and 768 kHz give
    // 20-31.25 Hz bins with choose_fft_size). At rates whose bins are wider than 31.3 Hz (for example
    // 33-40.9 kHz) the peak neighborhood rounds to 1 bin; nothing rejects such a rate.
    double peak_radius_hz = 47.0;           // a peak must be the maximum within +/- this
    double level_radius_hz = 23.0;          // a track's level is the maximum within +/- this of its bin
    double candidate_step_hz = 23.0;        // a candidate may move this far between frames
    // A new track must also stand threshold_db above the lower quartile of the bins within +/- this, Hz (the design's amendment of
    // 2026-10-19: a threshold against a local floor), which a wide signal's peaks do not. 0 for no such test.
    double local_floor_radius_hz = 0.0;
    // One track to a peak: a track does not move to a peak within attribution_distance_hz of an older track, and of two tracks
    // that end up that close, the younger dies (in DetectorUpdate::merged). Attribution::Distance only.
    bool one_track_per_peak = false;
    // No new track within this of 0 Hz, Hz: an I/Q radio's own DC offset stands there, whatever the band holds. 0 for no such rule.
    double dc_guard_hz = 0.0;
};

struct DetectorUpdate {
    std::vector<Track> born;
    std::vector<std::uint32_t> died;
    std::vector<std::uint32_t> merged;  // those of `died` that left because an older track holds their peak
};

// Finds CW carriers in spectrum frames and keeps a list of tracks.
class SignalDetector {
public:
    explicit SignalDetector(const DetectorConfig& config);

    // Throws std::invalid_argument if the frame does not have fft_size bins.
    DetectorUpdate process(const SpectrumFrame& frame);

    std::vector<Track> tracks() const;

    // The most tracks from now on, at most config.max_tracks; a smaller one closes none, but a new track must then replace a weaker.
    void set_track_limit(std::size_t limit) { track_limit_ = std::min(limit, config_.max_tracks); }

    // A track that its decoder's evidence has retired (track_evidence.hpp): it leaves now, and no new track opens within
    // attribution_distance_hz of its frequency until hold_s after now_s, twice that after a retirement at the same place once the
    // hold before it has ended, and so on, up to kLongestHoldS. hold_s 0: the place stays open. An unknown id is ignored.
    //
    // A wide signal opens track after track at new peaks: at kRegionRetirements such retirements within kRegionRadiusHz of each
    // other in kRegionMemoryS, the whole of that radius closes the same way, from kRegionHoldS. All heuristics, set on real 40 m:
    // FT8 and speech give dozens a minute there, the CW of the same recordings none or one.
    void retire(std::uint32_t id, double now_s, double hold_s);
    static constexpr double kLongestHoldS = 120.0;  // s: an FT8 frequency stays busy; a station there waits at most this.
    static constexpr double kRegionRadiusHz = 750.0;
    static constexpr int kRegionRetirements = 4;
    static constexpr double kRegionMemoryS = 60.0;
    static constexpr double kRegionHoldS = 30.0;

private:
    struct Active {
        Track track;
        int bin;
    };
    struct Candidate {
        int bin;
        double first_seen_s;
        float snr_db;
        bool seen;
    };

    float db_at(int bin) const;  // the average's level at a bin, dB re 1 FS^2 (dBFS)
    double refined_freq(int bin) const;
    bool is_peak(int i) const;
    void follow_peaks(float floor_db);
    bool within_older(std::size_t track, double freq_hz) const;

    // A place closed to new tracks.
    struct Hold {
        double freq_hz;
        double until_s;
        int count;  // holds here so far: retirements, not counting those that fell within a running hold
        double radius_hz;
    };
    std::vector<Hold> holds_;
    std::size_t track_limit_ = 0;  // set in the constructor
    struct Retirement {
        double freq_hz;
        double at_s;
    };
    std::vector<Retirement> retirements_;  // those of the last kRegionMemoryS that closed a place
    void close_place(double freq_hz, double radius_hz, double now_s, double hold_s);

    DetectorConfig config_;
    int peak_bins_ = 2;   // peak_radius_hz in bins
    int level_bins_ = 1;  // level_radius_hz in bins
    int step_bins_ = 1;   // candidate_step_hz in bins
    int local_floor_bins_ = 0;  // local_floor_radius_hz in bins
    double alpha_;
    std::uint64_t frames_seen_ = 0;
    double first_frame_s_ = 0;
    std::vector<double> average_;  // linear power per bin
    std::vector<double> scratch_;  // the median's work (reused)
    std::vector<Active> active_;
    std::vector<Candidate> candidates_;
    std::uint32_t next_id_ = 1;
};

}  // namespace kz4ap
