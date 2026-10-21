// The new decoder's hiss estimator: N0, FS^2/Hz, the density of the noise around a channel, read from the spectrum frames
// the engine already makes (design notes, topic 3, item 3; plan section 2.8). It works at channel level, not inside a
// decoder: the engine feeds it every SpectrumFrame, in oracle mode too, where the signal detector does not run, and asks
// it for each channel's N0 before each block. It reimplements the detector's averaging rule and does not touch the
// detector. White noise of total power sigma^2 (FS^2 per complex sample) at the sample rate fs (Hz) has N0 = sigma^2 /
// fs, FS^2/Hz.
//
// A frame, then a query, meet these stages in order.
//
// 1. Reading to power. A SpectrumFrame holds fft_size bins, lowest frequency first, bin i at (i - fft_size / 2) x
//    sample_rate / fft_size Hz, each in dB re 1 FS^2 (a tone of amplitude A reads 20 log10 A). A reading P becomes the
//    power p = 10^(P / 10), FS^2 per bin. A reading at or below -199.99 dB re 1 FS^2 becomes p = 0 (derived from the
//    analyzer's guard): SpectrumAnalyzer computes 10 log10(power + 1e-20), so a bin of exactly zero power reads
//    -200 dB re 1 FS^2, and a power below 2.3e-23 FS^2 (-226 dB re 1 FS^2, far under the quantization noise of any
//    recording) reads within 0.01 dB of that. A frame made by hand with -infinity converts to 0 as well.
//
// 2. Average, per bin. The n-th frame (n = 1 for the first) moves every bin's average toward its p by the step
//        a_n = max(alpha, 1 / n),    average += a_n (p - average),
//    with alpha = 1 - exp(-hop_s / tau) (derived), hop_s = (fft_size / 2) / sample_rate, s, the engine's hop, and
//    tau = hiss_average_s, s. This is the signal detector's rule (engine/src/signal_detector.cpp), a plain running mean
//    while 1 / n exceeds alpha and a one-pole average after: the first frame sets the average, and with the default
//    tau = 1 s (heuristic, the detector's value) and the engine's bins of 23.4375 Hz (hop_s = 21.333 ms at 48 kHz with
//    2 048 bins and at 192 kHz with 8 192), alpha = 0.021107, the mean runs through n = 47 and the pole takes over at
//    n = 48. The estimator keeps one average per bin of the span, FS^2.
//
// 3. The station's neighborhood and its percentile (placeholders, heuristic; design notes, topic 3, item 3). The
//    channelizer's signed bin c, in [-fft_size / 2, fft_size / 2), is bin c + fft_size / 2 of the frame. The neighborhood
//    is the bins within W = floor(hiss_half_width_hz x fft_size / sample_rate) bins of it, clipped at the span's edges:
//    W = 85 bins (+/-1 992 Hz, the floor of 2 000 / 23.4375) and 171 bins, fewer at an edge. Of the c_n bins left, the
//    one of rank r = ceil(hiss_percentile / 100 x c_n) from the smallest is taken, the 43rd of 171 (43 / 172 = 0.25, so
//    that on average the order statistic sits at the lower quartile of its bins' distribution). A bin that stations lift
//    above the rank costs nothing, but each that would have been under it moves the rank up the noise's own distribution:
//    with a quarter of the bins lifted the estimate reads about 2.6% high, with half 7.3% (design notes, derived for
//    independent bins; see "Known limitation").
//
// 4. The bias of the percentile (derived; the Gamma form is an approximation, ours, which the simulation in
//    hiss_test.cpp checks). On noise alone a bin's frame power is exponentially distributed (a bin of the Hann-windowed
//    transform of white noise is a complex Gaussian). Its average over frames, mean mu, has the relative variance
//        v = sum w_i^2 + 2 rho sum w_i w_(i+1),
//    w_i the weights of the average (sum w_i = 1) and rho the correlation of the powers of one bin in successive
//    frames: the window's overlap at the hop fft_size / 2 gives the amplitudes the correlation sum_m w[m] w[m + N / 2] /
//    sum w^2 = (N / 16) / (3 N / 8) = 1/6 for the periodic Hann window, exactly, and a pair of jointly Gaussian
//    complex amplitudes has the powers' correlation the square of theirs, rho = 1/36; frames two hops apart share no
//    samples. For the step a_n of stage 2 the sums follow, frame by frame, from A_0 = C_0 = 0 and a_0 = 0:
//        A_n = (1 - a_n)^2 A_(n-1) + a_n^2,
//        C_n = (1 - a_n)^2 C_(n-1) + (1 - a_n) a_n a_(n-1),
//        v_n = A_n + 2 rho C_n,        (A_1 = 1, C_1 = 0, v_1 = 1)
//    A_n being sum w_i^2 and C_n sum w_i w_(i+1) after n frames. The average is then taken as Gamma distributed with
//    mean mu and relative variance v_n, of shape k = 1 / v_n. The percentile of the bins therefore reads low, by
//        beta = Q(k, p) / k,
//    Q(k, p) the p-quantile of the Gamma distribution of shape k and scale 1, p = hiss_percentile / 100: after 1 frame
//    k = 1, an exponential, and beta = ln(4/3) = 0.2877; after 10, k = 9.52 and beta = 0.7667; after 47, k = 44.6 and
//    beta = 0.8953; in steady state A = alpha / (2 - alpha), C = (1 - alpha) alpha / (2 - alpha), k = 88.92 and beta =
//    0.92658. The estimate is divided by beta. Q comes from the regularized incomplete gamma function (a power series
//    below its argument k + 1, Lentz's continued fraction above), inverted by Newton's method inside a bisection
//    bracket that stops at a step of 1e-13 relative, so better than 1e-12 (tested against 13-digit values from a
//    different algorithm at 1e-10; the plan asks 1e-6). The approximation is checked by simulation on real analyzer
//    frames (hiss_test.cpp: 20 000 frames of white noise, every disjoint window of 171 bins): the 43rd smallest over the
//    mean bin exceeds beta by +0.96% at n = 1, +0.23% at 5, +0.03% at 10, -0.06% at 47 and -0.07% in steady state. The
//    plan asks 0.5% and meets it from n = 5 on; at n = 1 it does not, and the reason is not the arithmetic. beta is the
//    population quantile and the simulation the mean of an order statistic of 171 bins, which sits above it, by +0.34%
//    at n = 1 for independent bins (a numerical integral; 2 000 000 windows of independent exponentials gave +0.32%) and
//    within 0.1% from n = 2 on; and the Hann window's neighboring bins are correlated (4/9 in power between adjacent
//    bins, 1/36 two apart; derived), which adds about +0.6% at n = 1 (40 000 frames gave +0.93% in all: +0.85% at 2,
//    +0.48% at 3). That correlation also widens the percentile's spread: at n = 1 the relative standard deviation of an
//    estimate is 17.7% on real frames against 15.3% for independent bins (scratch; the design notes say about 40% more).
//
// 5. Per hertz. N0 = (the r-th smallest average / beta) / ENBW, with ENBW = 1.5 x sample_rate / fft_size Hz the
//    equivalent noise bandwidth of one bin of the Hann window (derived: fft_size x sum w^2 / (sum w)^2 = 1.5 bins; 35.156
//    Hz at the engine's bins; docs/signal-processing.md, section 4), the width over which the noise density N0 makes one
//    bin's power. N0 = 0 before any frame, and when the selected average is exactly zero (every frame read as exact
//    zeros there): the decoder decodes nothing while N0 = 0.
//
// Values. hiss_average_s = 1 s: heuristic (the detector's). hiss_half_width_hz = 2 000 Hz and hiss_percentile = 25%:
// placeholders, heuristic. hop = fft_size / 2 samples, alpha, the 1/36 and 1/6, ENBW, k, beta and the -199.99 dB re
// 1 FS^2 rule: derived. Nothing here is measured; the tests measure how well it works.
//
// Accuracy on noise alone (hiss_test.cpp, MeasuresTheGeneratorsNoise: the generator's white noise, sigma = 0.02 FS, 8
// realizations pooled, 100 channels each spread over the band): the mean estimate is within 0.25% of sigma^2 / fs at every
// second from 1 s to 6 s at 48 kHz (2 048 bins) and at 192 kHz (8 192 bins), and the rms deviation of the estimates from
// it is 2.0% and 2.1% at 1 s and 1.3% to 1.5% from 2 s on. One realization's own spread is wider (over 40 other
// seeds the rms deviation at 1 s was 1.98% with a standard deviation of 0.34% at 48 kHz, and the mean of the 100
// channels had one of 0.48%), because the 100 neighborhoods overlap.
//
// Limits. The estimate is of white noise: a floor that is not flat across the neighborhood (a passband edge, a slope)
// reads at its lower quartile, not its mean, and the 1 / beta correction does not cover that. It is the average of the
// power: a short burst of interference lifts every bin for about tau. At the span's edge the neighborhood is clipped and
// the rank r of the c bins left sits, on average, at the level r / (c + 1) of their distribution, not at the 25% that
// beta assumes (22 / 87 = 25.3% at the edge's 86 bins, 23 / 90 = 25.6% at 89); that moves the estimate by at most 0.19% in
// steady state (0.27% at n = 47) for 86 to 171 bins (scratch, 2026-10-20), and is not corrected. It is noisy in the
// first second or so: the relative standard deviation of one estimate (one window of 171 bins of real frames; scratch,
// 2026-10-20) is 17.7% after 1 frame, 6.7% after 5, 4.6% after 10 and 2.0% after 47, and the design leaves it to the
// decoder to decide how much to trust it then.
//
// Known limitation (measured). The percentile ignores stations only while they leave most of the neighborhood alone,
// and a strong station does not. A keyed carrier at +30 dB S500 with 5 ms edges (the generator's) stands more than 0.5 dB
// over the noise out to about +/-280 Hz in an 8 s average, 22 to 24 of the 171 bins and 13 to 15 of them more than 3 dB
// over (scratch run, 15 and 25 WPM), so four such stations within +/-2 kHz lift more than half of the neighborhood and the
// estimate reads 7.2% high on average over 8 noise realizations (4.7% to 11.2%; the plan asks at most 5%). One station
// adds 0.6% to the estimate of the same noise alone, two 2.5% and four 8.3% (hiss_test.cpp, ToleratesNeighbors and
// NeighborsLiftTheEstimateByWhatTheirSkirtsOccupy). In scratch runs of one realization the excess grew with the level
// (+2%, +6%, +9%, +13% at +10, +20, +30, +40 dB S500) and was smaller, 4% to 5%, when the four stations sat close together
// or near the window's edges, where their skirts fall outside it. A wider neighborhood helps more than a lower rank
// (scratch, 12 realizations, mean excess of four stations at +30 dB S500: 8.3% at +/-2 kHz and the 25th percentile, 3.8%
// at +/-3 kHz, 2.8% at +/-4 kHz; 5.7% at the 10th percentile and +/-2 kHz, 2.5% with +/-3 kHz), while the design notes
// give the scatter of the 25th percentile as +/-2.2%, +/-1.6%, +/-1.1% and +/-0.7% over +/-0.5, 1, 2 and 5 kHz. The
// range and the rank are placeholders to be set by measurement on real recordings (design notes, topic 3, item 3).
#pragma once

