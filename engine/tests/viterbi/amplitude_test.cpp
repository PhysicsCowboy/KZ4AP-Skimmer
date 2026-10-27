#include "kz4ap/viterbi/amplitude.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace kz4ap::viterbi;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleS = 4.0 / 1500.0;  // Delta, 2.667 ms (D17)
constexpr double kSpreadHz = 2.0;          // f_D (owner, 2026-10-22)

double rel(double a, double b) { return std::abs(a - b) / std::abs(b); }

// The mean of |1 - r e^(i theta)| over theta, by the periodic trapezoid rule: equal to 2F1(-1/2, -1/2; 1; r^2) by
// Parseval (the coefficients of (1 - t)^(1/2) squared), an independent evaluation of the hypergeometric function.
double mean_modulus(double r) {
    const int n = 4000000;
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        const double th = 2.0 * kPi * (i + 0.5) / n;
        s += std::sqrt(1.0 - 2.0 * r * std::cos(th) + r * r);
    }
    return s / n;
}

}  // namespace

TEST(Amplitude, FadingValuesAt2Hz) {
    const FadingTables t(kSpreadHz, kSampleS);
    struct Row {
        int lag;
        double rho, rho_a, spread;
    };
    // Design notes, part 6, "The values at 2 Hz" (derived), each to half a unit of its last printed digit.
    const Row rows[] = {{1, 0.99986, 0.99967, 0.012}, {18, 0.9555, 0.902, 0.208}, {54, 0.664, 0.416, 0.529},
                        {126, 0.108, 0.011, 0.703}};
    const double half_unit_rho[] = {5e-6, 5e-5, 5e-4, 5e-4};
    const double half_unit_rho_a[] = {5e-6, 5e-4, 5e-4, 5e-4};
    for (int i = 0; i < 4; ++i) {
        const auto& r = rows[i];
        const double rho = t.rho(r.lag);
        EXPECT_NEAR(rho, r.rho, half_unit_rho[i]) << "lag " << r.lag;
        EXPECT_NEAR(t.rho_a(r.lag), r.rho_a, half_unit_rho_a[i]) << "lag " << r.lag;
        EXPECT_NEAR(std::sqrt((1.0 - rho * rho) / 2.0), r.spread, 5e-4) << "lag " << r.lag;
        std::cout << std::setprecision(8) << "lag " << r.lag << ": rho " << rho << ", rho_a " << t.rho_a(r.lag) << ", spread "
                  << std::sqrt((1.0 - rho * rho) / 2.0) << "\n";
    }
    EXPECT_EQ(t.rho(0), 1.0);
    EXPECT_EQ(t.rho_a(0), 1.0);
    // Past the table (rho < 1e-12) both are 0: lag 400 is 1.067 s, rho = exp(-pi^2 4 1.138 / 2) = 1.7e-10; lag 600 is 1.6 s.
    EXPECT_GT(t.rho(400), 0.0);
    EXPECT_EQ(t.rho(600), 0.0);
    EXPECT_EQ(t.rho_a(600), 0.0);
    EXPECT_EQ(t.rho(1000000), 0.0);
    EXPECT_THROW(t.rho(-1), std::invalid_argument);
    EXPECT_THROW(FadingTables(-1.0, kSampleS), std::invalid_argument);
    EXPECT_THROW(FadingTables(2.0, 0.0), std::invalid_argument);
}

TEST(Amplitude, HypergeometricSeriesIsFullPrecision) {
    // The series branch (x < 0.5), the logarithmic branch about x = 1 (x >= 0.5) and x = 1 itself (4/pi), against the
    // independent mean of |1 - r e^(i theta)| with r = sqrt(x).
    for (double x : {1e-8, 0.01, 0.3, 0.4999, 0.5, 0.5001, 0.7, 0.9, 0.99, 0.9997, 1.0 - 1e-9}) {
        const double f = 1.0 + hyp2f1_minus_half_minus_one(x);
        EXPECT_NEAR(f, mean_modulus(std::sqrt(x)), 1e-12) << "x = " << x;
    }
    EXPECT_NEAR(1.0 + hyp2f1_minus_half_minus_one(1.0), 4.0 / kPi, 1e-15);
    // Continuity across the two branches' switch, to rounding.
    EXPECT_NEAR(hyp2f1_minus_half_minus_one(0.5 - 1e-13), hyp2f1_minus_half_minus_one(0.5 + 1e-13), 1e-12);
    // The small-x leading term: c_1^2 x = x/4.
    EXPECT_LT(rel(hyp2f1_minus_half_minus_one(1e-8), 0.25e-8), 1e-7);
    EXPECT_THROW(hyp2f1_minus_half_minus_one(1.5), std::invalid_argument);
    EXPECT_THROW(hyp2f1_minus_half_minus_one(-0.1), std::invalid_argument);
}

