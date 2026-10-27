// The channelizer follows the detector (ChannelFollower; docs/signal-processing.md, section 7 and A.7; design Part 1):
// the coarse move of a channel's bin beyond half a bin, and the fine, phase-continuous shift by the detector's
// frequency held over each 32-sample block (plan O3), through the engine's own channelizer at 48 and 192 kHz.
//
// The phase tests assert continuity, not absolute phase (Ruling 69): with the detector's frequency f_det held from
// each block's start, a drifting or scattered f_det moves the output's phase by design (1.43e-3 rad per block at
// 1 Hz/s, 0.027 rad per block for 0.2 Hz of scatter; derived), so the output is compared with a reference computed
// independently of the follower: the input as the channel carries it, delayed by the channel filter's group delay
// D = 1 / (4 x bin width) s, demodulated by one continuous phase, theta_j = -2 pi f_bin,0 D + 2 pi sum_{i<j} f_det,i / r
// (the channelizer's bin phase at the first sample plus the running sum of the held demodulating frequency, bin plus
// shift, over the samples so far).
#include "kz4ap/channel_follower.hpp"

#include "kz4ap/channelizer.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace kz4ap;

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kBinHz = 23.4375;  // Hz: 192 000 / 8 192 = 48 000 / 2 048 (derived)
constexpr double kRate = 1500.0;    // channel samples/s
constexpr std::size_t kFilled = 32;  // channel samples: the channel filter (21.33 ms) has filled after the first block

// The engine's channelizer at an input rate of 48 or 192 kHz: bins of 23.4375 Hz, 1 500 channel samples/s.
ChannelizerConfig config_at(int rate) {
    ChannelizerConfig config;
    config.sample_rate = rate;
    config.fft_size = rate == 48000 ? 2048 : 8192;
    return config;
}

double frac(double cycles) { return cycles - std::floor(cycles); }

// cycles: the phase of a carrier at frequency f0 + drift x t, Hz from the span's center, at time t, s (from 0 at t = 0).
double carrier_cycles(double f0_hz, double drift_hz_per_s, double t_s) {
    return f0_hz * t_s + 0.5 * drift_hz_per_s * t_s * t_s;
}

// A carrier of amplitude a, FS, at f0 + drift x t Hz from the span's center, at `rate` input samples/s.
std::vector<Sample> carrier(int rate, double duration_s, double f0_hz, double drift_hz_per_s, double a) {
    std::vector<Sample> x(static_cast<std::size_t>(duration_s * rate));
    for (std::size_t n = 0; n < x.size(); ++n) {
        const double angle = kTwoPi * frac(carrier_cycles(f0_hz, drift_hz_per_s, static_cast<double>(n) / rate));
        x[n] = Sample(static_cast<float>(a * std::cos(angle)), static_cast<float>(a * std::sin(angle)));
    }
    return x;
}

// What the channel gave after the follower: every channel sample from index 0, the detector's frequency held over
// each sample's block, Hz from the span's center, and the bin each block was cut at.
struct Followed {
    std::vector<Sample> y;
    std::vector<double> f_det_hz;
    std::vector<int> bins;  // one per block of 32 samples
};

// The channelizer at `config` over x, pushed in pieces of `push` input samples, its channel opened at the bin nearest
// f_det of block 0 and followed as the engine follows it: before each block's samples are used, f_det of the block
// (its index from the stream's start) shifts it, and bin_for moves the channel from the next block on.
Followed follow(const ChannelizerConfig& config, const std::vector<Sample>& x, std::size_t push,
                const std::function<double(std::uint64_t)>& f_det_of_block) {
    Channelizer channelizer(config);
    ChannelFollower follower(channelizer.output_rate(), channelizer.bin_to_hz(1));
    int bin = channelizer.hz_to_bin(f_det_of_block(0));
    channelizer.add_channel(1, bin);
    Followed out;
    std::vector<Sample> block;
    for (std::size_t first = 0; first < x.size(); first += push) {
        const std::size_t count = std::min(push, x.size() - first);
        channelizer.push(std::span<const Sample>(x).subspan(first, count),
                         [&](std::uint32_t, std::uint64_t first_index, std::span<const Sample> s) {
                             const double f_det = f_det_of_block(first_index / 32);
                             block.assign(s.begin(), s.end());
                             follower.shift(block, first_index, f_det, channelizer.bin_to_hz(bin));
                             out.y.insert(out.y.end(), block.begin(), block.end());
                             out.f_det_hz.insert(out.f_det_hz.end(), block.size(), f_det);
                             out.bins.push_back(bin);
                             const int next = follower.bin_for(f_det, bin, channelizer);
                             if (next != bin) {
                                 channelizer.set_center_bin(1, next);
                                 bin = next;
                             }
                         });
    }
    return out;
}

