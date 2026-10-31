// The channelizer follows the detector (design Part 1; plan D17; docs/signal-processing.md, section 7 and A.7): for the
// new decoder's channels only (plan O2), a channel is cut at the FFT bin nearest the detector's frequency for its track
// (the coarse step), and its stream is shifted by a phase-continuous oscillator at -(f_det - f_bin) (the fine step), so
// that the decoder receives its station near 0 Hz. Every quantity is in physical units: frequencies in Hz from the
// span's center, rates in samples/s; the bin and the sample appear only where they are used.
//
// THE COARSE STEP. bin_for keeps the channel's bin while |f_det - f_bin| <= bin_hz / 2 (11.71875 Hz at 23.4375 Hz bins:
// 192 000 / 8 192 = 48 000 / 2 048; derived: within half a bin the current bin is the nearest) and otherwise returns
// the bin nearest f_det (Channelizer::hz_to_bin). No hysteresis: a station parked on a boundary may move the channel on
// every block, which the phase rule below makes harmless (tested).
//
// THE FINE STEP. shift multiplies each sample of a block by exp(-j 2 pi phi), phi in cycles advancing by
// (f_det - f_bin) / r per sample, f_det held over the block (plan O3: the detector updates every 21.33 ms, the block's
// own length; an oracle channel's f_det is the label's at the block's start): one complex multiplication per sample,
// the oscillator's phasor computed from phi (cos and sin) so that it carries no rounding from sample to sample.
//
// THE PHASE RULE AT A MOVE. The total demodulating phase seen by a station in the channel is the channelizer's,
// 2 pi f_bin (t - D) at channel time t (Channelizer::set_center_bin: the input is demodulated before the channel filter,
// so a narrow station's phase in the channel is its input phase at t - D, D the filter's group delay), plus 2 pi phi.
// When the bin changes from f_1 to f_2 at a block starting at t (= first_index / r), shift adds (f_1 - f_2) (t - D)
// cycles to phi, so that the total, 2 pi f_bin (t - D) + 2 pi phi, is continuous: the station's phase does not step at
// a move, and between moves it advances by 2 pi f_det / r per sample (derived; tested against an independent reference
// within 0.01 rad, at 48 and 192 kHz). D = (fft_size / 2) / 2 input samples = 1 / (4 bin_hz) s = 10.667 ms (derived:
// the channel filter has fft_size / 2 + 1 symmetric taps, channel_filter_taps). Leaving out D would step the station's
// phase by a quarter turn per bin moved (derived: (f_1 - f_2) D = (bins moved) / 4 cycles).
#pragma once

#include "kz4ap/types.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace kz4ap {

class Channelizer;

class ChannelFollower {
public:
    // channel_rate_hz: the channel's rate r, samples/s; bin_hz: the channelizer's bin width, Hz (sample rate / FFT size).
    // Throws std::invalid_argument unless both are positive and finite.
    ChannelFollower(double channel_rate_hz, double bin_hz);

    // The bin the channel should be cut at for a detector frequency f_det_hz (Hz from the span's center), the channel
    // now on current_bin: current_bin while |f_det - f_bin| <= bin_hz / 2, else channelizer.hz_to_bin(f_det_hz).
    // Throws std::invalid_argument for a frequency that is not finite.
    int bin_for(double f_det_hz, int current_bin, const Channelizer& channelizer) const;

    // Shifts one block of the channel in place: first_index is the index of block[0] in the channel's stream from its
    // start (Channelizer's first_index), f_det_hz the detector's frequency held over the block and f_bin_hz the center
    // of the bin the block was cut at, both Hz from the span's center. When f_bin_hz differs from the previous block's,
    // the phase rule above applies first. The output does not depend on how a stream is split into calls with the same
    // f_det. Throws std::invalid_argument for a frequency that is not finite.
    void shift(std::span<Sample> block, std::uint64_t first_index, double f_det_hz, double f_bin_hz);

    // Starts afresh: the oscillator's phase at 0, no previous bin.
    void reset();

private:
    double rate_hz_;
    double bin_hz_;
    double delay_s_;                  // s: the channel filter's group delay, 1 / (4 bin_hz)
    double phase_cycles_ = 0.0;       // cycles, in [0, 1): phi at the next sample
    std::optional<double> f_bin_hz_;  // Hz: the bin of the previous block
};

}  // namespace kz4ap
