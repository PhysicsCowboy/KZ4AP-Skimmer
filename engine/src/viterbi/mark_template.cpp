#include "kz4ap/viterbi/mark_template.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace kz4ap::viterbi {

namespace {

bool positive_and_finite(double x) { return x > 0.0 && std::isfinite(x); }

// (pi - 2 acos 0.8) / pi = 0.590334: a raised cosine's 10-90% time as a fraction of its full length (derived).
double ten_ninety() { return (std::numbers::pi - 2.0 * std::acos(0.8)) / std::numbers::pi; }

// The raised-cosine rise of full length edge_s (s) at x seconds after its start: 0 before, 1 after.
double rise_at(double x, double edge_s) {
    return 0.5 - 0.5 * std::cos(std::numbers::pi * std::clamp(x / edge_s, 0.0, 1.0));
}

// Channel samples step_response runs past the first one at or after the response's support (heuristic: room for checks).
constexpr long long kMarginSamples = 8;

// The combined filter at the input rate and its step response: K[p] = sum_q h[q] g[p - q D] (the channel filter, then
// the front filter at every D-th input sample), p = 0 ... |g| - 1 + (|h| - 1) D, and Ks[p] = K[0] + ... + K[p]. The
// output for an input x at input sample n is sum_p K[p] x[n - p]; channel sample j is n = j D.
std::vector<double> combined_step(std::span<const double> g, int decimation, std::span<const double> h) {
    const auto step = static_cast<std::size_t>(decimation);
    std::vector<double> k(g.size() + (h.size() - 1) * step, 0.0);
    for (std::size_t q = 0; q < h.size(); ++q) {
        for (std::size_t l = 0; l < g.size(); ++l) k[q * step + l] += h[q] * g[l];
    }
    double acc = 0.0;
    for (double& x : k) {
        acc += x;
        x = acc;
    }
    return k;
}

// S_r(m / f_s), m = 0 ... count - 1: the response to a raised-cosine rise of full length edge_s (s) starting shift_s (s)
// after input sample 0, from the combined step response ks: S[m] = sum_n d[n] Ks[m - n], d[n] = e[n] - e[n - 1] the
// first difference of the sampled rise e[n] = rise_at(n / f_s - shift_s) (exact: e is the running sum of d), with
// Ks = 0 before its start and its total after its end.
std::vector<double> rise_on_input_grid(const std::vector<double>& ks, double input_rate_hz, double edge_s,
                                       double shift_s, std::size_t count) {
    // d[n] is nonzero for n in [first, last]: e[n] = 0 for n <= floor(shift f_s), 1 for n >= ceil((shift + E) f_s).
    const auto first = static_cast<long long>(std::floor(shift_s * input_rate_hz)) + 1;
    const auto last = static_cast<long long>(std::ceil((shift_s + edge_s) * input_rate_hz));
    const auto length = static_cast<std::size_t>(last - first + 1);
    // dr[i] = d[last - i], so that the sum below runs forward through both arrays.
    std::vector<double> dr(length);
    double previous = rise_at(static_cast<double>(first - 1) / input_rate_hz - shift_s, edge_s);
    for (long long n = first; n <= last; ++n) {
        const double e = rise_at(static_cast<double>(n) / input_rate_hz - shift_s, edge_s);
        dr[static_cast<std::size_t>(last - n)] = e - previous;
        previous = e;
    }
    // padded[t] = Ks[t - last] for t = m - n + last, m in [0, count), n in [first, last]: t in [0, count - 1 + last - first].
    const double total = ks.back();
    std::vector<double> padded(count + length - 1);
    for (std::size_t t = 0; t < padded.size(); ++t) {
        const long long index = static_cast<long long>(t) - last;
        padded[t] = index < 0 ? 0.0 : index >= static_cast<long long>(ks.size()) ? total : ks[static_cast<std::size_t>(index)];
    }
    // S[m] = sum_i dr[i] padded[m + i] (n = last - i, m - n + last = m + i); four partial sums, in a fixed order.
    std::vector<double> s(count);
    for (std::size_t m = 0; m < count; ++m) {
        const double* p = padded.data() + m;
        double a0 = 0.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
        std::size_t i = 0;
        for (; i + 4 <= length; i += 4) {
            a0 += dr[i] * p[i];
            a1 += dr[i + 1] * p[i + 1];
            a2 += dr[i + 2] * p[i + 2];
            a3 += dr[i + 3] * p[i + 3];
        }
        for (; i < length; ++i) a0 += dr[i] * p[i];
        s[m] = (a0 + a1) + (a2 + a3);
    }
    return s;
}

}  // namespace