// The reference phasor of each channel sample (see the file's comment): exp(j 2 pi (psi(t_j - D) - theta_j / 2 pi)),
// psi the carrier's phase in cycles, D = 1 / (4 x 23.4375 Hz) = 10.667 ms.
std::vector<std::complex<double>> reference(const Followed& out, double f0_hz, double drift_hz_per_s) {
    const double delay_s = 1.0 / (4.0 * kBinHz);
    std::vector<std::complex<double>> ref(out.y.size());
    double theta_cycles = frac(-kBinHz * out.bins.front() * delay_s);
    for (std::size_t j = 0; j < ref.size(); ++j) {
        const double t_s = static_cast<double>(j) / kRate;
        const double psi = frac(carrier_cycles(f0_hz, drift_hz_per_s, t_s - delay_s));
        ref[j] = std::polar(1.0, kTwoPi * (psi - theta_cycles));
        theta_cycles = frac(theta_cycles + out.f_det_hz[j] / kRate);
    }
    return ref;
}

std::complex<double> wide(Sample s) { return {s.real(), s.imag()}; }

// rad: the output's phase against the reference at sample j.
double phase_error(const Followed& out, const std::vector<std::complex<double>>& ref, std::size_t j) {
    return std::arg(wide(out.y[j]) * std::conj(ref[j]));
}

// rad: the output's phase step from sample j - 1 to sample j beyond the reference's own step there.
double extra_step(const Followed& out, const std::vector<std::complex<double>>& ref, std::size_t j) {
    const double step = std::arg(wide(out.y[j]) * std::conj(wide(out.y[j - 1])));
    const double expected = std::arg(ref[j] * std::conj(ref[j - 1]));
    return std::remainder(step - expected, kTwoPi);
}

// The blocks at which the bin moved (index of the block cut at the new bin).
std::vector<std::size_t> moves(const Followed& out) {
    std::vector<std::size_t> at;
    for (std::size_t k = 1; k < out.bins.size(); ++k)
        if (out.bins[k] != out.bins[k - 1]) at.push_back(k);
    return at;
}

// Standard normal draws from the Mersenne twister's bits alone (Box-Muller), the same on every standard library.
class Gauss {
public:
    explicit Gauss(unsigned seed) : rng_(seed) {}
    double next() {
        constexpr double kScale = 1.0 / 4294967296.0;  // 2^-32
        const double u1 = (static_cast<double>(rng_()) + 1.0) * kScale;  // in (0, 1]
        const double u2 = static_cast<double>(rng_()) * kScale;
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(kTwoPi * u2);
    }

private:
    std::mt19937 rng_;
};

}  // namespace

TEST(ChannelFollower, AStationAtTheDetectorsFrequencyArrivesAt0Hz) {
    // A steady carrier of 0.3 FS at +1 234.5 Hz from the span's center (7.6875 Hz below the center of bin 53,
    // 1 242.1875 Hz), f_det = 1 234.5 Hz, 10 s: after 21.3 ms (the channel filter's length) the output's frequency, from
    // its phase advance over the rest of the 10 s, is within 1e-3 Hz of 0, and its amplitude within 0.1% of the
    // carrier's times the channelizer's gain at the carrier's offset from the bin (computed here from the filter's taps).
    for (const int rate : {48000, 192000}) {
        SCOPED_TRACE(std::to_string(rate) + " samples/s");
        const ChannelizerConfig config = config_at(rate);
        const Followed out = follow(config, carrier(rate, 10.0, 1234.5, 0.0, 0.3), 65536,
                                    [](std::uint64_t) { return 1234.5; });
        ASSERT_GT(out.y.size(), 14000u);
        for (const int bin : out.bins) ASSERT_EQ(bin, 53);
        const std::vector<double> taps = channel_filter_taps(config);
        std::complex<double> response = 0.0;
        const double offset_hz = 1234.5 - 53 * kBinHz;
        for (std::size_t l = 0; l < taps.size(); ++l)
            response += taps[l] * std::polar(1.0, -kTwoPi * offset_hz * static_cast<double>(l) / rate);
        const double gain = std::abs(response);
        double advance_rad = 0.0;
        for (std::size_t j = kFilled; j < out.y.size(); ++j) {
            ASSERT_NEAR(std::abs(wide(out.y[j])) / (0.3 * gain), 1.0, 1e-3) << "sample " << j;
            if (j > kFilled) advance_rad += std::arg(wide(out.y[j]) * std::conj(wide(out.y[j - 1])));
        }
        const double span_s = static_cast<double>(out.y.size() - 1 - kFilled) / kRate;
        EXPECT_LT(std::abs(advance_rad / (kTwoPi * span_s)), 1e-3);
    }
}

