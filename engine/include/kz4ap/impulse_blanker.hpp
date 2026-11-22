// The impulse blanker (docs/signal-processing.md, section 4, "Impulse blanker", and A.4): finds the moments when power rises at once
// across most of the band's quiet bins, a click or a static crash, and gives the decoders, by channel sample, how much power that
// pulse puts in the band re the band's noise. A decoder then scores the samples the pulse reaches as unknown (no evidence), or takes
// the pulse's power as noise there (ImpulseBlankerConfig::mode). Standard practice, a noise blanker, moved to where the band's
// carriers cannot trigger it: a receiver's blanker compares the wideband amplitude with its average and blanks the whole IF, so
// that a strong station keys it and every channel loses those samples; here the strong stations' bins are taken out first, as
// SM5BSZ's Linrad does before its blanker detects a pulse, and the test is across frequency: a pulse is broadband, a station's key
// clicks are not.
//
// Per analyzer frame (the Hann-windowed FFT of N input samples, every N / 2):
// 1. Occupied bins. Each bin's power is averaged with a one-pole average of average_s. In each subband of subband_hz (a power of
//    two of bins: 512 bins, 12 kHz at 192 kHz) a bin is occupied when its average stands occupied_db or more above the subband's
//    lower quartile of averages, and so is every bin within occupied_guard_bins of it (the Hann main lobe's half width, 2 bins).
// 2. The quiet band in time. The frame's bins, occupied ones zeroed, are inverted subband by subband: B bins give the subband's
//    windowed signal at every N / B-th input sample. Divided by the window, |y|^2 is the quiet part of the subband's power in
//    time, over the window's central half (where the window is 0.5 to 1), which tiles the stream frame after frame.
// 3. Its noise. Per subband, nu = the median, over the last noise_memory_s of frames, of each frame's median of |y|^2 over its
//    central half / ln 2: ln 2 the median of an exponential of mean 1 (derived: |y|^2 of complex Gaussian noise). Not the frame's own
//    value: a crash of 15 ms or more fills most of a central half (21.33 ms) and would be its own noise (ear02-control's, 30 ms).
// 4. The excess. For each channel sample's slice of the input (N / channel_bins input samples), each subband's mean |y|^2 / nu less
//    1, over the subbands with min_quiet_share of their bins quiet (at least min_subbands of them, else nothing is read). A pulse
//    is there when the lower quartile of those excesses reaches `gate`, three quarters of the subbands risen at once, and their upper
//    quartile is at most flat_ratio times the lower: a pulse whose spectrum is flat across the band reads about the same in every
//    subband (for 16 subbands of 8 complex samples each the quartiles stand 1.7 apart at the median, 2.6 at the 99th percentile, by simulation of the Gamma's order statistics; ear02's and ear08's crashes 2.1 to 2.5, measured), a
//    strong station's key clicks, falling as 1 / f^2 from its carrier, far more in the subbands near it. Then u, the pulse's power re the band's noise, is their median; elsewhere 0, nothing
//    reported. From max_run_s into a run of pulses on, 0 too: that is the noise rising for good, which nu takes in within
//    noise_memory_s / 2. Nothing is read in the first noise_memory_s / 2.
#pragma once

#include "kz4ap/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kz4ap {

struct ImpulseBlankerConfig {
    // 0: off, the decoders see nothing of it (the decoder before text-blank-1). 1 (a variant): the segmental decoder scores a sample
    // whose pulse power at its filter's output, P, is at least threshold_db re its noise as unknown, no evidence at all (0 nats key-down
    // and key-up alike). 2 (the default; chosen by measurement, docs/decision-log.md, 2026-11-22): it adds P to that sample's noise
    // instead, so that the pulse is noise there: a gap's sample holds no evidence for a mark, a strong mark's still holds its own.
    int mode = 2;
    double threshold_db = 0.0;     // dB re the sample's noise sigma^2: where the decoder acts on a pulse (heuristic)
    double subband_hz = 12000.0;   // Hz: each subband's width, rounded to a power of two of bins
    int min_subbands = 4;          // subbands with enough quiet bins needed for any reading
    double min_quiet_share = 0.25;  // of a subband's bins quiet, for it to count
    double occupied_db = 6.0;      // dB re the subband's lower quartile of the bins' averages: an occupied bin
    int occupied_guard_bins = 2;   // bins: each side of an occupied bin, occupied too
    double average_s = 1.0;        // s: the bins' average
    double noise_memory_s = 1.0;   // s: the frames whose noise values are medianed
    double gate = 1.0;             // linear, power re the band's noise: the subbands' lower quartile below it reads no pulse
    double max_run_s = 0.2;        // s: a run of pulses longer than this reads 0 from there on
    double flat_ratio = 4.0;       // linear, power: the subbands' upper quartile of excess at most this times their lower, for a pulse
};