std::vector<double> step_response(std::span<const double> channel_taps, double input_rate_hz, int decimation,
                                  std::span<const double> front_taps, double rise_10_90_s, double offset_s) {
    if (channel_taps.empty() || front_taps.empty() || !positive_and_finite(input_rate_hz) || decimation < 1 ||
        !positive_and_finite(rise_10_90_s) || !std::isfinite(offset_s)) {
        throw std::invalid_argument(
            "step_response: taps must be non-empty, the rate and rise positive, the decimation at least 1 and the "
            "offset finite");
    }
    const double edge_s = rise_10_90_s / ten_ninety();  // E, s
    const double channel_rate_hz = input_rate_hz / decimation;
    const auto lg = static_cast<long long>(channel_taps.size());
    const auto lh = static_cast<long long>(front_taps.size());
    const long long step = decimation;
    // The support: S = 1 from tau = E + (|g| - 1) / f_s + (|h| - 1) / r on.
    const double support_s = edge_s + static_cast<double>(lg - 1) / input_rate_hz +
                             static_cast<double>(lh - 1) / channel_rate_hz;
    const long long full_from = std::max(0LL, static_cast<long long>(std::ceil(channel_rate_hz * (support_s - offset_s))));
    const long long count = full_from + 1 + kMarginSamples;

    // The input envelope x[n] = rise_at(n / f_s + offset_s): 0 for n <= n0, 1 for n >= n1, the rise in between.
    const auto n0 = static_cast<long long>(std::floor(-offset_s * input_rate_hz));
    const auto n1 = static_cast<long long>(std::ceil((edge_s - offset_s) * input_rate_hz));
    std::vector<double> edge(static_cast<std::size_t>(std::max(0LL, n1 - n0 - 1)));
    for (long long n = n0 + 1; n < n1; ++n) {
        edge[static_cast<std::size_t>(n - n0 - 1)] = rise_at(static_cast<double>(n) / input_rate_hz + offset_s, edge_s);
    }
    // Running sums of the channel taps, for the part of the input at 1.
    std::vector<double> g_sum(channel_taps.size() + 1, 0.0);
    for (std::size_t l = 0; l < channel_taps.size(); ++l) g_sum[l + 1] = g_sum[l] + channel_taps[l];

    // The channel stream x_c[i] = sum_l g[l] x[i D - l] for i = -(|h| - 1) ... count - 1.
    std::vector<double> xc(static_cast<std::size_t>(count + lh - 1));
    for (long long i = -(lh - 1); i < count; ++i) {
        const long long n = i * step;  // input sample of channel sample i
        // l <= n - n1: x = 1; n - n1 < l < n - n0: the rise; l >= n - n0: x = 0.
        const long long ones_end = std::clamp(n - n1 + 1, 0LL, lg);   // l in [0, ones_end): x = 1
        const long long rise_end = std::clamp(n - n0, 0LL, lg);       // l in [ones_end, rise_end): the rise
        double acc = g_sum[static_cast<std::size_t>(ones_end)];
        for (long long l = ones_end; l < rise_end; ++l) {
            acc += channel_taps[static_cast<std::size_t>(l)] * edge[static_cast<std::size_t>(n - l - n0 - 1)];
        }
        xc[static_cast<std::size_t>(i + lh - 1)] = acc;
    }
    // The front filter, causal: y[j] = sum_q h[q] x_c[j - q].
    std::vector<double> y(static_cast<std::size_t>(count));
    for (long long j = 0; j < count; ++j) {
        double acc = 0.0;
        for (long long q = 0; q < lh; ++q) {
            acc += front_taps[static_cast<std::size_t>(q)] * xc[static_cast<std::size_t>(j - q + lh - 1)];
        }
        y[static_cast<std::size_t>(j)] = acc;
    }
    return y;
}

