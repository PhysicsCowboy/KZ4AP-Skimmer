// The amplitude per story (design notes, "Detection, amplitude, frequency and the mark's shape", part 6; plan 1b, Task 6).
// The decoder runs it (search.hpp; viterbi_decoder.hpp).
//
// Each story carries one Gaussian amplitude N(mu, P) in FS at the front filter's output and the sample t_m of its last
// update (Bell 1977, one scalar Kalman filter per path). The prediction to sample k (owner, 2026-10-22 and 2026-10-23;
// plan D16, D17), with lag = k - t_m samples of Delta = 2.667 ms (the sample interval) and Omega_bar (FS^2) the story's
// long-run power:
//
//     rho     = exp(-pi^2 f_D^2 (lag Delta)^2 / 2)                           (1; derived: the complex gain's
//                                                                              correlation, Gaussian Doppler spectrum
//                                                                              of spread f_D, 2 Hz, owner, 2026-10-22)
//     rho_a   = (pi/4) (2F1(-1/2, -1/2; 1; rho^2) - 1) / (1 - pi/4)           (1; derived: the Rayleigh envelope's
//                                                                              correlation for that rho)
//     a_bar   = (sqrt(pi)/2) sqrt(Omega_bar) = 0.886 sqrt(Omega_bar)          (FS; derived: the Rayleigh mean)
//     mu'     = rho_a mu + (1 - rho_a) a_bar                                  (FS)
//     P'      = rho_a^2 P + (1/2) (1 - rho^2) Omega_bar                       (FS^2; the owner's rule, unchanged)
//
// The tables rho(lag) and rho_a(lag) are built once per spread (a genie-fading channel has its own) and read by lag
// until rho < 1e-12, after which both are 0. The hypergeometric function is evaluated to full double precision by two
// series (see hyp2f1_minus_half_minus_one).
//
// The search adds to P' at lag 1, inside a mark, (m - 1) (1/2) (1 - rho(1)^2) Omega_bar, m = 1 + (M - 1) x / (1 + x), x =
// Omega_bar / (X0 sigma_y^2), M = 16, X0 = 42 (predict_amplitude_at_level; heuristic, config.hpp): n predictions at lag 1
// add n Q while the true variance grows as n^2 Q (derived; 15.4 times too little at n = 16).
//
// Bell's decay variant (Part 15, item 16; D20) changes only the mean's target: across a space mu decays by
// Phi^(lag Delta / 5 ms), Phi = 10^(-2 / (22.4 T_dot,ms)) per 5 ms (Bell 1977; about 0.357 dB of amplitude per dot),
// never below 10^(-5/20) (-5 dB of amplitude) of the last estimate; the variance is the decided rule's, with no Q.
#pragma once

#include <cstdint>
#include <vector>

namespace kz4ap::viterbi {

// A story's amplitude estimate: mu (FS) and P (FS^2) as of sample t_m_sample (the sample of its last update).
struct Amplitude {
    double mu_fs;
    double p_fs2;
    std::int64_t t_m_sample;
};

// 2F1(-1/2, -1/2; 1; x) - 1, for 0 <= x <= 1, to full double precision (relative error about 1e-15), by the series
//   x < 0.5:  sum_{n >= 1} c_n^2 x^n, c_n = (-1/2)_n / n!  (terms positive, ratio (n - 1/2)^2 x / (n + 1)^2 < x),
//   x >= 0.5: the logarithmic expansion about x = 1 for c - a - b = 2 (Abramowitz and Stegun 15.3.11, m = 2), with
//             y = 1 - x < 0.5: the terms fall as y^n,
//   x = 1:    4/pi - 1 (the closed form 2F1(-1/2, -1/2; 1; 1) = Gamma(1) Gamma(2) / Gamma(3/2)^2 = 4/pi).
// (The plain power series is exact in principle at any x < 1 but needs about 6e7 terms at x = 0.9997.) Throws
// std::invalid_argument for x outside [0, 1].
double hyp2f1_minus_half_minus_one(double x);

class FadingTables {
public:
    // spread_hz: f_D (Hz), 0 meaning no fading (rho = rho_a = 1 at every lag); sample_interval_s: Delta (s). Throws
    // std::invalid_argument for a negative or non-finite spread or a non-positive or non-finite interval.
    FadingTables(double spread_hz, double sample_interval_s);

    // rho(lag) (1), lag in samples >= 0 (lag 0 is 1; beyond the table 0). Throws std::invalid_argument for lag < 0.
    double rho(std::int64_t lag) const;
    // rho_a(lag) (1), the same lags.
    double rho_a(std::int64_t lag) const;

private:
    bool no_fading_ = false;
    std::vector<double> rho_;    // lags 0 ... while rho >= 1e-12
    std::vector<double> rho_a_;  // the same lags
};

struct Prediction {
    double mu_fs;
    double p_fs2;
};

// The decided prediction (above) from `a` to sample k >= a.t_m_sample, with the story's long-run power omega_bar_fs2
// (FS^2, >= 0). The lag is k - a.t_m_sample; lag 0 returns (mu, P) unchanged. Throws std::invalid_argument for k before
// t_m or a negative or non-finite omega_bar_fs2.
Prediction predict_amplitude(const Amplitude& a, std::int64_t k, double omega_bar_fs2, const FadingTables& tables);

// The multiplier m (1) of the variance added at lag 1, above: from 1 at Omega_bar = 0 towards multiplier_max at strong
// levels, halfway at Omega_bar = level_knee x sigma2_fs2.
double added_variance_multiplier(double omega_bar_fs2, double sigma2_fs2, double multiplier_max, double level_knee);

// predict_amplitude, with the variance added at lag 1 multiplied by added_variance_multiplier. Other lags are
// predict_amplitude's. Throws as predict_amplitude does.
Prediction predict_amplitude_at_level(const Amplitude& a, std::int64_t k, double omega_bar_fs2, double sigma2_fs2,
                                      double multiplier_max, double level_knee, const FadingTables& tables);

// Bell's decay variant. dot_s: the story's dot length T_dot (s, > 0); sample_interval_s: Delta (s), the interval the
// tables were built with. Lag 0 is unchanged; lag 1 (the next sample inside a mark, where every sample is an estimate)
// equals predict_amplitude exactly; at lag >= 2 (across a space) the mean is mu max(Phi^(lag Delta / 5 ms), 10^(-5/20))
// with Phi = 10^(-2 / (22.4 T_dot,ms)), and the variance is predict_amplitude's (D20). Throws std::invalid_argument as
// predict_amplitude does and for a non-positive dot_s or sample_interval_s.
Prediction predict_amplitude_bell(const Amplitude& a, std::int64_t k, double omega_bar_fs2, double dot_s,
                                  const FadingTables& tables, double sample_interval_s);

}  // namespace kz4ap::viterbi
