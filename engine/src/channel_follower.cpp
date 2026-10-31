#include "kz4ap/channel_follower.hpp"

#include "kz4ap/channelizer.hpp"

#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>

namespace kz4ap {

namespace {

bool positive_and_finite(double x) { return x > 0.0 && std::isfinite(x); }

double frac(double cycles) { return cycles - std::floor(cycles); }

}  // namespace

ChannelFollower::ChannelFollower(double channel_rate_hz, double bin_hz)
    : rate_hz_(channel_rate_hz), bin_hz_(bin_hz), delay_s_(1.0 / (4.0 * bin_hz)) {
    if (!positive_and_finite(channel_rate_hz) || !positive_and_finite(bin_hz))
        throw std::invalid_argument("ChannelFollower: the channel rate (samples/s) and the bin width (Hz) must be "
                                    "positive and finite");
}

int ChannelFollower::bin_for(double f_det_hz, int current_bin, const Channelizer& channelizer) const {
    if (!std::isfinite(f_det_hz)) throw std::invalid_argument("ChannelFollower: the detector's frequency must be finite");
    if (std::fabs(f_det_hz - channelizer.bin_to_hz(current_bin)) <= 0.5 * bin_hz_) return current_bin;
    return channelizer.hz_to_bin(f_det_hz);
}

void ChannelFollower::shift(std::span<Sample> block, std::uint64_t first_index, double f_det_hz, double f_bin_hz) {
    if (!std::isfinite(f_det_hz) || !std::isfinite(f_bin_hz))
        throw std::invalid_argument("ChannelFollower: the detector's frequency and the bin's must be finite (Hz)");
    if (f_bin_hz_ && *f_bin_hz_ != f_bin_hz) {
        // The bin moved: keep 2 pi f_bin (t - D) + 2 pi phi continuous at this block's first sample (the header).
        const double t_s = static_cast<double>(first_index) / rate_hz_;
        phase_cycles_ = frac(phase_cycles_ + (*f_bin_hz_ - f_bin_hz) * (t_s - delay_s_));
    }
    f_bin_hz_ = f_bin_hz;
    const double step_cycles = (f_det_hz - f_bin_hz) / rate_hz_;
    for (Sample& y : block) {
        const std::complex<double> turn = std::polar(1.0, -2.0 * std::numbers::pi * phase_cycles_);
        const std::complex<double> out = std::complex<double>(y.real(), y.imag()) * turn;
        y = Sample(static_cast<float>(out.real()), static_cast<float>(out.imag()));
        phase_cycles_ = frac(phase_cycles_ + step_cycles);
    }
}

void ChannelFollower::reset() {
    phase_cycles_ = 0.0;
    f_bin_hz_.reset();
}

}  // namespace kz4ap
