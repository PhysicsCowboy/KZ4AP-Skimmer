// Listening to one station of a recording (kz4ap-listen): the signal at one frequency, shifted to an audio tone, as mono
// audio, so that a decoding can be checked by ear. docs/signal-processing.md, A.13.
#pragma once

#include "kz4ap/recording_reader.hpp"
#include "kz4ap/types.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace kz4ap {

struct ListenConfig {
    // The audio's rate, samples/s: 8000 or 12000.
    int audio_rate = 12000;
    // The station's frequency, Hz from the recording's center in the true spectrum (after RecordingOptions::mirror).
    double offset_hz = 0.0;
    // The audio frequency the station's carrier is heard at, Hz.
    double tone_hz = 600.0;
    // The listening filter's width at -6 dB re the passband, Hz, both sides of the station together (listen_filter_taps).
    // Ignored with track_filter.
    double bandwidth_hz = 500.0;
    // The engine's own channel instead of the listening filter: the channelizer's stream (section 7) at the bin nearest
    // offset_hz, shifted by the rest of the offset, interpolated to audio_rate: what a track's decoder reads.
    bool track_filter = false;
};

// The listening filter at audio_rate (samples/s), applied to the complex baseband with the station at 0 Hz: a Kaiser-windowed
// sinc, linear phase, an odd number of real taps normalized to a sum of 1 (unity gain at 0 Hz). Its point 6 dB below the
// passband at +/- bandwidth_hz / 2; its transition kListenTransitionRatio x bandwidth_hz wide, centered there, so the passband
// is |f| <= 0.3 x bandwidth_hz and the stopband |f| >= 0.7 x bandwidth_hz (150 and 350 Hz at 500 Hz); Kaiser's estimates for
// kListenAttenuationDb. Throws std::invalid_argument for a bandwidth outside [kListenMinBandwidthHz, audio_rate / 2].
std::vector<double> listen_filter_taps(double bandwidth_hz, int audio_rate);
// Of the bandwidth: heuristic (the transition's width against the taps, 311 at 12 kHz and 500 Hz).
inline constexpr double kListenTransitionRatio = 0.4;
// Kaiser's design value for the stopband, dB re the passband: 82 so that at least 80 dB is met, as the resampler's (heuristic).
inline constexpr double kListenAttenuationDb = 82.0;
// Narrower filters need more taps than are worth it (3097 at 12 kHz and 50 Hz); heuristic.
inline constexpr double kListenMinBandwidthHz = 50.0;

// The half-width of the band the station's audio occupies around the tone, Hz: the listening filter's stopband edge, 0.7 x
// bandwidth_hz, or the channel filter's (cutoff plus half its transition, about 279 Hz at 192 kHz) with track_filter. The tone
// must be at least this far above 0 Hz and below audio_rate / 2, so that nothing folds when the real part is taken (derived),
// and below the resampler's passband edge, kTransitionRatio x audio_rate under audio_rate / 2 (a heuristic margin).
double listen_half_band_hz(const ListenConfig& config, int input_rate);

// A clip of a recording, in FS, before any gain: audio[k] stands for the recording's time start_s + k / audio_rate (the file's
// first sample at 0 s), the filters' delays taken out, the start's rounding to a whole input sample too (by a fractional delay
// at audio_rate): edges land within 0.04 us of their times (measured; TimeWindowIsExact allows 1 us).
//   audio[k] = Re( z[k] exp(j 2 pi tone_hz k / audio_rate) ),
// z the complex baseband at audio_rate with the station at 0 Hz: without track_filter the input mixed by
// exp(-j 2 pi offset_hz t), resampled to audio_rate (RationalResampler), then the listening filter; with it the channelizer's
// channel at bin c = round(offset_hz / bin width), mixed by exp(-j 2 pi (offset_hz - c x bin width) t), resampled to audio_rate.
// A complex tone of amplitude A FS at offset_hz comes out as a real tone of amplitude A FS at tone_hz. Input before the
// recording's start or past its end counts as zero. Throws std::invalid_argument for a configuration that does not fit
// (listen_half_band_hz, a rate other than 8000 or 12000 samples/s, an offset whose band passes the band the input holds flat:
// fs / 2, or the reader's resampler's passband, a channel rate that is not a whole number of samples/s), a negative start, or a
// duration that is not positive or rounds to no audio sample.
struct ListenClip {
    std::vector<float> audio;      // FS
    std::vector<Sample> baseband;  // z, the same times, FS
    double band_power_fs2 = 0;  // the mean of |z|^2 over the clip, FS^2: the power in the listening band, on the input's scale
};
ListenClip listen_clip(RecordingReader& reader, const ListenConfig& config, double start_s, double duration_s);

// Writes audio x gain as a mono 16-bit PCM WAV at rate samples/s (1 FS = 32768 counts, rounded, clipped to the 16-bit range).
// Returns the number of samples clipped. Throws std::runtime_error if the file cannot be written.
std::uint64_t write_mono_wav16(const std::filesystem::path& path, int rate, std::span<const float> audio, double gain);

}  // namespace kz4ap