#include "kz4ap/spectrum.hpp"
#include "kz4ap/viterbi/config.hpp"

#include <cstdint>
#include <vector>

namespace kz4ap::viterbi {

class HissEstimator {
public:
    // sample_rate, Hz, and fft_size, bins, are those of the spectrum frames (SpectrumAnalyzer(sample_rate, fft_size,
    // fft_size / 2): the hop is half the transform); the configuration's hiss_half_width_hz, hiss_percentile and
    // hiss_average_s are read. Throws std::invalid_argument for a sample_rate below 1, an fft_size below 2, a
    // hiss_half_width_hz that is negative or not finite, a hiss_percentile outside (0, 100), and a hiss_average_s that is
    // not positive and finite.
    HissEstimator(int sample_rate, int fft_size, const ViterbiConfig& config);

    // Adds one frame to the averages (stages 1 and 2) and updates the bias. Throws std::invalid_argument, changing
    // nothing, if the frame does not hold fft_size bins.
    void push(const SpectrumFrame& frame);

    // N0, FS^2/Hz, around the channel whose center is the channelizer's signed bin center_bin (Channelizer::hz_to_bin,
    // in [-fft_size / 2, fft_size / 2)): stages 3 to 5. 0 before any frame, and on exact zeros. Throws std::out_of_range
    // for a center_bin outside that range.
    double noise_density_fs2_per_hz(int center_bin) const;