TemplateTables make_template_tables(std::span<const double> channel_taps, double input_rate_hz, int decimation,
                                    std::span<const double> front_taps, int channel_samples_per_sample,
                                    double edge_rise_s, double rise_min_s, double rise_max_s) {
    if (channel_taps.empty() || front_taps.empty() || !positive_and_finite(input_rate_hz) || decimation < 1 ||
        channel_samples_per_sample < 1 || !positive_and_finite(edge_rise_s) || !positive_and_finite(rise_min_s) ||
        !positive_and_finite(rise_max_s) || rise_min_s > rise_max_s) {
        throw std::invalid_argument(
            "make_template_tables: taps must be non-empty, the rate and rises positive, the decimation and spacing at "
            "least 1 and rise_min_s <= rise_max_s");
    }
    constexpr int kSpan = TemplateTables::kSpan;
    const auto positions = static_cast<std::size_t>(channel_samples_per_sample) * static_cast<std::size_t>(decimation);
    const std::size_t count = positions * kSpan;  // input samples of the 16 samples j = 0 ... 15
    const std::vector<double> ks = combined_step(channel_taps, decimation, front_taps);
    const double template_edge_s = edge_rise_s / ten_ninety();

    TemplateTables t{};
    // The template: S(j Delta + u), u = i / f_s, averaged over the positions.
    const std::vector<double> s = rise_on_input_grid(ks, input_rate_hz, template_edge_s, 0.0, count);
    for (int j = 0; j < kSpan; ++j) {
        const double* x = s.data() + static_cast<std::size_t>(j) * positions;
        double sum = 0.0;
        for (std::size_t i = 0; i < positions; ++i) sum += x[i];
        t.s[j] = sum / static_cast<double>(positions);
        double square = 0.0;
        for (std::size_t i = 0; i < positions; ++i) square += (x[i] - t.s[j]) * (x[i] - t.s[j]);
        t.v_position[j] = square / static_cast<double>(positions);
    }
    // The half point: the first upward crossing of 1/2, linearly interpolated between input samples.
    t.half_point_s = std::numeric_limits<double>::quiet_NaN();
    for (std::size_t m = 1; m < s.size(); ++m) {
        if (s[m - 1] < 0.5 && s[m] >= 0.5) {
            t.half_point_s = (static_cast<double>(m - 1) + (0.5 - s[m - 1]) / (s[m] - s[m - 1])) / input_rate_hz;
            break;
        }
    }

    // The rises: each aligned at its half-amplitude point; deviations from the template, averaged.
    std::array<double, kSpan> v_sum{};
    std::array<std::array<double, kSpan>, kSpan> c_sum{};
    for (int r = 0; r < kTemplateRiseCount; ++r) {
        const double rise_s =
            rise_min_s + (rise_max_s - rise_min_s) * static_cast<double>(r) / (kTemplateRiseCount - 1);
        const double edge_s = rise_s / ten_ninety();
        const double shift_s = (template_edge_s - edge_s) / 2.0;
        const std::vector<double> sr = rise_on_input_grid(ks, input_rate_hz, edge_s, shift_s, count);
        std::array<double, kSpan> mean_deviation{};  // m(j, r) - s_j
        for (int j = 0; j < kSpan; ++j) {
            const double* x = sr.data() + static_cast<std::size_t>(j) * positions;
            double sum = 0.0;
            double square = 0.0;
            for (std::size_t i = 0; i < positions; ++i) {
                const double d = x[i] - t.s[j];
                sum += d;
                square += d * d;
            }
            mean_deviation[j] = sum / static_cast<double>(positions);
            v_sum[j] += square / static_cast<double>(positions);
        }
        for (int a = 0; a < kSpan; ++a) {
            for (int b = 0; b < kSpan; ++b) c_sum[a][b] += mean_deviation[a] * mean_deviation[b];
        }
    }
    for (int a = 0; a < kSpan; ++a) {
        t.v[a] = v_sum[a] / kTemplateRiseCount;
        for (int b = 0; b < kSpan; ++b) t.c[a][b] = c_sum[a][b] / kTemplateRiseCount;
    }
    return t;
}

void advance(EdgeHistory& history, bool edge_now) {
    if ((history.recent & 0x8000u) != 0) history.key_down_before = !history.key_down_before;
    history.recent = static_cast<std::uint16_t>((history.recent << 1) | (edge_now ? 1u : 0u));
}

TemplateValue template_value(const EdgeHistory& history, const TemplateTables& tables) {
    const double base = history.key_down_before ? 1.0 : 0.0;
    if (history.recent == 0) return {base, 0.0};
    // The edges from the oldest (bit 15) to the newest (bit 0); the oldest is a key-down unless the key was down.
    std::array<int, TemplateTables::kSpan> age{};
    std::array<double, TemplateTables::kSpan> sign{};
    int n = 0;
    double next = history.key_down_before ? -1.0 : 1.0;
    for (int j = TemplateTables::kSpan - 1; j >= 0; --j) {
        if ((history.recent >> j) & 1u) {
            age[n] = j;
            sign[n] = next;
            next = -next;
            ++n;
        }
    }
    double s = base;
    double v = 0.0;
    for (int e = 0; e < n; ++e) {
        s += sign[e] * tables.s[age[e]];
        v += tables.v[age[e]];
        for (int f = e + 1; f < n; ++f) v += 2.0 * sign[e] * sign[f] * tables.c[age[e]][age[f]];
    }
    return {s, v};
}

std::int64_t claimed_edge_sample(double keyed_time_s, double sample_interval_s) {
    if (!std::isfinite(keyed_time_s) || !positive_and_finite(sample_interval_s)) {
        throw std::invalid_argument("claimed_edge_sample: the time must be finite and the interval positive");
    }
    const double x = keyed_time_s / sample_interval_s;  // samples
    const double nearest = std::round(x);
    // A time within 10^-9 of a sample plus 8 rounding units of x counts as at it (heuristic: t = k Delta rounds).
    const double tolerance = 1e-9 + 8.0 * std::numeric_limits<double>::epsilon() * std::abs(x);
    if (std::abs(x - nearest) <= tolerance) return static_cast<std::int64_t>(nearest);
    return static_cast<std::int64_t>(std::ceil(x));
}

}  // namespace kz4ap::viterbi
