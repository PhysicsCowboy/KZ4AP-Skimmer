#include "kz4ap/resampler.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>

namespace kz4ap {
namespace {

// The modified Bessel function of the first kind, order 0, by its power series (terms below 1e-12 of the sum dropped).
double bessel_i0(double x) {
    double sum = 1.0, term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 200; ++k) {
        term *= q / (static_cast<double>(k) * k);
        sum += term;
        if (term < 1e-12 * sum) break;
    }
    return sum;
}

}  // namespace

RationalResampler::RationalResampler(int in_rate, int out_rate, double transition_ratio, double attenuation_db) {
    if (in_rate <= 0 || out_rate <= 0) throw std::invalid_argument("resampler rates must be positive (samples/s)");
    if (!(transition_ratio > 0.0 && transition_ratio < 0.5) || !(attenuation_db >= 50.0))
        throw std::invalid_argument("resampler: the transition must be within (0, 0.5) of the lower rate, the attenuation at least 50 dB");
    const int g = std::gcd(in_rate, out_rate);
    up_ = out_rate / g;
    down_ = in_rate / g;
    if (up_ > kMaxFactor || down_ > kMaxFactor)
        throw std::invalid_argument("cannot resample " + std::to_string(in_rate) + " to " + std::to_string(out_rate) +
                                    " samples/s: the ratio " + std::to_string(up_) + "/" + std::to_string(down_) + " is too fine");
    const double fs_up = static_cast<double>(up_) * in_rate;
    const double lower = std::min(in_rate, out_rate);
    const double transition = transition_ratio * lower;  // Hz
    // -6 dB in the transition's middle, so that the stopband starts at half the lower rate: what lies beyond the output's
    // Nyquist (or, rising, the input's images) is attenuated before it can fold into the output band.
    const double cutoff = 0.5 * lower - 0.5 * transition;  // Hz
    // Kaiser's estimates: beta for the attenuation, length for the transition (Oppenheim and Schafer, eqs. 7.62-7.63).
    const double beta = 0.1102 * (attenuation_db - 8.7);
    const int length = static_cast<int>(std::ceil((attenuation_db - 7.95) / (2.285 * 2.0 * std::numbers::pi * transition / fs_up))) + 1;
    taps_ = std::max(1, (length + up_ - 1) / up_);
    const int n = taps_ * up_;
    const double center = (n - 1) / 2.0;
    delay_s_ = center / fs_up;
    std::vector<double> h(static_cast<std::size_t>(n));
    const double i0_beta = bessel_i0(beta);
    for (int i = 0; i < n; ++i) {
        const double t = i - center;
        const double x = 2.0 * cutoff * t / fs_up;
        const double sinc = x == 0.0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
        const double r = center > 0 ? t / center : 0.0;
        const double window = bessel_i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0_beta;
        h[static_cast<std::size_t>(i)] = sinc * window;
    }
    // Phase-major, oldest input first (so that push reads its input forward), each phase scaled to a sum of 1: a constant
    // input gives the same constant out, exactly to rounding.
    phases_.assign(static_cast<std::size_t>(n), 0.0f);
    for (int p = 0; p < up_; ++p) {
        double sum = 0.0;
        for (int k = 0; k < taps_; ++k) sum += h[static_cast<std::size_t>(p + k * up_)];
        for (int k = 0; k < taps_; ++k)
            phases_[static_cast<std::size_t>(p * taps_ + taps_ - 1 - k)] = static_cast<float>(h[static_cast<std::size_t>(p + k * up_)] / sum);
    }
    reset(0);
}

void RationalResampler::reset(std::uint64_t first_output) {
    next_out_ = first_output;
    const auto base = static_cast<std::int64_t>(first_output * static_cast<std::uint64_t>(down_) / static_cast<std::uint64_t>(up_));
    history_start_ = base - (taps_ - 1);
    history_.clear();
    if (history_start_ < 0) history_.assign(static_cast<std::size_t>(-history_start_), Sample(0.0f, 0.0f));
    first_input_ = static_cast<std::uint64_t>(std::max<std::int64_t>(0, history_start_));
}

void RationalResampler::push(std::span<const Sample> in, std::vector<Sample>& out) {
    history_.insert(history_.end(), in.begin(), in.end());
    const std::int64_t end = history_start_ + static_cast<std::int64_t>(history_.size());  // one past the last input held
    const auto up = static_cast<std::uint64_t>(up_), down = static_cast<std::uint64_t>(down_);
    for (;;) {
        const std::uint64_t position = next_out_ * down;
        const auto base = static_cast<std::int64_t>(position / up);
        if (base >= end) break;
        const auto phase = static_cast<std::size_t>(position % up);
        const float* h = phases_.data() + phase * static_cast<std::size_t>(taps_);
        // y = sum over k of h[p + kL] x[base - k], read oldest first; four partial sums, for the processor's pipelines.
        const float* x = reinterpret_cast<const float*>(history_.data() + (base - history_start_ - (taps_ - 1)));
        float re[4] = {0.0f, 0.0f, 0.0f, 0.0f}, im[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        int k = 0;
        for (; k + 4 <= taps_; k += 4)
            for (int j = 0; j < 4; ++j) {
                re[j] += h[k + j] * x[2 * (k + j)];
                im[j] += h[k + j] * x[2 * (k + j) + 1];
            }
        for (; k < taps_; ++k) {
            re[0] += h[k] * x[2 * k];
            im[0] += h[k] * x[2 * k + 1];
        }
        out.emplace_back((re[0] + re[1]) + (re[2] + re[3]), (im[0] + im[1]) + (im[2] + im[3]));
        ++next_out_;
    }
    // Keep what the next output needs: taps_ - 1 samples before its base.
    const auto keep_from = static_cast<std::int64_t>(next_out_ * down / up) - (taps_ - 1);
    if (keep_from > history_start_) {
        const auto drop = static_cast<std::size_t>(std::min<std::int64_t>(keep_from - history_start_, static_cast<std::int64_t>(history_.size())));
        history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(drop));
        history_start_ += static_cast<std::int64_t>(drop);
    }
}

std::uint64_t RationalResampler::outputs_for(std::uint64_t n_in, int up, int down) {
    if (n_in == 0) return 0;
    return (n_in * static_cast<std::uint64_t>(up) - 1) / static_cast<std::uint64_t>(down) + 1;
}

}  // namespace kz4ap
