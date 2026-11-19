#include "kz4ap/listen.hpp"

#include "kaiser.hpp"
#include "kz4ap/channelizer.hpp"
#include "kz4ap/engine.hpp"
#include "kz4ap/resampler.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>

namespace kz4ap {
namespace {

// A phase-continuous oscillator exp(j 2 pi f n / rate), its phase kept in cycles in [0, 1) (double), so that it does not drift
// over a long clip.
class Oscillator {
public:
    Oscillator(double frequency_hz, double rate) : step_(frequency_hz / rate - std::floor(frequency_hz / rate)) {}
    Sample next() {
        const double angle = 2.0 * std::numbers::pi * cycles_;
        cycles_ += step_;
        if (cycles_ >= 1.0) cycles_ -= 1.0;
        return Sample(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
    }

private:
    double step_;
    double cycles_ = 0.0;
};

// The station at 0 Hz at the audio rate, from the reader's samples. Output k (counted from the first sample pushed, index 0)
// stands for the input's time k / audio_rate - delay_s(), input index 0 at time 0; every filter is linear-phase, so output k
// rests on input within delay_s() of that time either side.
class Path {
public:
    virtual ~Path() = default;
    virtual void push(std::span<const Sample> in, std::vector<Sample>& out) = 0;
    virtual double delay_s() const = 0;
};

// Mix, resample to the audio rate, listening filter.
class FilterPath final : public Path {
public:
    FilterPath(int input_rate, const ListenConfig& c)
        : mixer_(-c.offset_hz, input_rate), resampler_(input_rate, c.audio_rate) {
        const std::vector<double> h = listen_filter_taps(c.bandwidth_hz, c.audio_rate, c.filter_center_hz ? kReceiverTransitionHz : 0.0);
        taps_.assign(h.begin(), h.end());
        history_.assign(taps_.size() - 1, Sample(0.0f, 0.0f));
        delay_s_ = resampler_.delay_s() + 0.5 * static_cast<double>(taps_.size() - 1) / c.audio_rate;
    }
    void push(std::span<const Sample> in, std::vector<Sample>& out) override {
        mixed_.resize(in.size());
        for (std::size_t i = 0; i < in.size(); ++i) mixed_[i] = in[i] * mixer_.next();
        resampled_.clear();
        resampler_.push(mixed_, resampled_);
        history_.insert(history_.end(), resampled_.begin(), resampled_.end());
        const std::size_t t = taps_.size();
        for (std::size_t i = t - 1; i < history_.size(); ++i) {
            // The taps are symmetric, so their order against the samples does not matter.
            double re = 0.0, im = 0.0;
            const Sample* x = history_.data() + (i - (t - 1));
            for (std::size_t k = 0; k < t; ++k) {
                re += taps_[k] * x[k].real();
                im += taps_[k] * x[k].imag();
            }
            out.emplace_back(static_cast<float>(re), static_cast<float>(im));
        }
        history_.erase(history_.begin(), history_.end() - static_cast<std::ptrdiff_t>(t - 1));
    }
    double delay_s() const override { return delay_s_; }

private:
    Oscillator mixer_;
    RationalResampler resampler_;
    std::vector<double> taps_;
    std::vector<Sample> history_, mixed_, resampled_;
    double delay_s_ = 0.0;
};

// The engine's channel: the channelizer at the bin nearest the offset, the rest of the offset mixed out at the channel's rate
// (as ChannelFollower does for a detector frequency held still), resampled to the audio rate.
class TrackPath final : public Path {
public:
    TrackPath(int input_rate, const ListenConfig& c) : channelizer_(channel_config(input_rate)) {
        const double channel_rate = channelizer_.output_rate();
        if (channel_rate != std::round(channel_rate))
            throw std::invalid_argument("--track-filter needs a channel rate of whole samples/s: " + std::to_string(input_rate) +
                                        " samples/s gives " + std::to_string(channel_rate));
        const int bin = channelizer_.hz_to_bin(c.offset_hz);
        channelizer_.add_channel(0, bin);
        residual_ = std::make_unique<Oscillator>(-(c.offset_hz - channelizer_.bin_to_hz(bin)), channel_rate);
        resampler_ = std::make_unique<RationalResampler>(static_cast<int>(channel_rate), c.audio_rate);
        // The channel filter's group delay: (taps - 1) / 2 = fft_size / 4 input samples (Channelizer::set_center_bin).
        const ChannelizerConfig cc = channel_config(input_rate);
        delay_s_ = resampler_->delay_s() + static_cast<double>(cc.fft_size / 4) / input_rate;
    }
    static ChannelizerConfig channel_config(int input_rate) {
        const EngineConfig e;
        return ChannelizerConfig{input_rate, choose_fft_size(input_rate), e.channel_bins, e.channel_cutoff_hz};
    }
    void push(std::span<const Sample> in, std::vector<Sample>& out) override {
        channel_.clear();
        channelizer_.push(in, [&](std::uint32_t, std::uint64_t, std::span<const Sample> s) {
            for (const Sample& v : s) channel_.push_back(v * residual_->next());
        });
        resampler_->push(channel_, out);
    }
    double delay_s() const override { return delay_s_; }

private:
    Channelizer channelizer_;
    std::unique_ptr<Oscillator> residual_;
    std::unique_ptr<RationalResampler> resampler_;
    std::vector<Sample> channel_;
    double delay_s_ = 0.0;
};

// The fractional delay that takes out the start's rounding to a whole input sample: a Kaiser-windowed sinc of 2M + 1 taps at the
// audio rate, M = kFractionHalf, centered at M + mu audio samples (|mu| <= fa / (2 fs): 0.031 at 192 kHz and 12 kHz, 0.125 at 48 kHz and 12 kHz), the
// window's beta as the listening filter's (8.078), the taps normalized to a sum of 1. For mu = 0 it is the identity. In the
// station's band (|f| <= 350 Hz, 3% of the audio rate at 12 kHz) it is a delay of mu samples to far better than the edge-finding
// can see (tested through TimeWindowIsExact); 16 is heuristic.
constexpr std::uint64_t kFractionHalf = 16;

std::vector<double> fractional_delay_taps(double mu) {
    const int m = static_cast<int>(kFractionHalf);
    const double beta = 0.1102 * (kListenAttenuationDb - 8.7);
    const double i0_beta = detail::bessel_i0(beta);
    std::vector<double> g(static_cast<std::size_t>(2 * m + 1));
    double sum = 0.0;
    for (int i = 0; i <= 2 * m; ++i) {
        const double t = i - m - mu;
        const double sinc = t == 0.0 ? 1.0 : std::sin(std::numbers::pi * t) / (std::numbers::pi * t);
        const double r = t / (m + 1);
        g[static_cast<std::size_t>(i)] = sinc * detail::bessel_i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0_beta;
        sum += g[static_cast<std::size_t>(i)];
    }
    for (double& v : g) v /= sum;
    return g;
}

void put_u16(std::string& b, std::uint32_t v) {
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void put_u32(std::string& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

}  // namespace

std::vector<double> listen_filter_taps(double bandwidth_hz, int audio_rate, double transition_hz) {
    if (audio_rate <= 0 || !(bandwidth_hz >= kListenMinBandwidthHz) || !(bandwidth_hz <= 0.5 * audio_rate))
        throw std::invalid_argument("the listening bandwidth must be between " + std::to_string(kListenMinBandwidthHz) +
                                    " Hz and half the audio rate");
    const double cutoff = 0.5 * bandwidth_hz;                       // Hz, -6 dB re the passband
    const double transition = transition_hz > 0.0 ? transition_hz : kListenTransitionRatio * bandwidth_hz;  // Hz
    const double a = kListenAttenuationDb;
    // Kaiser's estimates (J. F. Kaiser, "Nonrecursive digital filter design using the I0-sinh window function", Proc. IEEE
    // Int. Symp. Circuits and Systems, 1974): beta for the attenuation, length (A - 7.95) / (2.285 x 2 pi x transition / rate)
    // for the transition (Oppenheim and Schafer's textbook form rounds 7.95 to 8); the length made odd
    // so that the delay is a whole number of samples.
    const double beta = 0.1102 * (a - 8.7);
    int length = static_cast<int>(std::ceil((a - 7.95) / (2.285 * 2.0 * std::numbers::pi * transition / audio_rate))) + 1;
    if (length % 2 == 0) ++length;
    const double center = (length - 1) / 2.0;
    const double i0_beta = detail::bessel_i0(beta);
    std::vector<double> h(static_cast<std::size_t>(length));
    double sum = 0.0;
    for (int i = 0; i < length; ++i) {
        const double t = i - center;
        const double x = 2.0 * cutoff * t / audio_rate;
        const double sinc = x == 0.0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
        const double r = t / center;
        h[static_cast<std::size_t>(i)] = sinc * detail::bessel_i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0_beta;
        sum += h[static_cast<std::size_t>(i)];
    }
    for (double& v : h) v /= sum;
    return h;
}

double listen_half_band_hz(const ListenConfig& config, int input_rate) {
    if (!config.track_filter)
        return 0.5 * config.bandwidth_hz +
               0.5 * (config.filter_center_hz ? kReceiverTransitionHz : kListenTransitionRatio * config.bandwidth_hz);
    // The channel filter's transition, about 5.5 x input_rate / taps wide (channel_filter_taps), centered on its cutoff.
    const ChannelizerConfig cc = TrackPath::channel_config(input_rate);
    return cc.cutoff_hz + 0.5 * 5.5 * input_rate / (cc.fft_size / 2 + 1);
}

ListenClip listen_clip(RecordingReader& reader, const ListenConfig& given, double start_s, double duration_s) {
    // A filter centered at C away from the tone: the same path centered on the frequency that comes out at C, offset + (C - tone),
    // heard at C, so that the station at offset comes out at C - (C - tone) = tone (derived).
    ListenConfig config = given;
    if (config.filter_center_hz && !config.track_filter) {
        config.offset_hz = given.offset_hz + (*given.filter_center_hz - given.tone_hz);
        config.tone_hz = *given.filter_center_hz;
    }
    const int fs = reader.sample_rate();
    const int fa = config.audio_rate;
    if (fa != 8000 && fa != 12000) throw std::invalid_argument("the audio rate must be 8000 or 12000 samples/s");
    if (!(start_s >= 0.0) || !std::isfinite(start_s)) throw std::invalid_argument("the start must be at least 0 s");
    if (!(duration_s > 0.0) || !std::isfinite(duration_s)) throw std::invalid_argument("the duration must be positive (s)");
    const double half = listen_half_band_hz(config, fs);
    // The tone's limits. Derived: the band must stay above 0 Hz and below fa / 2, so that nothing folds when the real part is
    // taken. Heuristic, on top: a margin of RationalResampler::kTransitionRatio x fa below fa / 2, the resampler's transition
    // to fa (its flat passband ends there; 625 Hz at 12 kHz), so that the station's band is not on the resampler's slope.
    const double audio_edge = 0.5 * fa - RationalResampler::kTransitionRatio * fa;
    if (!(config.tone_hz >= half) || !(config.tone_hz + half <= audio_edge))
        throw std::invalid_argument("the tone must lie between " + std::to_string(half) + " and " + std::to_string(audio_edge - half) +
                                    " Hz for this bandwidth and audio rate");
    // The band the input holds flat: fs / 2, or, for a recording the reader resampled, the resampler's passband, which ends
    // kTransitionRatio x fs below fs / 2 (86 kHz from the center at 192 kHz, A.1); beyond it the station is attenuated.
    const double input_edge = reader.resampled() ? 0.5 * fs - RationalResampler::kTransitionRatio * fs : 0.5 * fs;
    if (!(std::abs(config.offset_hz) + half <= input_edge))
        throw std::invalid_argument("the frequency lies outside the band the recording holds flat (" + std::to_string(input_edge) +
                                    " Hz either side of its center, less the listening band's half-width): " +
                                    std::to_string(config.offset_hz) + " Hz from its center");
    const auto count = static_cast<std::uint64_t>(std::llround(duration_s * fa));
    if (count == 0)
        throw std::invalid_argument("the clip is shorter than one audio sample (" + std::to_string(1.0 / fa) + " s)");

    std::unique_ptr<Path> path;
    if (config.track_filter) path = std::make_unique<TrackPath>(fs, config);
    else path = std::make_unique<FilterPath>(fs, config);

    // Reader output m stands for the file's time m / fs - d_r (RationalResampler::delay_s). Path output k from reader sample s
    // on stands for reader sample s + (k / fa - d_p) fs. Output K, the clip's first, is to stand for the file's start_s:
    //   s = (start_s + d_r + d_p - K / fa) fs, rounded,
    // with K / fa >= 2 d_p so that output K rests only on input from s on (no zeros of the path's own start).
    const double d_r = reader.resampler() ? reader.resampler()->delay_s() : 0.0;
    const double d_p = path->delay_s();
    const auto lead = std::max<std::uint64_t>(static_cast<std::uint64_t>(std::ceil(2.0 * d_p * fa)) + 1, kFractionHalf);
    const double first_exact = (start_s + d_r + d_p - static_cast<double>(lead) / fa) * fs;
    const auto first = static_cast<std::int64_t>(std::llround(first_exact));
    // Reading from the rounded sample makes output k stand for e = first - first_exact input samples later than its label
    // (|e| <= 1/2): taken out by the fractional delay below, mu = e x fa / fs audio samples.
    const std::vector<double> fraction = fractional_delay_taps((static_cast<double>(first) - first_exact) * fa / fs);
    const std::uint64_t needed = lead + count + kFractionHalf;

    std::vector<Sample> block(16384), baseband;
    baseband.reserve(static_cast<std::size_t>(needed) + 1024);
    if (first < 0) {
        // Before the recording's start: zeros.
        std::vector<Sample> zeros(static_cast<std::size_t>(-first), Sample(0.0f, 0.0f));
        path->push(zeros, baseband);
        reader.seek(0);
    } else {
        reader.seek(static_cast<std::uint64_t>(first));
    }
    while (baseband.size() < needed) {
        std::size_t n = reader.read(block);
        if (n == 0) {
            // Past the recording's end: zeros.
            std::fill(block.begin(), block.end(), Sample(0.0f, 0.0f));
            n = block.size();
        }
        path->push(std::span<const Sample>(block).first(n), baseband);
    }

    ListenClip clip;
    // z'[k] = sum_i g[i] z[k + M - i] = z(k - mu), M = kFractionHalf.
    clip.baseband.resize(static_cast<std::size_t>(count));
    for (std::uint64_t k = 0; k < count; ++k) {
        const Sample* z = baseband.data() + (lead + k + kFractionHalf);
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < fraction.size(); ++i) {
            re += fraction[i] * z[-static_cast<std::ptrdiff_t>(i)].real();
            im += fraction[i] * z[-static_cast<std::ptrdiff_t>(i)].imag();
        }
        clip.baseband[static_cast<std::size_t>(k)] = Sample(static_cast<float>(re), static_cast<float>(im));
    }
    clip.audio.resize(static_cast<std::size_t>(count));
    Oscillator tone(config.tone_hz, fa);
    double power = 0.0;
    for (std::uint64_t k = 0; k < count; ++k) {
        const Sample z = clip.baseband[static_cast<std::size_t>(k)];
        power += std::norm(std::complex<double>(z));
        clip.audio[static_cast<std::size_t>(k)] = (z * tone.next()).real();
    }
    clip.band_power_fs2 = power / static_cast<double>(count);
    return clip;
}

std::uint64_t write_mono_wav16(const std::filesystem::path& path, int rate, std::span<const float> audio, double gain) {
    if (audio.size() * 2 > 0xFFFFFFFFull - 64) throw std::runtime_error("the clip would pass 4 GiB, a WAV's limit");
    const auto data_bytes = static_cast<std::uint32_t>(audio.size() * 2);
    std::string h = "RIFF";
    put_u32(h, 36 + data_bytes);
    h += "WAVEfmt ";
    put_u32(h, 16);
    put_u16(h, 1);  // PCM
    put_u16(h, 1);  // mono
    put_u32(h, static_cast<std::uint32_t>(rate));
    put_u32(h, static_cast<std::uint32_t>(rate) * 2);
    put_u16(h, 2);
    put_u16(h, 16);
    h += "data";
    put_u32(h, data_bytes);
    std::string pcm;
    pcm.reserve(audio.size() * 2);
    std::uint64_t clipped = 0;
    for (const float v : audio) {
        const double c = std::round(static_cast<double>(v) * gain * 32768.0);
        if (c > 32767.0 || c < -32768.0) ++clipped;
        put_u16(pcm, static_cast<std::uint32_t>(static_cast<std::int32_t>(std::clamp(c, -32768.0, 32767.0))) & 0xFFFFu);
    }
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot create " + path_text(path));
    file.write(h.data(), static_cast<std::streamsize>(h.size()));
    file.write(pcm.data(), static_cast<std::streamsize>(pcm.size()));
    if (!file) throw std::runtime_error("cannot write " + path_text(path));
    return clipped;
}

}  // namespace kz4ap
