#include "kz4ap/viterbi/hiss.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace kz4ap::viterbi {

namespace {

// Derived (hiss.hpp, stage 4): the correlation of the powers of one bin in two frames a hop of fft_size / 2 apart, the
// square of the periodic Hann window's amplitude correlation 1/6.
constexpr double kSuccessivePowerCorrelation = 1.0 / 36.0;

// Derived (hiss.hpp, stage 5): the equivalent noise bandwidth of one bin of the Hann window, in bins.
constexpr double kHannEnbwBins = 1.5;

// dB re 1 FS^2 (hiss.hpp, stage 1): a reading at or below this is an exact zero. SpectrumAnalyzer reads zero power as
// 10 log10(1e-20) = -200 dB re 1 FS^2; this is 0.01 dB above it.
constexpr double kZeroReadingDb = -199.99;

// The weights of the average stop changing, to the double's precision, once the step has been the pole's alpha for m
// frames with (1 - alpha)^(2 m) = exp(-2 m hop_s / tau) below 2^-53 = 1.1e-16, that is for 2 m hop_s / tau above this
// many nepers (e^-37 = 8.5e-17).
constexpr double kSettleNepers = 37.0;

bool positive_and_finite(double x) { return x > 0.0 && std::isfinite(x); }

// The series and the fraction of the incomplete gamma function stop at this relative size of their last term, or after
// this many terms. Near x = a the terms fall as exp(-n^2 / (2 a)), so a shape a needs about 8.6 sqrt(a) of them for 1e-16
// (derived): 81 at the steady state's shape of 89, 2 700 at 1e5.
constexpr double kSeriesEpsilon = 1.0e-16;
constexpr int kMaxTerms = 100000;

// ln of x^a e^-x / Gamma(a), the factor of the incomplete gamma function's series and fraction, for a > 0 and x > 0. Its
// three terms are each about a ln a in size, so it carries a rounding error of about 1e-16 a ln a (derived): 4e-14 at the
// steady state's shape of 89, 1e-10 at 1e5.
double log_gamma_factor(double a, double x) { return a * std::log(x) - x - std::lgamma(a); }

// P(a, x), the regularized lower incomplete gamma function, a > 0 and x >= 0: the probability that a Gamma variate of
// shape a and scale 1 is at most x. Below x = a + 1 by its power series,
//     P = x^a e^-x / Gamma(a) sum_(n >= 0) x^n / (a (a + 1) ... (a + n)),
// and above by Lentz's continued fraction for Q = 1 - P,
//     Q = x^a e^-x / Gamma(a) / (x + 1 - a - 1 (1 - a) / (x + 3 - a - 2 (2 - a) / (x + 5 - a - ...))),
// each converging fast in its own region (Press et al., Numerical Recipes, section 6.2).
double regularized_gamma_p(double a, double x) {
    if (!(x > 0.0)) return 0.0;
    const double factor = std::exp(log_gamma_factor(a, x));
    if (x < a + 1.0) {
        double term = 1.0 / a;
        double sum = term;
        for (int n = 1; n < kMaxTerms; ++n) {
            term *= x / (a + n);
            sum += term;
            if (term < sum * kSeriesEpsilon) break;
        }
        return factor * sum;
    }
    const double tiny = std::numeric_limits<double>::min() / kSeriesEpsilon;
    double b = x + 1.0 - a;
    double c = 1.0 / tiny;
    double d = 1.0 / b;
    double fraction = d;
    for (int i = 1; i < kMaxTerms; ++i) {
        const double numerator = -i * (i - a);
        b += 2.0;
        d = numerator * d + b;
        if (std::fabs(d) < tiny) d = tiny;
        c = b + numerator / c;
        if (std::fabs(c) < tiny) c = tiny;
        d = 1.0 / d;
        const double change = d * c;
        fraction *= change;
        if (std::fabs(change - 1.0) < kSeriesEpsilon) break;
    }
    return 1.0 - factor * fraction;
}

// The p-quantile of the Gamma distribution of shape a and scale 1, 0 < p < 1: the x with P(a, x) = p. Newton's method
// (the derivative is the density x^(a-1) e^-x / Gamma(a)) from the mean, inside a bracket that every evaluation
// narrows, with a bisection step wherever Newton's leaves it. The bracket starts at [0, a + 1], doubled upward until
// P(a, upper end) reaches p. It stops when a step is below 1e-13 of x; the answer is then good to 1e-12 relative or
// better (the plan asks 1e-6), limited by the incomplete gamma function's own accuracy.
double gamma_quantile(double a, double p) {
    double low = 0.0;
    double high = a + 1.0;
    while (regularized_gamma_p(a, high) < p) {
        low = high;
        high *= 2.0;
    }
    double x = std::clamp(a, low, high);
    if (!(x > 0.0)) x = 0.5 * (low + high);
    for (int iteration = 0; iteration < 200; ++iteration) {
        const double excess = regularized_gamma_p(a, x) - p;
        if (excess < 0.0)
            low = x;
        else
            high = x;
        const double density = std::exp(log_gamma_factor(a, x) - std::log(x));
        double next = x - excess / density;
        if (!(next > low && next < high)) next = 0.5 * (low + high);
        const bool done = std::fabs(next - x) <= 1.0e-13 * next;
        x = next;
        if (done) break;
    }
    return x;
}

}  // namespace

