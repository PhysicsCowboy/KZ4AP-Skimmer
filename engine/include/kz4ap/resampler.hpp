// An exact rational sample-rate change, L/M, by a polyphase windowed-sinc FIR: for recordings at a rate the decoders do not take
// (HDSDR's 250 kHz, to 192 kHz by 96/125). docs/signal-processing.md, section 1 and A.1.
#pragma once

#include "kz4ap/types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace kz4ap {

class RationalResampler {
public:
    // in_rate, out_rate: samples/s, > 0; their ratio reduced to L/M must have L and M at most kMaxFactor. Throws
    // std::invalid_argument otherwise. The filter: Kaiser-windowed sinc at fs_up = L x in_rate whose stopband starts at half
    // the lower rate (the lower rate's Nyquist, so that nothing folds into the output band less than attenuation_db down),
    // with a transition transition_ratio x the lower rate wide below it (-6 dB at its middle). The last two are for
    // experiments; the engine takes the defaults.
    RationalResampler(int in_rate, int out_rate, double transition_ratio = kTransitionRatio, double attenuation_db = kAttenuationDb);

    static constexpr int kMaxFactor = 4096;
    // Of the lower rate: 10 kHz at 192 kHz, so the pass band runs to 86 kHz; heuristic, chosen against its CPU (A.1).
    static constexpr double kTransitionRatio = 10.0 / 192.0;
    // Kaiser's design value for the stopband, dB re the passband: 82 so that at least 80 dB is met (at 96/125, measured: 82.0 dB
    // from 96 kHz on; a design value of 80 gave 79.8). The 80 dB is a heuristic (18 to 45 dB above 16-bit noise, A.1).
    static constexpr double kAttenuationDb = 82.0;

    int up() const { return up_; }
    int down() const { return down_; }
    int taps_per_phase() const { return taps_; }
    // The filter's group delay, s: output sample n stands for the input's time n / out_rate - delay_s().
    double delay_s() const { return delay_s_; }

    // Starts over at output sample `first_output` (0 at the input's first sample); the input is then fed from first_input().
    // Input before sample 0 counts as zero.
    void reset(std::uint64_t first_output);
    std::uint64_t first_input() const { return first_input_; }
    // Takes the next input samples, contiguous with those before, and appends every output they complete.
    void push(std::span<const Sample> in, std::vector<Sample>& out);
    // The outputs from first_output that n_in input samples in all (from sample 0) complete.
    static std::uint64_t outputs_for(std::uint64_t n_in, int up, int down);

private:
    int up_ = 1, down_ = 1, taps_ = 1;
    double delay_s_ = 0.0;
    std::vector<float> phases_;  // taps_ coefficients per phase p, phase-major, oldest input first: h[p + k*up_] at p * taps_ + taps_ - 1 - k
    std::vector<Sample> history_;
    std::int64_t history_start_ = 0;  // input index of history_[0]; negative while zeros stand before the input
    std::uint64_t next_out_ = 0;
    std::uint64_t first_input_ = 0;
};

}  // namespace kz4ap
