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
    // std::invalid_argument otherwise. The filter: Kaiser-windowed sinc at fs_up = L x in_rate, cut at half the lower rate
    // (-6 dB there), transition kTransitionRatio x the lower rate wide centered on it, kAttenuationDb in the stopband.
    RationalResampler(int in_rate, int out_rate);

    static constexpr int kMaxFactor = 4096;
    static constexpr double kTransitionRatio = 0.104;  // of the lower rate: 20 kHz at 192 kHz; heuristic
    static constexpr double kAttenuationDb = 80.0;     // stopband, dB re the passband; heuristic

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
    std::vector<float> phases_;  // taps_ coefficients per phase p, phase-major: h[p + k*up_] at p * taps_ + k
    std::vector<Sample> history_;
    std::int64_t history_start_ = 0;  // input index of history_[0]; negative while zeros stand before the input
    std::uint64_t next_out_ = 0;
    std::uint64_t first_input_ = 0;
};

}  // namespace kz4ap
