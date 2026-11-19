// Receiver audio as the engine takes it: a receiver's audio output (real samples, one channel of a WAV file) made into the
// analytic signal at the engine's lowest decoder rate, so that the I/Q chain (spectrum, detector, channels, decoders) runs on
// it unchanged. A tone heard at f Hz is then a complex tone at +f Hz: the engine's frequencies are audio tone frequencies.
// docs/signal-processing.md, section 1 and A.1, "Receiver audio".
#pragma once

#include "kz4ap/resampler.hpp"
#include "kz4ap/types.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace kz4ap {

// The rate the analytic signal comes out at, complex samples/s: 48 000, the lowest of the decoders' rates (decoder_rate_for),
// where choose_fft_size gives 2 048 bins of 23.44 Hz and the channels run at r = 1 500 samples/s as at 192 kHz.
inline constexpr int kAudioEngineRate = 48000;

// The analytic filter (analytic_filter_taps): each transition's full width, Hz, centered on 0 Hz and on the Nyquist frequency,
// so that tones from 100 Hz to 23.9 kHz pass and their images at negative frequencies are stopped (heuristic: below any
// receiver's audio passband, which starts at 200 Hz or more).
inline constexpr double kAnalyticTransitionHz = 200.0;
// Kaiser's design value, dB re the passband: 82, as the resampler's and the listening filter's, so that at least 80 dB is met.
inline constexpr double kAnalyticAttenuationDb = 82.0;

// The receiver's passband the tools assume for receiver audio unless told (EngineConfig::passband_lo_hz, passband_hi_hz), Hz:
// a typical SSB receiver's 2.7 kHz audio passband (heuristic; a narrower CW filter needs its own edges).
inline constexpr double kAudioPassbandLoHz = 300.0;
inline constexpr double kAudioPassbandHiHz = 3000.0;

// The analytic filter's taps at rate (samples/s), complex, oldest first: h[m] = w(t) (sinc(t) + j (1 - cos(pi t)) / (pi t)),
// t = m - center, m = 0 ... 2D + 1, the ideal response 2 for 0 < f < rate / 2 and 0 for -rate / 2 < f < 0 (the real part a
// delay, the imaginary part the Hilbert transform of it) under a Kaiser window w(t) = I0(beta sqrt(1 - (t / (D + 1))^2)) /
// I0(beta), beta = 0.1102 (A - 8.7), D = ceil(Kaiser's length estimate / 2) for transition_hz and A = attenuation_db. center
// is D + center_fraction, center_fraction in [0, 1): a fractional delay, so that a delay before the filter can be made a whole
// number of samples. A real tone of amplitude a FS comes out as a complex tone of amplitude a FS. Throws std::invalid_argument
// for a rate below 1, a transition outside (0, rate / 4), an attenuation below 50 dB or a fraction outside [0, 1).
std::vector<Sample> analytic_filter_taps(int rate, double center_fraction = 0.0, double transition_hz = kAnalyticTransitionHz,
                                         double attenuation_db = kAnalyticAttenuationDb);

// Real audio at audio_rate (samples/s) to the analytic signal at kAudioEngineRate, as a stream: the audio resampled to
// 48 kHz (RationalResampler, unless it is at 48 kHz already), then the analytic filter, by fast convolution (overlap-save).
// The delays are taken out exactly: output k stands for the audio's time k / 48 000 s, the first audio sample at 0 s (the
// analytic filter's center carries the resampler's fraction of a sample). Audio before the first sample counts as zero.
class AudioAnalytic {
public:
    // Throws std::invalid_argument for a rate the resampler refuses (a ratio to 48 000 finer than 4096/4096 once reduced).
    explicit AudioAnalytic(int audio_rate);
    ~AudioAnalytic();

    int audio_rate() const { return audio_rate_; }
    // nullptr at 48 kHz.
    const RationalResampler* resampler() const { return resampler_.get(); }
    int analytic_taps() const { return static_cast<int>(taps_.size()); }
    // The analytic filter's center, samples at 48 kHz, and the whole delay taken out, in those samples.
    double analytic_center() const { return center_; }
    std::uint64_t removed_samples() const { return skip_; }
    int fft_size() const { return fft_size_; }

    // Takes the next audio samples, FS, and appends every output they complete (in blocks of fft_size - taps + 1).
    void push(std::span<const float> audio, std::vector<Sample>& out);
    // The outputs n audio samples stand for: ceil(n x 48 000 / audio_rate).
    static std::uint64_t outputs_for(std::uint64_t n_audio, int audio_rate);

private:
    void run_block(std::vector<Sample>& out);

    int audio_rate_;
    std::unique_ptr<RationalResampler> resampler_;
    std::vector<Sample> taps_;
    double center_ = 0.0;
    std::uint64_t skip_ = 0;      // outputs dropped at the start: the delays
    std::uint64_t produced_ = 0;  // filter outputs so far, dropped ones included
    int fft_size_ = 0;
    std::size_t block_ = 0;       // new samples a block
    std::vector<Sample> response_;  // the taps' DFT over fft_size, divided by fft_size
    std::vector<Sample> buffer_, spectrum_, result_;
    std::vector<Sample> pending_;   // at 48 kHz, after the history of taps - 1 samples
    std::vector<Sample> complex_in_, resampled_;
};

}  // namespace kz4ap
