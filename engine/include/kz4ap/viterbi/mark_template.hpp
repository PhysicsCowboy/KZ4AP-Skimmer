// The new decoder's mark template and edge term (design notes, "Detection, amplitude, frequency and the mark's shape",
// part 5, and part 11's timing reference; plan 1b, Task 4). Library code: the decoder does not run it yet.
//
// A story predicts the front filter's actual output for its claimed keying, as a fraction of its key-down amplitude:
//
// 1. The step response S(tau) (step_response; derived). A carrier keyed down at time t_d with a raised-cosine rise of
//    10-90% time t_r (full length E = t_r / 0.590334, 0.590334 = (pi - 2 acos 0.8) / pi), the rise starting at t_d:
//        x(t) = c(t - t_d),   c(x) = 1/2 - 1/2 cos(pi clip(x / E, 0, 1)),
//    sampled at the input rate f_s (x[n] = x(n / f_s)), through the channel filter g (causal, at f_s, decimated by D to
//    the channel rate r = f_s / D) and then the front filter h (causal, at r):
//        x_c[i] = sum_l g[l] x[i D - l],   y[j] = sum_q h[q] x_c[j - q],
//    so channel sample j lies at input time j / r (the Channelizer's reference: its channel sample of absolute index j is
//    the filter's output at input sample j D, as MarkTemplate.MatchesTheChainOnKeyedCarriers checks). S(tau) = y at
//    tau = j / r - t_d. Both filters have unit gain at 0 Hz and finite support, so S(tau) = 0 for tau <= 0 and S(tau) = 1
//    (to rounding) for tau >= E + (|g| - 1) / f_s + (|h| - 1) / r = 4.235 + 21.333 + 16.000 = 41.57 ms on the default
//    chain (2.5 ms rise, 4 097 taps at 192 kHz or 1 025 at 48 kHz, 25 taps). Half amplitude 20.79 ms after key-down
//    (2.12 ms the edge's half point + 10.67 ms the channel filter's delay + 8.00 ms the front filter's), 10-90% rise
//    10.03 ms, 1-99% rise 14.73 ms (derived; MarkTemplate.StepResponseFigures).
//
// 2. The template s_j (make_template_tables; derived). A story claims each edge at the first sample at or after the
//    keyed edge (claimed_edge_sample; owner, 2026-10-25), so the offset u from the keyed edge to the claimed sample
//    (j = 0) is uniform on [0, Delta), Delta = 4 / r = 2.667 ms the sample interval. s_j is S(j Delta + u) averaged over u
//    on the input-rate grid, u = i / f_s, i = 0 ... P - 1, P = Delta f_s (512 offsets at 192 kHz, 128 at 48 kHz).
//    s_j = 1 exactly for j >= 16 (16 Delta = 42.67 ms >= 41.57 ms, the response's support). On the default chain
//    s_0 ... s_15 = 0.00000, 0.00001, 0.00016, -0.00064, -0.00080 (the channel filter's precursor), 0.0505, 0.2126,
//    0.4334, 0.6590, 0.8661, 0.9825, 1.0023, 0.9999, 0.9999, then within 10^-4 of 1.
//
// 3. The edge term v_j (derived, under the stated distributions). Other transmitters' rises: t_r uniform over
//    [rise_min, rise_max] = 0.8 ... 4.7 ms (the ARRL Lab's measured range; uniform is the maximum-entropy choice given
//    only the range), represented by 79 rise times t_r,i = rise_min + i (rise_max - rise_min) / 78, i = 0 ... 78
//    (0.05 ms apart on the default range), each weighted 1/79. Each rise is aligned to the template at its half-amplitude
//    point: its rise starts delta_r = (E_t - E_r) / 2 after the template's key-down (E_t, E_r the full lengths), the story
//    absorbing the delay in its timing (both filters are symmetric, so the response's half point moves with the edge's).
//    With S_r that aligned response:
//        v_j          = mean over r and u of (S_r(j Delta + u) - s_j)^2,
//        v_position_j = mean over u of (S(j Delta + u) - s_j)^2   (the template's rise alone; for reference),
//        m(j, r)      = mean over u of S_r(j Delta + u),
//        c[a][b]      = mean over r of (m(a, r) - s_a)(m(b, r) - s_b).
//    s_j stays the 2.5 ms template, so v_j is a mean square deviation about s_j, not a variance about the mean over
//    rise times. v_j = c[j][j] + mean over r of the variance over u.
//
// 4. A story's template and edge term (EdgeHistory, template_value; derived). The template of a story's keying is the
//    superposition of its edges' responses, s_k = sum over edges sigma_e S(age_e) (sigma_e = +1 for a key-down, -1 for
//    a key-up; it assumes the carrier's phase continuous across a key-up, which matters only when an element gap is
//    shorter than the response, above about 67 WPM). An edge 16 or more samples old adds exactly sigma_e, so a story
//    keeps only its claimed edges of the last 16 samples (bit j of EdgeHistory::recent: an edge j samples back) and the
//    key state before them (key_down_before); edges alternate, so these give every sign:
//        s = (key_down_before ? 1 : 0) + sum over edges in recent of sigma_e s_{age_e}.
//    The edge term is exact given the assumption that each edge's position u_e within its sample is independent and
//    uniform on [0, Delta), with one rise time shared by the transmitter's edges: the mean square deviation of the
//    combined template about the combined 2.5 ms template, over (u, r) jointly, is
//        v = sum_e v_{age_e} + 2 sum_{e < e'} sigma_e sigma_e' c[age_e][age_e']
//    (given r the positions are independent, so a pair's cross term is the product of their position-averaged
//    deviations, averaged over r). One edge alone gives v_j.
//
// Known limitations: the aligned responses of rises longer than 3.80 ms run past 16 samples (at 4.7 ms to 43.4 ms
// after the template's key-down, against 16 Delta = 42.67 ms); the tables stop at j = 15, dropping a deviation of at
// most 2.3 x 10^-12 of full (measured on the default chain; MarkTemplate.SlowRisesEndWithinTheSpan). The 48 kHz chain's
// s_j differ from 192 kHz's by at most 0.00066 of full (measured; MarkTemplate.At48kHz). Unshaped keying and key clicks
// are outside v (part 13).
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace kz4ap::viterbi {

// S(tau) at the channel samples j = 0 ... N - 1 for a key-down at input time offset_s (s) before channel sample 0 (a
// negative offset puts the key-down after it): element j is S(j / r + offset_s), r = input_rate_hz / decimation
// (stage 1 of the header). channel_taps: the channel filter's taps at input_rate_hz (samples/s; channel_filter_taps),
// listed from the newest input sample's tap; decimation: input samples per channel sample; front_taps: the front
// filter's taps (front_filter_taps); rise_10_90_s: the edge's 10-90% time (s). N runs 8 channel samples past the first
// channel sample at or after the response's support (all of them 1 to rounding); S = 1 at every later sample. Computed
// directly through the two filters. Throws std::invalid_argument for empty taps, a non-positive or non-finite rate or
// rise, a decimation below 1 or a non-finite offset.
std::vector<double> step_response(std::span<const double> channel_taps, double input_rate_hz, int decimation,
                                  std::span<const double> front_taps, double rise_10_90_s, double offset_s);

// The template's tables (stages 2 and 3 of the header), indexed by the age j = 0 ... kSpan - 1 in samples.
struct TemplateTables {
    static constexpr int kSpan = 16;  // samples: s_j = 1 exactly and v_j = 0 from j = kSpan on
    std::array<double, kSpan> s;           // s_j (1)
    std::array<double, kSpan> v;           // v_j: over u and the transmitter's rise time (1)
    std::array<double, kSpan> v_position;  // over u alone, the template's rise (1; for reference)
    std::array<std::array<double, kSpan>, kSpan> c;  // c[a][b], the pair term of two edges at ages a and b (1)
    double half_point_s;                   // s: key-down to the template's half amplitude (20.79 ms on the default)
};

// The rise times the edge term averages over (derived from the stated distribution: 79 points, ends included).
inline constexpr int kTemplateRiseCount = 79;

// The tables for the channel filter's taps at input_rate_hz (samples/s) and decimation, the front filter's taps,
// channel_samples_per_sample channel samples per sample (4), the template's 10-90% rise edge_rise_s (2.5 ms) and the
// rise times' range [rise_min_s, rise_max_s] (0.8 to 4.7 ms) (s). Throws std::invalid_argument for empty taps, a
// non-positive or non-finite rate or time, a decimation or spacing below 1, or rise_min_s > rise_max_s.
TemplateTables make_template_tables(std::span<const double> channel_taps, double input_rate_hz, int decimation,
                                    std::span<const double> front_taps, int channel_samples_per_sample,
                                    double edge_rise_s, double rise_min_s, double rise_max_s);

// A story's claimed edges within the last kSpan samples. Bit j of recent is set when a claimed edge lies j samples back
// (bit 0: this sample); key_down_before is the key state before every edge in recent (the state kSpan samples back).
struct EdgeHistory {
    std::uint16_t recent = 0;
    bool key_down_before = false;
};

// Moves the history on by one sample: every edge ages by one (an edge leaving bit 15 flips key_down_before), then
// edge_now sets bit 0. Called once per sample, before template_value for that sample.
void advance(EdgeHistory& history, bool edge_now);

// A story's template s and edge term v at the current sample (stage 4 of the header; 1 and 1).
struct TemplateValue {
    double s;
    double v;
};
TemplateValue template_value(const EdgeHistory& history, const TemplateTables& tables);

// The claimed edge of a keyed edge at keyed_time_s (s, on the stream's time, t_k = k Delta): the first sample at or after
// it, ceil(t / Delta) (owner, 2026-10-25). A keyed time within 10^-9 of a sample plus 8 rounding units of t / Delta
// (in samples; heuristic, for the rounding of t = k Delta) counts as at that sample. Throws std::invalid_argument for a
// non-finite time or a non-positive or non-finite interval.
std::int64_t claimed_edge_sample(double keyed_time_s, double sample_interval_s);

}  // namespace kz4ap::viterbi