TEST(Amplitude, RhoAIsTheRayleighEnvelopeCorrelation) {
    // 10^6 pairs of Rayleigh envelopes whose complex Gaussian gains have correlation rho: the sample correlation
    // coefficient of the envelopes against rho_a. Standard error (1 - rho_a^2)/sqrt(N) <= 0.001, bound 0.003.
    std::mt19937_64 gen(20261027);
    std::normal_distribution<double> g(0.0, std::sqrt(0.5));
    const int n = 1000000;
    for (double rho : {0.3, 0.6, 0.9}) {
        const double c = std::sqrt(1.0 - rho * rho);
        double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
        for (int i = 0; i < n; ++i) {
            const double xr = g(gen), xi = g(gen), wr = g(gen), wi = g(gen);
            const double a = std::hypot(xr, xi);
            const double b = std::hypot(rho * xr + c * wr, rho * xi + c * wi);
            sa += a;
            sb += b;
            saa += a * a;
            sbb += b * b;
            sab += a * b;
        }
        const double ma = sa / n, mb = sb / n;
        const double corr = (sab / n - ma * mb) / std::sqrt((saa / n - ma * ma) * (sbb / n - mb * mb));
        // rho_a(rho) from the table's formula, evaluated directly.
        const double expected = (kPi / 4.0) * hyp2f1_minus_half_minus_one(rho * rho) / (1.0 - kPi / 4.0);
        EXPECT_NEAR(corr, expected, 0.003) << "rho " << rho;
        std::cout << "rho " << rho << ": simulated " << corr << ", formula " << expected << "\n";
    }
}

TEST(Amplitude, PredictionIsTheOwnersRule) {
    const FadingTables t(kSpreadHz, kSampleS);
    const double omega = 0.04;  // FS^2
    const Amplitude a{0.31, 0.003, 100};
    const double abar = std::sqrt(kPi) / 2.0 * std::sqrt(omega);
    EXPECT_NEAR(std::sqrt(kPi) / 2.0, 0.886227, 5e-7);
    for (int lag : {1, 7, 18, 54, 126, 300}) {
        const Prediction p = predict_amplitude(a, 100 + lag, omega, t);
        const double rho = t.rho(lag), ra = t.rho_a(lag);
        EXPECT_LT(rel(p.mu_fs, ra * a.mu_fs + (1 - ra) * abar), 1e-12) << "lag " << lag;
        EXPECT_LT(rel(p.p_fs2, ra * ra * a.p_fs2 + 0.5 * (1 - rho * rho) * omega), 1e-12) << "lag " << lag;
    }
    const Prediction same = predict_amplitude(a, 100, omega, t);  // lag 0: unchanged
    EXPECT_EQ(same.mu_fs, a.mu_fs);
    EXPECT_EQ(same.p_fs2, a.p_fs2);
    const Prediction far = predict_amplitude(a, 100 + 10000, omega, t);  // lag 10^4 (26.7 s)
    EXPECT_LT(rel(far.mu_fs, 0.886227 * std::sqrt(omega)), 1e-6);        // the printed constant's own precision
    EXPECT_LT(rel(far.mu_fs, std::sqrt(kPi) / 2.0 * std::sqrt(omega)), 1e-9);
    EXPECT_LT(rel(far.p_fs2, omega / 2.0), 1e-9);
    EXPECT_THROW(predict_amplitude(a, 99, omega, t), std::invalid_argument);
    EXPECT_THROW(predict_amplitude(a, 101, -1.0, t), std::invalid_argument);
}

