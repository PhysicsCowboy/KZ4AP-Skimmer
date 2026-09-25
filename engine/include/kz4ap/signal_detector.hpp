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
    int min_separation_bins = 3;     // peaks closer than this to a track belong to it
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

    DetectorConfig config_;
    double alpha_;
    std::uint64_t frames_seen_ = 0;
    double first_frame_s_ = 0;
    std::vector<double> average_;  // linear power per bin
    std::vector<Active> active_;
    std::vector<Candidate> candidates_;
    std::uint32_t next_id_ = 1;
};

}  // namespace kz4ap