HissEstimator::HissEstimator(int sample_rate, int fft_size, const ViterbiConfig& config)
    : fft_size_(fft_size), half_bins_(0), fraction_(0.0), alpha_(0.0), enbw_hz_(0.0), settled_frames_(0) {
    if (sample_rate < 1) throw std::invalid_argument("hiss: sample_rate must be at least 1 (Hz)");
    if (fft_size < 2) throw std::invalid_argument("hiss: fft_size must be at least 2 (bins)");
    if (!(config.hiss_half_width_hz >= 0.0) || !std::isfinite(config.hiss_half_width_hz))
        throw std::invalid_argument("hiss: hiss_half_width_hz must be finite and not negative (Hz)");
    if (!(config.hiss_percentile > 0.0 && config.hiss_percentile < 100.0))
        throw std::invalid_argument("hiss: hiss_percentile must lie in (0, 100) (percent)");
    if (!positive_and_finite(config.hiss_average_s))
        throw std::invalid_argument("hiss: hiss_average_s must be positive and finite (s)");

    const double bin_hz = static_cast<double>(sample_rate) / fft_size;
    const double hop_s = static_cast<double>(fft_size / 2) / sample_rate;
    const double decay = hop_s / config.hiss_average_s;  // nepers per frame: 1 - alpha = exp(-decay)
    alpha_ = -std::expm1(-decay);
    // The neighborhood's half-width in bins, the floor of its Hz (the 1e-9 guards a quotient a rounding below a whole
    // number), at most the whole span.
    half_bins_ = static_cast<int>(
        std::min(std::floor(config.hiss_half_width_hz / bin_hz + 1.0e-9), static_cast<double>(fft_size)));
    fraction_ = config.hiss_percentile / 100.0;
    enbw_hz_ = kHannEnbwBins * bin_hz;
    // The frame from which the step is alpha for good, 1 / alpha rounded up, and the frames after it that the weights
    // need to settle; clamped well below the largest 64-bit integer for a time constant far beyond any recording.
    const double settle = std::ceil(1.0 / alpha_) + std::ceil(kSettleNepers / (2.0 * decay)) + 1.0;
    settled_frames_ = static_cast<std::int64_t>(std::min(settle, 4.0e18));
    average_.assign(static_cast<std::size_t>(fft_size), 0.0);
    sample_rate_ = sample_rate;
    band_first_ = 0;
    band_last_ = fft_size - 1;
}

