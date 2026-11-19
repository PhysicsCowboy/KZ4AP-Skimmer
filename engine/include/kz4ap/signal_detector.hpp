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
    // frames: how often the band's median, the floor, is taken from the average, one every so many frames. It costs a selection over every bin, and the average moves slowly
    // (average_s). 1 takes it each frame.
    int floor_every_frames = 1;
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
    // A peak (new or followed) must also stand prominence_db above the lowest bin between it and any higher bin within
    // +/- prominence_radius_hz on either side: its topographic prominence within that window, dB re that lowest bin. A
    // weaker station on a stronger one's skirt passes; a ripple of the stronger one's keying sidebands or of the noise on
    // that skirt does not. 0 for no such test.
    float prominence_db = 0.0f;
    double prominence_radius_hz = 47.0;
    // Keying sidebands: a keyed carrier's power spectrum is symmetric about the carrier (its envelope is real), so a peak with
    // a bin at least sideband_db above it within +/- sideband_radius_hz on one side (the highest there, the carrier) whose
    // mirror about that bin (the highest of the mirror bin and its two neighbors) stands no more than sideband_mirror_db below
    // it, is that carrier's sideband and not a station. 0 for no such test.
    float sideband_db = 0.0f;
    float sideband_mirror_db = 3.0f;
    double sideband_radius_hz = 150.0;
    // Attribution::Distance: each frame a track moves to the strongest qualifying peak within this of its frequency, Hz; 0 for
    // attribution_distance_hz (an answering station within the channel distance takes the track).
    double follow_radius_hz = 0.0;
    // The engine's (Engine): the detector reads its own spectrum, of fft_factor times the band's FFT (bins fft_factor
    // times narrower) and a hop of half that FFT; 1 reads the band's frames. A power of two. The detector itself takes
    // fft_size and hop as given.
    int fft_factor = 1;
    // A level-scaled exclusion (a variant, off by default): no track, new or standing, within d(dL) = skirt_exclusion_hz x
    // 10^((dL - skirt_exclusion_db) / 20) Hz of a track dL dB stronger, at most skirt_exclusion_radius_hz: inside a stronger
    // station's skirt, taken to fall 6 dB per doubling of the distance (a keyed carrier's 1/f^2 in power). 0 dB for no such rule.
    float skirt_exclusion_db = 0.0f;
    double skirt_exclusion_hz = 30.0;
    double skirt_exclusion_radius_hz = 150.0;
    // The receiver's passband, Hz from the span's center (the engine sets them from EngineConfig::passband_lo_hz and
    // passband_hi_hz): when band_hi_hz > band_lo_hz, the floor (the median), the local floor and the peaks, new or followed,
    // are taken over the bins whose centers lie within [band_lo_hz, band_hi_hz] only. Otherwise the whole span.
    double band_lo_hz = 0.0;
    double band_hi_hz = 0.0;
};

struct DetectorUpdate {
    std::vector<Track> born;
    std::vector<std::uint32_t> died;
    std::vector<std::uint32_t> merged;  // those of `died` that left because an older track holds their peak
    std::vector<std::uint32_t> copies;  // those of `died` that left inside a stronger track's skirt (skirt_exclusion_db)
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
    //
    // first_open: the first such retirement at a place within kLongestHoldS leaves the place open and counts toward no region;
    // the next one there closes it for hold_s, as a first hold.
    void retire(std::uint32_t id, double now_s, double hold_s, bool first_open = false);
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
    // skirt_exclusion_db: whether a peak at freq_hz of level snr_db lies inside the skirt of a stronger track than `self` (an index
    // into active_, or active_.size() for a new peak).
    bool inside_skirt(std::size_t self, double freq_hz, float snr_db) const;

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
    int prominence_bins_ = 0;   // prominence_radius_hz in bins
    int sideband_bins_ = 0;     // sideband_radius_hz in bins
    int band_first_ = 0;        // the passband's bins, first and last (band_lo_hz, band_hi_hz; else the whole span)
    int band_last_ = 0;
    double alpha_;
    float floor_db_ = 0.0f;  // the last floor taken (floor_every_frames), dB re 1 FS^2 per bin
    std::uint64_t frames_seen_ = 0;
    double first_frame_s_ = 0;
    std::vector<double> average_;  // linear power per bin
    std::vector<double> scratch_;  // the median's work (reused)
    std::vector<Active> active_;
    std::vector<Candidate> candidates_;
    std::uint32_t next_id_ = 1;
};

}  // namespace kz4ap
