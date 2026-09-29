#pragma once

#include "kz4ap/spectrum.hpp"

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
    std::size_t max_tracks = 200;
    int min_separation_bins = 3;     // Attribution::Bins only: peaks closer than this to a track's bin belong to it
    Attribution attribution = Attribution::Distance;
    double attribution_distance_hz = 47.0;  // D, Hz (the engine sets it from EngineConfig::channel_distance_hz)
    // Neighborhoods, Hz, converted to bins at the point of use (std::lround(hz / bin width)); at 23.4 Hz
    // bins they are milestone 1's 2, 1 and 1 bins. The Envelope path's bit-identity to milestone 1
    // rests on that rounding: lround(47 Hz / b) = 2 and lround(23 Hz / b) = 1 for bin widths b from
    // 18.8 to 31.3 Hz, which covers every usual rate (8, 11.025, 32, 44.1, 48, 96, 192 and 768 kHz give
    // 20-31.25 Hz bins with choose_fft_size). At rates whose bins are wider than 31.3 Hz (for example
    // 33-40.9 kHz) the peak neighborhood rounds to 1 bin; nothing rejects such a rate.
    double peak_radius_hz = 47.0;           // a peak must be the maximum within +/- this
    double level_radius_hz = 23.0;          // a track's level is the maximum within +/- this of its bin
    double candidate_step_hz = 23.0;        // a candidate may move this far between frames
};

struct DetectorUpdate {
    std::vector<Track> born;
    std::vector<std::uint32_t> died;
};

// Finds CW carriers in spectrum frames and keeps a list of tracks.
class SignalDetector {
public:
    explicit SignalDetector(const DetectorConfig& config);

    // Throws std::invalid_argument if the frame does not have fft_size bins.
    DetectorUpdate process(const SpectrumFrame& frame);

    std::vector<Track> tracks() const;

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

    double refined_freq(const std::vector<float>& avg_db, int bin) const;
    bool is_peak(const std::vector<float>& avg_db, int i) const;
    void follow_peaks(const std::vector<float>& avg_db, float floor_db);

    DetectorConfig config_;
    int peak_bins_ = 2;   // peak_radius_hz in bins
    int level_bins_ = 1;  // level_radius_hz in bins
    int step_bins_ = 1;   // candidate_step_hz in bins
    double alpha_;
    std::uint64_t frames_seen_ = 0;
    double first_frame_s_ = 0;
    std::vector<double> average_;  // linear power per bin
    std::vector<Active> active_;
    std::vector<Candidate> candidates_;
    std::uint32_t next_id_ = 1;
};

}  // namespace kz4ap