// The pulse's power by channel sample: what the decoders read (Decoder::set_impulse_series).
struct ImpulseSeries {
    std::uint64_t first = 0;    // the channel sample of values[0] (absolute index from the stream's start)
    std::vector<float> values;  // u: power re the band's noise, linear, of the input samples [D c, D c + D) of channel sample c (D the decimation); 0: none
    int mode = 0;               // ImpulseBlankerConfig::mode
    double threshold = 1.0;     // ImpulseBlankerConfig::threshold_db, linear
    // One past the last channel sample read so far.
    std::uint64_t end() const { return first + values.size(); }
    // u at channel sample c; 0 outside what is held.
    double at(std::int64_t c) const {
        if (c < static_cast<std::int64_t>(first) || c >= static_cast<std::int64_t>(end())) return 0.0;
        return values[static_cast<std::size_t>(c - static_cast<std::int64_t>(first))];
    }
};

class ImpulseBlanker {
public:
    // sample_rate, fft_size: the analyzer's; decimation: input samples per channel sample; band_lo_hz, band_hi_hz: the receiver's
    // passband (EngineConfig::passband_lo_hz; both 0 for the whole span); keep_s: how much of the series is kept, s (a replay reads
    // the past). Throws std::invalid_argument for a setting outside its range.
    ImpulseBlanker(const ImpulseBlankerConfig& config, int sample_rate, int fft_size, int decimation, double band_lo_hz,
                   double band_hi_hz, double keep_s);

    // Whether the band holds min_subbands subbands at all (not for receiver audio's few kHz): if not, push does nothing and the
    // series stays empty.
    bool active() const { return subbands_ >= config_.min_subbands; }
    // One analyzer frame: its bins in the FFT's order (SpectrumAnalyzer::last_bins), its power lowest frequency first
    // (SpectrumFrame::power_fs2), and the index of the input sample just past it.
    void push(std::span<const Sample> bins, std::span<const float> power_fs2, std::int64_t end_input);
    const ImpulseSeries& series() const { return series_; }

    // Since the first frame: channel samples read, those with a pulse (u above 0), and the runs of them (a firing each).
    std::uint64_t slices() const { return slices_; }
    std::uint64_t fired_slices() const { return fired_slices_; }
    std::uint64_t firings() const { return firings_; }
    int subbands() const { return subbands_; }
    int subband_bins() const { return subband_bins_; }

private:
    ImpulseBlankerConfig config_;
    int n_ = 0;            // FFT size
    int decimation_ = 1;
    int first_bin_ = 0;    // lowest-first index of the first subband's first bin
    int subbands_ = 0;
    int subband_bins_ = 0;  // B
    int step_ = 1;         // input samples between a subband's time samples, N / B
    int per_slice_ = 1;    // a subband's time samples per channel sample, D / step_
    std::size_t memory_ = 1;  // frames in the noise's median
    std::size_t warm_ = 1;    // frames before anything is read: half of memory_
    std::size_t keep_ = 0;    // channel samples kept in the series
    double alpha_ = 1.0;
    bool started_ = false;
    std::vector<double> average_;            // by bin, lowest first
    std::vector<float> inverse_window_sq_;   // 1 / w^2 at a subband's time samples of the central half
    std::vector<std::vector<double>> noise_ring_;  // by subband: the last memory_ frames' values
    std::size_t noise_next_ = 0;
    std::size_t noise_count_ = 0;
    std::vector<Sample> in_, out_;
    std::vector<double> sorted_;
    std::vector<std::uint8_t> occupied_;
    std::vector<std::vector<float>> excess_;  // by subband, by slice of this frame
    std::vector<char> valid_;
    ImpulseSeries series_;
    std::uint64_t slices_ = 0, fired_slices_ = 0, firings_ = 0;
    std::size_t run_ = 0;      // channel samples of the run of pulses going on
    std::size_t max_run_ = 1;  // max_run_s in channel samples
};

}  // namespace kz4ap
