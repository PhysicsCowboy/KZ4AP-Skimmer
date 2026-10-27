#include "kz4ap/viterbi/front_filter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace kz4ap::viterbi {

namespace {

// The decoder's channel rate, samples/s, and the front filter's size there (derived: 2 floor((T + E) r / 2) + 1).
constexpr double kDefaultChannelRateHz = 1500.0;
constexpr std::size_t kDefaultTapCount = 25;

bool positive_and_finite(double x) { return x > 0.0 && std::isfinite(x); }

}  // namespace

std::vector<double> front_filter_taps(double channel_rate_hz, double dot_s, double rise_10_90_s) {
    if (!positive_and_finite(channel_rate_hz) || !positive_and_finite(dot_s) || !positive_and_finite(rise_10_90_s)) {
        throw std::invalid_argument("front_filter_taps: the rate, dot and rise must be positive and finite");
    }
    // (pi - 2 acos 0.8) / pi = 0.590334: a raised cosine's 10-90% time as a fraction of its full length (derived).
    const double ten_ninety = (std::numbers::pi - 2.0 * std::acos(0.8)) / std::numbers::pi;
    const double edge_s = rise_10_90_s / ten_ninety;  // E, s
    const double pulse_s = dot_s + edge_s;            // T + E, s: where the pulse ends
    const double center_s = pulse_s / 2.0;            // t_c, s
    const auto half = static_cast<long long>(std::floor(pulse_s * channel_rate_hz / 2.0));
    const auto count = static_cast<std::size_t>(2 * half + 1);
    if (channel_rate_hz == kDefaultChannelRateHz && count != kDefaultTapCount) {
        throw std::invalid_argument("front_filter_taps: at 1 500 samples/s the front filter has 25 taps");
    }
    auto c = [&](double x) { return 0.5 - 0.5 * std::cos(std::numbers::pi * std::clamp(x / edge_s, 0.0, 1.0)); };
    std::vector<double> taps(count);
    double sum = 0.0;
    for (long long j = -half; j <= half; ++j) {
        const double t = center_s + static_cast<double>(j) / channel_rate_hz;
        const double p = c(t) * (1.0 - c(t - dot_s));
        taps[static_cast<std::size_t>(j + half)] = p;
        sum += p;
    }
    for (double& tap : taps) tap /= sum;
    return taps;
}

OutputNoise output_noise(std::span<const double> channel_taps, double input_rate_hz, int decimation,
                         std::span<const double> front_taps, int channel_samples_per_sample) {
    if (channel_taps.empty() || front_taps.empty() || !positive_and_finite(input_rate_hz) || decimation < 1 ||
        channel_samples_per_sample < 1) {
        throw std::invalid_argument("output_noise: taps must be non-empty; rate, decimation and spacing positive");
    }
    constexpr std::size_t kLags = 7;  // rho_0 ... rho_6
    const auto spacing = static_cast<std::size_t>(channel_samples_per_sample);
    const auto step = static_cast<std::size_t>(decimation);
    // Lags of the channel stream the sum needs: |l + a - b| at most (kLags - 1) spacing + front taps - 1.
    const std::size_t max_lag = (kLags - 1) * spacing + front_taps.size() - 1;
    // R_c[d] / (N0 f_s) = sum_n g[n] g[n + d D], d = 0 ... max_lag (1: the white input has density N0 and power N0 f_s).
    std::vector<double> r_channel(max_lag + 1, 0.0);
    for (std::size_t d = 0; d <= max_lag; ++d) {
        const std::size_t shift = d * step;
        double acc = 0.0;
        for (std::size_t n = 0; n + shift < channel_taps.size(); ++n) acc += channel_taps[n] * channel_taps[n + shift];
        r_channel[d] = acc;
    }
    // R_y[l] / (N0 f_s) = sum_a sum_b h[a] h[b] R_c[|l + a - b|], for l = 0, spacing, ..., 6 spacing.
    std::array<double, kLags> r_front{};
    for (std::size_t m = 0; m < kLags; ++m) {
        const auto l = static_cast<long long>(m * spacing);
        double acc = 0.0;
        for (std::size_t a = 0; a < front_taps.size(); ++a) {
            for (std::size_t b = 0; b < front_taps.size(); ++b) {
                const long long d = l + static_cast<long long>(a) - static_cast<long long>(b);
                acc += front_taps[a] * front_taps[b] * r_channel[static_cast<std::size_t>(std::llabs(d))];
            }
        }
        r_front[m] = acc;
    }
    OutputNoise out{};
    out.sigma2_per_n0_hz = input_rate_hz * r_front[0];  // sigma_y^2 / N0 = f_s x (R_y[0] / (N0 f_s)), Hz
    double sum_linear = 0.0;
    double sum_squared = 0.0;
    for (std::size_t m = 0; m < kLags; ++m) {
        out.rho[m] = r_front[m] / r_front[0];
        if (m > 0) {
            sum_linear += out.rho[m];
            sum_squared += out.rho[m] * out.rho[m];
        }
    }
    out.w_strong = 1.0 / (1.0 + 2.0 * sum_linear);
    out.w_weak = 1.0 / (1.0 + 2.0 * sum_squared);
    return out;
}

EnvelopeSampler::EnvelopeSampler(std::vector<double> taps, int channel_samples_per_sample)
    : taps_(std::move(taps)), spacing_(channel_samples_per_sample) {
    if (taps_.empty() || spacing_ < 1) {
        throw std::invalid_argument("EnvelopeSampler: taps must be non-empty and the spacing at least 1");
    }
    history_.assign(taps_.size() - 1, std::complex<double>{});
}

void EnvelopeSampler::reset() {
    std::fill(history_.begin(), history_.end(), std::complex<double>{});
    has_next_ = false;
}

void EnvelopeSampler::push(std::span<const Sample> block, std::uint64_t first_index, std::vector<EnvelopeSample>& out) {
    if (block.empty()) return;
    if (!has_next_ || first_index != next_index_) {
        std::fill(history_.begin(), history_.end(), std::complex<double>{});
    }
    const std::size_t keep = history_.size();
    work_.resize(keep + block.size());
    std::copy(history_.begin(), history_.end(), work_.begin());
    for (std::size_t i = 0; i < block.size(); ++i) work_[keep + i] = std::complex<double>(block[i]);
    const auto spacing = static_cast<std::uint64_t>(spacing_);
    // The first block index whose absolute index is a multiple of the spacing.
    const std::uint64_t first_offset = (spacing - first_index % spacing) % spacing;
    for (std::uint64_t p = first_offset; p < block.size(); p += spacing) {
        std::complex<double> y = 0.0;
        const std::size_t newest = keep + static_cast<std::size_t>(p);  // work_ index of x[n]
        for (std::size_t i = 0; i < taps_.size(); ++i) y += taps_[i] * work_[newest - i];
        out.push_back({static_cast<std::int64_t>((first_index + p) / spacing), y, std::abs(y)});
    }
    std::copy(work_.end() - static_cast<std::ptrdiff_t>(keep), work_.end(), history_.begin());
    next_index_ = first_index + block.size();
    has_next_ = true;
}

}  // namespace kz4ap::viterbi