void HissEstimator::set_band(double lo_hz, double hi_hz) {
    if (!std::isfinite(lo_hz) || !std::isfinite(hi_hz)) throw std::invalid_argument("hiss: the passband's edges must be finite (Hz)");
    const BinRange band = band_bins(lo_hz, hi_hz, sample_rate_, fft_size_);
    band_first_ = band.first;
    band_last_ = band.last;
}

void HissEstimator::advance(Weights& weights, double step) {
    const double keep = 1.0 - step;
    weights.sum_squares = keep * keep * weights.sum_squares + step * step;
    weights.sum_products = keep * keep * weights.sum_products + keep * step * weights.last_step;
    weights.last_step = step;
}

double HissEstimator::beta_of(const Weights& weights) const {
    const double variance = weights.sum_squares + 2.0 * kSuccessivePowerCorrelation * weights.sum_products;
    const double shape = 1.0 / variance;
    return gamma_quantile(shape, fraction_) / shape;
}

void HissEstimator::push(const SpectrumFrame& frame) {
    if (frame.power_db.size() != average_.size())
        throw std::invalid_argument("hiss: the frame must hold fft_size bins");
    // A frame of exact zeros in every bin is missing data, not a measurement (hiss.hpp, stage 2): it is not counted and
    // changes nothing.
    if (std::all_of(frame.power_db.begin(), frame.power_db.end(), [](float db) { return db <= kZeroReadingDb; }))
        return;
    ++frames_;
    const double step = std::max(alpha_, 1.0 / static_cast<double>(frames_));
    const bool linear = frame.power_fs2.size() == average_.size();
    for (std::size_t i = 0; i < average_.size(); ++i) {
        const double power = frame.power_db[i] <= kZeroReadingDb ? 0.0
                             : linear                            ? static_cast<double>(frame.power_fs2[i])
                                                                 : std::pow(10.0, frame.power_db[i] / 10.0);
        average_[i] += step * (power - average_[i]);
    }
    if (frames_ <= settled_frames_) {
        advance(weights_, step);
        beta_ = beta_of(weights_);
    }
}

double HissEstimator::noise_density_fs2_per_hz(int center_bin) const {
    const int half = fft_size_ / 2;
    if (center_bin < -half || center_bin >= fft_size_ - half)
        throw std::out_of_range("hiss: center_bin must lie in [-fft_size / 2, fft_size / 2) (channelizer bins)");
    const int index = center_bin + half;  // the frame's bin: bin i is at (i - fft_size / 2) bins from 0 Hz
    if (frames_ == 0) return 0.0;
    int first = std::max(band_first_, index - half_bins_);
    int last = std::min(band_last_, index + half_bins_);
    if (last < first) {
        // A channel outside the passband (set_band): the passband's own bins.
        first = band_first_;
        last = band_last_;
    }
    const int count = last - first + 1;
    // The rank from the smallest, ceil(fraction x count); the 1e-9 guards a product a rounding above a whole number.
    const int rank = std::clamp(static_cast<int>(std::ceil(fraction_ * count - 1.0e-9)), 1, count);
    std::vector<double> window(average_.begin() + first, average_.begin() + last + 1);
    std::nth_element(window.begin(), window.begin() + (rank - 1), window.end());
    const double percentile = window[static_cast<std::size_t>(rank - 1)];
    if (!(percentile > 0.0)) return 0.0;
    return percentile / beta_ / enbw_hz_;
}

double HissEstimator::bias(int frames) const {
    if (frames < 1) throw std::invalid_argument("hiss: bias needs at least 1 frame");
    Weights weights;
    const std::int64_t last = std::min<std::int64_t>(frames, settled_frames_);
    for (std::int64_t n = 1; n <= last; ++n) advance(weights, std::max(alpha_, 1.0 / static_cast<double>(n)));
    return beta_of(weights);
}

}  // namespace kz4ap::viterbi
