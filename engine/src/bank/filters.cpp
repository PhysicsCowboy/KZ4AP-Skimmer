#include "kz4ap/bank/filters.hpp"

#include "kz4ap/matched_front_end.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kz4ap::bank {

std::vector<double> branch_lengths_s(const BankConfig& cfg) {
    const double l_min = cfg.length_dits * 1.2 / cfg.max_wpm;
    const double l_max = cfg.length_dits * 1.2 / cfg.min_wpm;
    const int count = static_cast<int>(std::ceil(std::log(l_max / l_min) / std::log(cfg.ladder_step) - 1e-9));
    std::vector<double> out(static_cast<std::size_t>(std::max(count, 0)));
    for (std::size_t k = 0; k < out.size(); ++k) out[k] = l_min * std::pow(cfg.ladder_step, static_cast<double>(k));
    return out;
}

std::vector<int> branch_samples(const std::vector<double>& lengths_s, double rate_hz) {
    std::vector<int> out(lengths_s.size());
    for (std::size_t k = 0; k < out.size(); ++k)
        out[k] = static_cast<int>(std::max(1.0, std::nearbyint(lengths_s[k] * rate_hz)));
    return out;
}

std::vector<double> realized_lengths_s(const BankConfig& cfg, double rate_hz) {
    const std::vector<int> n = branch_samples(branch_lengths_s(cfg), rate_hz);
    std::vector<double> out(n.size());
    for (std::size_t k = 0; k < n.size(); ++k) out[k] = n[k] / rate_hz;
    return out;
}

std::vector<std::complex<double>> boxcar(std::span<const std::complex<double>> u, int n) {
    std::vector<std::complex<double>> c(u.size() + 1);
    c[0] = 0.0;
    for (std::size_t i = 0; i < u.size(); ++i) c[i + 1] = c[i] + u[i];
    std::vector<std::complex<double>> out(u.size());
    const double scale = 1.0 / n;  // numpy's complex division by n + 0j multiplies by 1 / n
    for (std::size_t m = 1; m <= u.size(); ++m) {
        const std::size_t lo = m > static_cast<std::size_t>(n) ? m - static_cast<std::size_t>(n) : 0;
        const std::complex<double> d = c[m] - c[lo];
        out[m - 1] = {d.real() * scale, d.imag() * scale};
    }
    return out;
}

double numpy_abs(std::complex<double> z) {
    const double re = std::abs(z.real());
    const double im = std::abs(z.imag());
    if (std::isnan(re) || std::isnan(im)) return std::isinf(re) || std::isinf(im) ? INFINITY : NAN;
    const double larger = std::max(re, im);
    const double smaller = std::min(re, im);
    if (larger == 0.0) return 0.0;
    if (std::isinf(larger)) return larger;
    const double rat = smaller / larger;
    return std::sqrt(std::fma(rat, rat, 1.0)) * larger;
}

double boxcar_power_f32(std::complex<double> v) {
    const double a = numpy_abs(v);
    return static_cast<double>(static_cast<float>(a * a));
}

double power_response(double f_hz, int n, double rate_hz) {
    const double x = std::numbers::pi * f_hz / rate_hz;
    const double den = n * std::sin(x);
    if (std::abs(den) > 1e-12) {
        const double r = std::sin(n * x) / den;
        return r * r;
    }
    return 1.0;
}

double log_bessel_i0(double z) { return kz4ap::log_bessel_i0(z); }

double envelope_llr(double x, double a) { return kz4ap::envelope_llr(x, a); }

double logistic(double g) { return 1.0 / (1.0 + std::exp(-std::clamp(g, -50.0, 50.0))); }

}  // namespace kz4ap::bank