TEST(Amplitude, BellDecaysAbout036DbPerDot) {
    const FadingTables t(kSpreadHz, kSampleS);
    const double omega = 0.04;
    const Amplitude a{0.31, 0.003, 0};
    for (double wpm : {5.2, 25.0, 100.0}) {
        const double dot_s = 1.2 / wpm;  // PARIS
        // A lag of about a quarter of the 14-dot floor distance, in whole samples (at least 3 so that it is a space).
        const int lag = std::max(3, static_cast<int>(std::lround(3.0 * dot_s / kSampleS)));
        const Prediction p = predict_amplitude_bell(a, lag, omega, dot_s, t, kSampleS);
        const double db_total = -20.0 * std::log10(p.mu_fs / a.mu_fs);          // dB of amplitude, re the last estimate
        const double db_per_dot = db_total / (lag * kSampleS / dot_s);
        EXPECT_NEAR(db_per_dot, 40.0 / (22.4 * 5.0), 0.001) << wpm << " WPM";  // 0.357 dB of amplitude per dot
        EXPECT_NEAR(40.0 / (22.4 * 5.0), 0.357, 0.0005);
        // The exact form: Phi^(lag Delta / 5 ms), Phi = 10^(-2/(22.4 T_dot,ms)).
        const double phi = std::pow(10.0, -2.0 / (22.4 * dot_s * 1000.0));
        EXPECT_LT(rel(p.mu_fs, a.mu_fs * std::pow(phi, lag * kSampleS / 0.005)), 1e-12);
    }
    // The floor: never below -5 dB of amplitude re the last estimate, at any length of space.
    {
        const double dot_s = 1.2 / 25.0;
        const Prediction p = predict_amplitude_bell(a, 100000, omega, dot_s, t, kSampleS);
        EXPECT_LT(rel(p.mu_fs, a.mu_fs * std::pow(10.0, -5.0 / 20.0)), 1e-12);
        // 14 dots reach -5 dB; 13 dots do not, 15 dots are held at it.
        const int l13 = static_cast<int>(13.0 * dot_s / kSampleS), l15 = static_cast<int>(15.0 * dot_s / kSampleS);
        EXPECT_GT(predict_amplitude_bell(a, l13, omega, dot_s, t, kSampleS).mu_fs, p.mu_fs * (1 + 1e-6));
        EXPECT_LT(rel(predict_amplitude_bell(a, l15, omega, dot_s, t, kSampleS).mu_fs, p.mu_fs), 1e-12);
    }
    // Lag 1 (inside a mark) equals the decided rule, mean and variance; lag 0 is unchanged.
    for (double wpm : {5.2, 25.0, 100.0}) {
        const double dot_s = 1.2 / wpm;
        const Prediction b = predict_amplitude_bell(a, 1, omega, dot_s, t, kSampleS);
        const Prediction d = predict_amplitude(a, 1, omega, t);
        EXPECT_EQ(b.mu_fs, d.mu_fs);
        EXPECT_EQ(b.p_fs2, d.p_fs2);
        const Prediction z = predict_amplitude_bell(a, 0, omega, dot_s, t, kSampleS);
        EXPECT_EQ(z.mu_fs, a.mu_fs);
        EXPECT_EQ(z.p_fs2, a.p_fs2);
    }
    // Across spaces of 1, 18, 54 and 126 samples the variance is the decided rule's (D20: no Q), only the mean differs.
    for (int lag : {1, 18, 54, 126}) {
        const Prediction b = predict_amplitude_bell(a, lag, omega, 0.048, t, kSampleS);
        const Prediction d = predict_amplitude(a, lag, omega, t);
        EXPECT_LT(rel(b.p_fs2, d.p_fs2), 1e-12) << "lag " << lag;
        if (lag > 1) EXPECT_LT(b.mu_fs, a.mu_fs);
        if (lag > 1) EXPECT_NE(b.mu_fs, d.mu_fs);
    }
    EXPECT_THROW(predict_amplitude_bell(a, 5, omega, 0.0, t, kSampleS), std::invalid_argument);
    EXPECT_THROW(predict_amplitude_bell(a, -1, omega, 0.048, t, kSampleS), std::invalid_argument);
}
