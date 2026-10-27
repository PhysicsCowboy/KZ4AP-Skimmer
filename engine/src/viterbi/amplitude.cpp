#include "kz4ap/viterbi/amplitude.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kz4ap::viterbi {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEulerGamma = 0.57721566490153286061;
constexpr double kLn2 = 0.69314718055994530942;
constexpr double kRhoCutoff = 1e-12;       // the table ends where rho falls below this (1; heuristic: far below any use)
constexpr double kBellFloorDb = 5.0;       // Bell 1977: the decay stops at -5 dB of amplitude
constexpr double kBellStepS = 0.005;       // Bell 1977: Phi is per 5 ms
constexpr double kBellExponent = 2.0;      // Phi = 10^(-2 / (22.4 T_dot,ms)): 2 and 22.4 are Bell's
constexpr double kBellDotConstant = 22.4;

void require_finite_nonneg(double v, const char* what) {
    if (!(v >= 0.0) || !std::isfinite(v)) throw std::invalid_argument(what);
}

}  // namespace

double hyp2f1_minus_half_minus_one(double x) {
    if (!(x >= 0.0 && x <= 1.0)) throw std::invalid_argument("hyp2f1_minus_half_minus_one: x outside [0, 1]");
    if (x == 1.0) return 4.0 / kPi - 1.0;
    if (x < 0.5) {
        // c_n^2 x^n with c_0 = 1, c_{n+1} = c_n (n - 1/2) / (n + 1); summed from n = 1, smallest terms last.
        double c = -0.5;  // c_1
        double xn = x;    // x^n
        double sum = 0.0;
        for (int n = 1; n < 200; ++n) {
            const double term = c * c * xn;
            sum += term;
            if (term < 1e-18 * sum) break;
            c *= (n - 0.5) / (n + 1.0);
            xn *= x;
        }
        return sum;
    }
    // x in [0.5, 1): A&S 15.3.11 with a = b = -1/2, c = 1 = a + b + m, m = 2, y = 1 - x:
    //   F = (4/pi) (1 - y/4) - (1/(4 pi)) sum_n t_n y^(n+2) [ln y - psi(n+1) - psi(n+3) + 2 psi(n + 3/2)],
    //   t_n = (3/2)_n^2 / (n! (n+2)!), t_0 = 1/2.
    const double y = 1.0 - x;
    const double ln_y = std::log(y);
    double t = 0.5;
    double psi_n1 = -kEulerGamma;                     // psi(1)
    double psi_n3 = -kEulerGamma + 1.0 + 0.5;         // psi(3)
    double psi_nh = -kEulerGamma - 2.0 * kLn2 + 2.0;  // psi(3/2)
    double yp = y * y;
    double sum = 0.0;
    for (int n = 0; n < 200; ++n) {
        const double term = t * yp * (ln_y - psi_n1 - psi_n3 + 2.0 * psi_nh);
        sum += term;
        if (std::abs(term) < 1e-18 * std::abs(sum) && n > 2) break;
        t *= (n + 1.5) * (n + 1.5) / ((n + 1.0) * (n + 3.0));
        psi_n1 += 1.0 / (n + 1.0);
        psi_n3 += 1.0 / (n + 3.0);
        psi_nh += 1.0 / (n + 1.5);
        yp *= y;
    }
    return (4.0 / kPi) * (1.0 - 0.25 * y) - sum / (4.0 * kPi) - 1.0;
}

FadingTables::FadingTables(double spread_hz, double sample_interval_s) {
    require_finite_nonneg(spread_hz, "FadingTables: spread must be finite and >= 0");
    if (!(sample_interval_s > 0.0) || !std::isfinite(sample_interval_s))
        throw std::invalid_argument("FadingTables: sample interval must be finite and > 0");
    if (spread_hz == 0.0) {
        no_fading_ = true;
        return;
    }
    for (std::int64_t lag = 0;; ++lag) {
        const double t = static_cast<double>(lag) * sample_interval_s;
        const double rho = std::exp(-kPi * kPi * spread_hz * spread_hz * t * t / 2.0);
        if (rho < kRhoCutoff) break;
        // rho_a = (pi/4) (2F1 - 1) / (1 - pi/4); (2F1 - 1) is taken without cancellation.
        const double rho_a =
            lag == 0 ? 1.0  // exactly, so that a prediction across no time leaves the estimate unchanged
                     : (kPi / 4.0) * hyp2f1_minus_half_minus_one(std::min(1.0, rho * rho)) / (1.0 - kPi / 4.0);
        rho_.push_back(rho);
        rho_a_.push_back(rho_a);
    }
}

double FadingTables::rho(std::int64_t lag) const {
    if (lag < 0) throw std::invalid_argument("FadingTables::rho: negative lag");
    if (no_fading_) return 1.0;
    return lag < static_cast<std::int64_t>(rho_.size()) ? rho_[lag] : 0.0;
}

double FadingTables::rho_a(std::int64_t lag) const {
    if (lag < 0) throw std::invalid_argument("FadingTables::rho_a: negative lag");
    if (no_fading_) return 1.0;
    return lag < static_cast<std::int64_t>(rho_a_.size()) ? rho_a_[lag] : 0.0;
}

Prediction predict_amplitude(const Amplitude& a, std::int64_t k, double omega_bar_fs2, const FadingTables& tables) {
    const std::int64_t lag = k - a.t_m_sample;
    if (lag < 0) throw std::invalid_argument("predict_amplitude: sample before the last update");
    require_finite_nonneg(omega_bar_fs2, "predict_amplitude: long-run power must be finite and >= 0");
    const double rho = tables.rho(lag);
    const double rho_a = tables.rho_a(lag);
    const double a_bar = 0.5 * std::sqrt(kPi) * std::sqrt(omega_bar_fs2);
    return {rho_a * a.mu_fs + (1.0 - rho_a) * a_bar, rho_a * rho_a * a.p_fs2 + 0.5 * (1.0 - rho * rho) * omega_bar_fs2};
}

Prediction predict_amplitude_bell(const Amplitude& a, std::int64_t k, double omega_bar_fs2, double dot_s,
                                  const FadingTables& tables, double sample_interval_s) {
    if (!(dot_s > 0.0) || !std::isfinite(dot_s)) throw std::invalid_argument("predict_amplitude_bell: dot_s must be > 0");
    if (!(sample_interval_s > 0.0) || !std::isfinite(sample_interval_s))
        throw std::invalid_argument("predict_amplitude_bell: sample interval must be > 0");
    Prediction p = predict_amplitude(a, k, omega_bar_fs2, tables);  // the variance, and the mean at lags 0 and 1
    const std::int64_t lag = k - a.t_m_sample;
    if (lag >= 2) {
        // Phi^(lag Delta / 5 ms) = 10^(-2 / (22.4 T_dot,ms) * lag Delta / 5 ms), floored at 10^(-5/20).
        const double decay_db = 20.0 * kBellExponent / (kBellDotConstant * dot_s * 1000.0) *
                                (static_cast<double>(lag) * sample_interval_s / kBellStepS);
        p.mu_fs = a.mu_fs * std::pow(10.0, -std::min(decay_db, kBellFloorDb) / 20.0);
    }
    return p;
}

}  // namespace kz4ap::viterbi