    // beta after `frames` frames (stage 4): the factor by which the percentile reads low on noise alone, 0.2877 after 1
    // frame (the exponential's ln(4/3)) and 0.92658 in steady state at the defaults. The recursion runs up to the frame
    // where it has reached its fixed point (to the double's precision) and stops there, so the cost is bounded for any
    // `frames`. Throws std::invalid_argument for frames below 1.
    double bias(int frames) const;

private:
    // The sums of stage 4 after some frames: A (the sum of the weights' squares), C (the sum of products of successive
    // weights) and the latest step.
    struct Weights {
        double sum_squares = 0.0;
        double sum_products = 0.0;
        double last_step = 0.0;
    };

    static void advance(Weights& weights, double step);
    double beta_of(const Weights& weights) const;

    int fft_size_;
    int half_bins_;                       // W: the neighborhood's half-width, bins
    double fraction_;                     // hiss_percentile / 100
    double alpha_;                        // the pole's step per frame
    double enbw_hz_;                      // one bin's equivalent noise bandwidth, Hz
    std::int64_t settled_frames_;         // from this frame on the weights no longer change (to the double's precision)
    std::int64_t frames_ = 0;             // frames pushed
    Weights weights_;                     // after frames_ frames, or after settled_frames_ once past it
    double beta_ = 1.0;                   // beta for weights_
    std::vector<double> average_;         // FS^2: the average of each bin's power
};

}  // namespace kz4ap::viterbi