TEST(ChannelFollower, MovesTheBinBeyondHalfABin) {
    // f_det from 0 to 60 Hz in steps of 0.01 Hz, the channel starting on bin 0: the bin changes exactly where
    // |f_det - f_bin| first exceeds half a bin, 11.71875 Hz, to the bin nearest f_det: at 11.72 Hz to bin 1
    // (23.4375 Hz), at 35.16 Hz to bin 2 (46.875 Hz), at 58.60 Hz to bin 3 (derived); never anywhere else.
    for (const int rate : {48000, 192000}) {
        SCOPED_TRACE(std::to_string(rate) + " samples/s");
        const Channelizer channelizer(config_at(rate));
        const ChannelFollower follower(channelizer.output_rate(), channelizer.bin_to_hz(1));
        int bin = 0;
        std::vector<std::pair<int, int>> changes;  // (step, new bin)
        for (int k = 0; k <= 6000; ++k) {
            const double f_det = 0.01 * k;
            const double off = std::abs(f_det - channelizer.bin_to_hz(bin));
            const int next = follower.bin_for(f_det, bin, channelizer);
            if (off <= 0.5 * kBinHz) {
                ASSERT_EQ(next, bin) << f_det << " Hz";
            } else {
                ASSERT_EQ(next, static_cast<int>(std::lround(f_det / kBinHz))) << f_det << " Hz";
                changes.emplace_back(k, next);
            }
            bin = next;
        }
        const std::vector<std::pair<int, int>> expected = {{1172, 1}, {3516, 2}, {5860, 3}};
        EXPECT_EQ(changes, expected);
    }
}

TEST(ChannelFollower, AStationParkedOnABinBoundary) {
    // A steady carrier 11.72 Hz above the center of bin 32 (750 Hz), 0.00125 Hz beyond the boundary between bins 32
    // and 33, and f_det scattered about it with a standard deviation of 0.2 Hz per block (the detector's steady
    // scatter), for 60 s: the bin moves on many blocks (on about half of them: the rule has no hysteresis), the output
    // equals the reference within 0.01 rad at every sample, and at no move does its phase step beyond the block's own
    // expected change by more than 0.01 rad: continuity makes the moves harmless.
    constexpr double kStation = 750.0 + 11.72;
    for (const int rate : {48000, 192000}) {
        SCOPED_TRACE(std::to_string(rate) + " samples/s");
        Gauss gauss(17);
        std::vector<double> f_det(60 * 1500 / 32 + 2);
        for (double& f : f_det) f = kStation + 0.2 * gauss.next();
        const Followed out = follow(config_at(rate), carrier(rate, 60.0, kStation, 0.0, 0.3), 65536,
                                    [&](std::uint64_t block) { return f_det.at(block); });
        const std::vector<std::size_t> moved = moves(out);
        EXPECT_GT(moved.size(), 500u);
        const std::vector<std::complex<double>> ref = reference(out, kStation, 0.0);
        double worst = 0.0;
        for (std::size_t j = kFilled; j < out.y.size(); ++j) worst = std::max(worst, std::abs(phase_error(out, ref, j)));
        EXPECT_LT(worst, 0.01);
        double worst_step = 0.0;
        for (const std::size_t k : moved) worst_step = std::max(worst_step, std::abs(extra_step(out, ref, 32 * k)));
        EXPECT_LT(worst_step, 0.01);
        ::testing::Test::RecordProperty("worst_phase_error_rad_" + std::to_string(rate), std::to_string(worst));
    }
}

TEST(ChannelFollower, PhaseIsContinuousAcrossABinMove) {
    // A carrier drifting at 1 Hz/s for 60 s from 5 Hz below the center of bin 32 (745 Hz at 0 s), f_det its true
    // frequency at each block's start: the bin moves twice, to 33 from block 785 (16.747 s) and to 34 from block 1 884
    // (40.192 s), each the block after the first whose f_det lies beyond half a bin (761.71875 and 785.15625 Hz, reached
    // at 16.719 and 40.156 s; derived), the output
    // equals the reference within 0.01 rad at every sample, and at neither move does its phase step beyond the
    // block's own expected change by more than 0.01 rad. The same with the input pushed in pieces of 1, 7, 32 and
    // 1 000 samples: identical output.
    constexpr double kStart = 745.0;
    constexpr double kDrift = 1.0;
    const auto true_frequency = [](std::uint64_t block) {
        return kStart + kDrift * static_cast<double>(block * 32) / kRate;
    };
    for (const int rate : {48000, 192000}) {
        SCOPED_TRACE(std::to_string(rate) + " samples/s");
        const std::vector<Sample> x = carrier(rate, 60.0, kStart, kDrift, 0.3);
        const Followed out = follow(config_at(rate), x, 65536, true_frequency);
        const std::vector<std::size_t> moved = moves(out);
        ASSERT_EQ(moved.size(), 2u);
        EXPECT_EQ(out.bins[moved[0]], 33);
        EXPECT_EQ(out.bins[moved[1]], 34);
        EXPECT_EQ(moved[0], 785u);
        EXPECT_EQ(moved[1], 1884u);
        const std::vector<std::complex<double>> ref = reference(out, kStart, kDrift);
        double worst = 0.0;
        for (std::size_t j = kFilled; j < out.y.size(); ++j) worst = std::max(worst, std::abs(phase_error(out, ref, j)));
        EXPECT_LT(worst, 0.01);
        for (const std::size_t k : moved) EXPECT_LT(std::abs(extra_step(out, ref, 32 * k)), 0.01) << "block " << k;
        if (rate != 48000) continue;
        for (const std::size_t push : {1u, 7u, 32u, 1000u}) {
            SCOPED_TRACE("pushed in pieces of " + std::to_string(push) + " samples");
            const Followed again = follow(config_at(rate), x, push, true_frequency);
            ASSERT_EQ(again.y.size(), out.y.size());
            for (std::size_t j = 0; j < out.y.size(); ++j) ASSERT_EQ(again.y[j], out.y[j]) << "sample " << j;
        }
    }
}

TEST(ChannelFollower, RefusesWhatItCannotUse) {
    // A rate and a bin width that are positive and finite; frequencies that are finite.
    constexpr double kInf = std::numeric_limits<double>::infinity();
    EXPECT_THROW(ChannelFollower(0.0, kBinHz), std::invalid_argument);
    EXPECT_THROW(ChannelFollower(kRate, -1.0), std::invalid_argument);
    EXPECT_THROW(ChannelFollower(kRate, kInf), std::invalid_argument);
    ChannelFollower follower(kRate, kBinHz);
    std::vector<Sample> block(32);
    EXPECT_THROW(follower.shift(block, 0, kInf, 0.0), std::invalid_argument);
    EXPECT_THROW(follower.shift(block, 0, 0.0, std::nan("")), std::invalid_argument);
    const Channelizer channelizer(config_at(48000));
    EXPECT_THROW((void)follower.bin_for(std::nan(""), 0, channelizer), std::invalid_argument);
}

TEST(ChannelFollower, ResetStartsAfresh) {
    // reset forgets the phase and the last bin: the same blocks again give the same output as a new follower.
    ChannelFollower follower(kRate, kBinHz);
    std::vector<Sample> first(32, Sample(1.0f, 0.0f));
    follower.shift(first, 0, 5.0, 0.0);
    std::vector<Sample> moved(32, Sample(1.0f, 0.0f));
    follower.shift(moved, 32, 30.0, kBinHz);
    follower.reset();
    std::vector<Sample> again(32, Sample(1.0f, 0.0f));
    follower.shift(again, 0, 5.0, 0.0);
    EXPECT_EQ(again, first);
}

TEST(Channelizer, SetCenterBinTakesEffectFromTheNextBlock) {
    // A tone on the center of bin 201 at 192 kHz, channels 1 and 2 on bin 201. From inside channel 1's sink at its
    // tenth block, both channels are moved to bin 202: neither's tenth block changes (channel 2's comes after channel
    // 1's in the same block), and from the eleventh on both carry the tone 23.4375 Hz below their center, turning by
    // -2 pi x 23.4375 / 1 500 rad per sample. An id that is no channel is refused.
    const ChannelizerConfig config = config_at(192000);
    Channelizer channelizer(config);
    channelizer.add_channel(1, 201);
    channelizer.add_channel(2, 201);
    std::vector<Sample> x = carrier(192000, 0.5, 201 * kBinHz, 0.0, 0.3);
    std::vector<std::vector<Sample>> y(3);
    channelizer.push(x, [&](std::uint32_t id, std::uint64_t first_index, std::span<const Sample> s) {
        y[id].insert(y[id].end(), s.begin(), s.end());
        if (id == 1 && first_index == 9 * 32) {
            channelizer.set_center_bin(1, 202);
            channelizer.set_center_bin(2, 202);
        }
    });
    for (const std::size_t id : {1u, 2u}) {
        SCOPED_TRACE("channel " + std::to_string(id));
        ASSERT_GT(y[id].size(), 15u * 32u);
        for (std::size_t j = 96; j + 1 < 10 * 32; ++j)
            ASSERT_NEAR(std::arg(wide(y[id][j + 1]) * std::conj(wide(y[id][j]))), 0.0, 1e-3) << j;
        const double step = -kTwoPi * kBinHz / kRate;
        for (std::size_t j = 10 * 32; j + 1 < y[id].size(); ++j)
            ASSERT_NEAR(std::arg(wide(y[id][j + 1]) * std::conj(wide(y[id][j]))), step, 1e-3) << j;
    }
    EXPECT_THROW(channelizer.set_center_bin(3, 5), std::invalid_argument);
}
