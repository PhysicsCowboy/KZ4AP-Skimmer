// The long-run amplitude per story: Omega_hat, nu and the default Omega_bar (design notes, "Detection, amplitude,
// frequency and the mark's shape", part 7; plan 1b, Task 7). Library code: the decoder does not run it yet.
//
// 1. Omega_hat, each story's RMS power by ordinary least squares (owner, 2026-10-23 and 2026-10-25; derived):
//        Omega_hat = sum_k s_k^2 (e_k^2 - sigma_y^2) / sum_k s_k^4      (FS^2),
//    over every sample with s_k != 0 since the story's sums restarted (an over's start, a rescue newborn), e_k the
//    envelope sample (FS), s_k the story's template (1) and sigma_y^2 the front filter's output noise power at that
//    sample (FS^2). A sample with s_k = 0 adds nothing. It is unbiased on a steady carrier (E e_k^2 = s_k^2 a^2 +
//    sigma_y^2) and under fading (E Omega_hat = Omega). Its physical floor, from e_k^2 >= 0, is
//    -sigma_y^2 sum s_k^2 / sum s_k^4: -2.09 sigma_y^2 after a 100 WPM dot claimed as 4 samples, -1.05 after "PARIS" at
//    25 WPM, and after "PARIS" at 100 WPM -1.34 or -1.29 as its first dot is claimed as 4 or 5 samples (derived,
//    LongRun.PhysicalFloor; the design's -1.30 does not record its placement). The sums are compensated (Neumaier), so
//    a story that lives for an hour keeps them exact to rounding (Review focus 4).
// 2. nu, the number of independent fades behind Omega_hat (owner, 2026-10-23; Satterthwaite's 1946 moment match,
//    derived): each story sums W_i = sum s_k^4 over the samples of bin i, the bins b = 16 samples (42.67 ms) long on the
//    channel's common grid (bin i holds the samples k with floor(k / 16) = i), and
//        nu = (sum_i W_i)^2 / sum_i sum_j W_i W_j rho^2(t_i - t_j),   rho^2(tau) = exp(-pi^2 f_D^2 tau^2),
//    t_i the bins' centers (t_i - t_j = (i - j) b), f_D the fading spread (2 Hz, owner, 2026-10-22). The sums run over
//    the whole over; a bin's cross terms use the 10 bins before it only (rho^2 < 0.001 beyond 0.42 s at 2 Hz), so the
//    double sum is a running total: each closed bin adds W_i^2 + 2 W_i C_i, C_i = sum over the kept bins j of W_j
//    rho^2((i - j) b). The open bin counts too (plan O7): its C is fixed while it is open, so nu exists from a story's
//    first sample with s != 0 (nu = 1 on one bin) and costs a few operations per sample. Binning error against the exact
//    sum over samples: (pi f_D b)^2 / 12 = 0.6% at 2 Hz (derived for weight spread evenly over each bin; the scale, not
//    a bound); -0.6% to +0.6% measured on "PARIS" once, twice and four times at 8, 25 and 100 WPM
//    (LongRun.BinnedNuMatchesTheExactSum; the design's scratch measured up to +2.3%), within the Satterthwaite form's
//    own ~3%. Keeping 10 bins changes nu by under 0.01% against keeping all (LongRun.NuOnKeyDownTimes).
// 3. Omega_bar, the expected long-run power, by default from Omega_hat clipped at zero (owner, 2026-10-25): the model
//    nu Omega_hat_+ / Omega ~ Gamma(nu), Omega_hat_+ = max(Omega_hat, 0), with the owner's prior flat in dB over
//    [Omega_lo, Omega_hi] (-15 to +85 dB S500, placeholders), Omega = 10^(L/10) N0 x 500 Hz at L dB S500. With
//    sigma_y^2 = N0 x 74.4998 Hz (the 192 kHz chain's output_noise; 74.48 Hz in the design notes, from a 48 kHz model,
//    0.03% apart), Omega_lo = 0.2122 sigma_y^2 and Omega_hi = 2.122 x 10^9 sigma_y^2 (derived). The posterior is the
//    inverse gamma of shape nu and scale beta = nu Omega_hat_+ truncated to the prior's range, and Omega_bar its mean
//    (derived; omega_bar_closed_form):
//        Omega_bar = integral Omega^(-nu) e^(-beta / Omega) dOmega / integral Omega^(-nu - 1) e^(-beta / Omega) dOmega,
//    both over [Omega_lo, Omega_hi]. About Omega_hat nu / (nu - 1) once nu exceeds about 1.5; at nu = 1 it rests on the
//    prior's top (14.4 Omega_hat at +20 dB S500). At beta = 0 it is Omega_lo E(nu - 1) / E(nu), E(a) = (1 - e^(-a l)) / a
//    (E(0) = l), l = ln(Omega_hi / Omega_lo): Omega_lo nu / (nu - 1) only where nu - 1 >> 1 / l = 0.043, 23.0 Omega_lo at
//    nu = 1, about 21 Omega_lo at nu = 1.01. At nu = infinity it is Omega_hat_+ clipped to [Omega_lo, Omega_hi].
// 4. The table (OmegaBarTable; heuristic grid, its error measured against the closed form, plan O8: at most 0.1%).
//    Inputs p = 1 / (1 + sqrt(nu - 1)) on [0, 1] (p = 1 at nu = 1, p = 0 at nu = infinity; p falls as 1/sqrt(nu) for
//    large nu, where the posterior's width in ln Omega is 1/sqrt(nu), and 1 - p grows as sqrt(nu - 1) near nu = 1,
//    where Omega_bar changes fastest) and z = ln(Omega_hat_+ / Omega_hi); output ln(Omega_bar / Omega_hi); bilinear.
//    The p nodes are uniform (97); the z nodes are uniform within each of nine segments: fine (0.01) within 0.5 of the
//    two corners of the clipping, z = -l (Omega_hat at the prior's bottom) and z = 0 (at its top), whose width in z is
//    about 1/sqrt(nu); medium (0.06) out to 4 from them, where at nu of 1 to 3 Omega_bar bends over several units of z;
//    coarse (0.25) elsewhere, where it is close to linear in z (534 nodes of z: 51 798 values, 52 055 with the
//    beta = 0 row, 0.42 MB as doubles). Below z_min = -l - 9 (Omega_hat at or below 1.2 x 10^-4 Omega_lo, zero and
//    negative Omega_hat included) the exact beta = 0 value is read, from a row of 257 nodes uniform in p; above
//    z_max = 8 (Omega_hat 2 981 times Omega_hi) z is read at z_max, where Omega_bar is within 0.04% of the value
//    beyond. nu below 1 (rounding only: nu >= 1 by the Satterthwaite form) and nu not a number read as 1. Measured
//    against the closed form (the generator's 10 000 points, nu log-uniform on [1, 1 000], Omega_hat / sigma_y^2
//    uniform in asinh on [-5, 10^9]): at most 0.055%; with nu up to 10^6, 0.056% (the generated file records the run).
//    Known limitation, on the compiled table as on any other: above nu of about 1.5 x 10^4 (more than about an hour of
//    key-down in one over: 12 763 after an hour) a corner is narrower than the fine step, and within it the error
//    passes 0.1%, up to about 0.14% (measured on the compiled table within 0.3 of the corners in Task 7's review: 0.096%
//    at nu = 1.5 x 10^4, 0.140% at 3 x 10^4 and 10^5, 0.105% at 3 x 10^5, 0.066% at 10^6; 0.14% at nu = 7.4 x 10^4 on
//    a table for -5 and +60 dB S500). The random points above miss the corners.
//    The plan's axes, 1/nu and x = ln(beta / Omega_hi) = z + ln nu, were not used: on them the corners move with ln nu
//    and the row 1/nu = 0 is degenerate (Omega_bar = Omega_lo at every x), so the plan's 31 x 256 grid missed by up to
//    100% (measured in scratch), and meeting 0.1% on them would need fine x everywhere the corners pass and hundreds
//    of nodes of 1/nu near 0 (derived; Task 7's report).
//    The compiled table (omega_bar_table.cpp, generated by kz4ap-viterbi-tables omega-bar) is for -15 and +85 dB S500;
//    a table for other bounds is generated into a file and loaded (make_viterbi_shared refuses other bounds without
//    one; Ruling 72, N14).
//
// The measured variant: the noise in Omega_bar's likelihood (owner, 2026-10-25; design notes, part 7; part 15, item 17;
// plan 1b, Task 8). Library code: no decoder runs it until Task 12.
// 5. Omega_hat's noise variance (NoiseVarianceSums; derived). With x_k = s_k alpha_k + n_k (alpha_k the story's complex
//    carrier at the front filter's output, E|alpha_k|^2 = Omega; n_k complex Gaussian noise, E n_j n_l^* = sigma_y^2
//    rho_(j-l), rho_m the front filter's output-noise correlation at lags of Delta, front_filter.hpp), Omega_hat =
//    Omega G + N, G the fading's power gain over the story's samples (mean 1) and N the noise's part, of variance
//        Var(N | Omega, G) = A + B Omega G,   A = sigma_y^4 a2 / (sum s^4)^2,   B = 2 sigma_y^2 b2 / (sum s^4)^2,
//        a2 = sum_j sum_l s_j^2 s_l^2 rho_(j-l)^2,   b2 = sum_j sum_l s_j^3 s_l^3 rho_(j-l)
//    (Isserlis' theorem for the noise-with-noise part; the signal-with-noise part with the fading's correlation over
//    the noise's 6 lags, 16.0 ms, taken as 1: at least 0.9950 at 2 Hz; derived). Both double sums run over 6 lags
//    (rho_7 and beyond, below 3 x 10^-4, change them by under 0.01%; measured,
//    NoiseVariance.RunningSumsAreTheDoubleSums) as two exact running sums (owner, 2026-10-25), at each sample
//        a2 += s_k^2 (s_k^2 + 2 sum_{m=1..6} rho_m^2 s_(k-m)^2),   b2 += s_k^3 (s_k^3 + 2 sum_{m=1..6} rho_m s_(k-m)^3),
//    s_(k-m) the story's own template at its previous 6 samples (0 before the restart). Checked by simulation on the
//    chain's correlation (NoiseVariance.MatchesSimulation).
// 6. The variant's Omega_bar (omega_bar_variant_direct; derived). The model: Omega_hat = Omega G + N, G ~ Gamma (shape
//    nu, mean 1), N Gaussian (mean 0, variance A + B Omega G), the default's prior flat in ln Omega on [Omega_lo,
//    Omega_hi] (stage 3). Counting powers in units of SE0 = sqrt(A) = sigma_y^2 sqrt(a2) / sum s^4 (FS^2), the
//    inputs are nu, chi = Omega_hat / SE0, zeta = SE0 / sigma_y^2 and lambda = B / SE0 = 2 b2 / (sum s^4 sqrt(a2))
//    (= kappa zeta, kappa = 2 b2 / a2): the likelihood's variance is SE0^2 (1 + lambda P / SE0) at realized power P.
//    With P = Omega G the realized power, the integral over Omega at fixed P is a partial moment of the gamma
//    (derived):
//        Omega_bar = integral phi(P) E[1/G; G in (P/Omega_hi, P/Omega_lo)] dP
//                    / integral phi(P) Pr[G in (P/Omega_hi, P/Omega_lo)] / P dP,
//        phi(P) = Normal(Omega_hat - P; 0, A + B P),
//    the gamma's partial moments from the regularized incomplete gamma functions (ln P(a, x), ln Q(a, x): series and
//    continued fraction; for nu - 1 < 0.5 the unregularized window integral of t^(nu - 2) e^(-t) directly, finite at
//    nu = 1), and at nu = infinity G = 1. The numerics (heuristic constants, their effect measured):
//    - Both integrals run over u = ln(P / SE0), in log space (a likelihood can sit thousands of nats below 1), from
//      10^-16 SE0 (the integrands fall at least as e^u below their peaks; derived) to where phi is below e^-700 of its
//      largest value, P = (max(chi, 0) + 1 500 lambda + 40 sqrt(1 + lambda max(chi, 0)) + 100) SE0 (derived), or to
//      4 Omega_lo if that is further (above it the window is near 1 and phi only falls); at nu = infinity over the
//      prior's range only.
//    - Breakpoints: every 0.25 in u; about phi's peak where it is narrower than that (its width in u is
//      sqrt(1 + lambda chi) / chi); at Omega_lo and Omega_hi and, above nu = 16, at 1 +/- k / sqrt(nu) times them
//      (k = 1 ... 6: the gamma window's edges). Each log-integrand's peak is then found by golden-section search
//      between the best breakpoint's neighbors, its value taken as the reference of that integrand's exponentials,
//      and where the peak is narrower than 0.25 (scale 1 / (|g'| + sqrt|g''|) in u, g the log-integrand) breakpoints
//      are added at 0.5 to 32 scales about it (a peak at the prior's edge at nu = infinity needs it), so that every
//      narrow feature lies at a breakpoint. Should a quadrature node still pass a reference by more than 1 nat, the
//      reference moves up and the sums are taken again.
//    - Panels whose ends are both more than 90 nats below their integrand's peak are left out (they change the
//      integrals by under e^-80). Each panel is summed by 10-point Gauss-Legendre on its two halves, its error
//      estimated against the rule on the whole panel, and the panel of the largest share of the error is halved until
//      both integrals' estimated errors are below 10^-11 of the integrals.
//    - Against initial panels of 0.125, a tolerance of 10^-13, a lower end of 10^-20 SE0 and panels dropped only 150
//      nats below the peak, the result moved by at most 6.8 x 10^-14 relative on 12 696 points: 4 000 of
//      TableMatchesTheDirectIntegral's distribution, 2 000 at random over the table's whole range at its nu nodes and
//      6 696 at every nu node with the axes' ends and inner values of chi, zeta and lambda (measured in scratch;
//      Task 8's report). About 0.4 to 1.5 ms per call on the desktop (measured).
// 7. The table (OmegaBarVariantTable; heuristic grid, its error measured against the direct integral). Four inputs:
//    upsilon = 1/nu at 31 nodes uniform in p, upsilon = 1 - (1 - p)^2 (p = 1 - sqrt(1 - 1/nu): denser toward nu = 1);
//    asinh(chi) uniform on [asinh(-8), asinh(10^4)] at 85 nodes (steps of 0.15); ln(zeta) uniform on [ln 0.003,
//    ln 400] at 63 nodes (steps of 0.19); ln(lambda) uniform on [ln 10^-3, ln 1 120] at 57 nodes (steps of 0.25).
//    Output ln(Omega_bar / sigma_y^2), single precision, layout [upsilon][chi][zeta][lambda], multilinear in (p,
//    asinh chi, ln zeta, ln lambda) over the 16 corners; 9 462 285 values, 37.8 MB.
//    - The design's grid (part 7: zeta at 42 nodes, lambda at 29, 12.8 MB; heuristic, chosen there from each axis's
//      own interpolation error) met the 99th percentile (0.80% on the generator's 4 000 points) but not the largest
//      error, 5.7% (bound 3%; plan 1b, Task 8). Two sources (measured, Task 8's report): along lambda, Omega_bar's
//      rise toward Omega_hat's floor (chi from -5 to -8, zeta 0.15 to 0.4) bends by up to 8% of Omega_bar within one
//      0.50 step of ln(lambda); and the prior's lower corner, Omega_hat = Omega_lo, at large nu and zeta below 0.05,
//      where the corner, at chi = 0.21 / zeta, runs diagonally across the (asinh chi, ln zeta) cells. Halving the
//      lambda step and taking two thirds of the zeta step is the smallest of the grids tried that meets both bounds on
//      every draw (7 draws of the 4 000 points: 99th percentile 0.50 to 0.53%, largest 1.8 to 2.7%); lambda at 43
//      nodes failed one draw (3.2%), and more p nodes, Task 7's p = 1 / (1 + sqrt(nu - 1)) or more chi nodes did not
//      help on their own. On points crowded onto the corner (zeta 0.003 to 0.05, nu 15 to 1 000, Omega_hat within a
//      factor of 2 of Omega_lo) the largest error is 3.2% (measured), a region the design's points rarely reach.
//    - Its error against the direct integral on the generator's 4 000 points (seed 1; design part 7's two halves):
//      |relative error| median 0.055%, 99th percentile 0.55%, largest 2.08% (at nu = 38.1, chi = 24.3, zeta = 0.0077,
//      kappa = 0.53: the lower corner); first marks (zeta >= 4) 0.45% and 0.80%; chi > 30 0.19% and 1.46%; kappa = 0
//      0.68% and 0.80% (99th percentile and largest; measured, the generator prints it by region). On
//      TableMatchesTheDirectIntegral's 400 points 0.44% and 0.97%. Its build: about 56 min on 7 threads of the desktop.
//    - Beyond the axes: chi above 10^4 reads the default's table (stage 4: the variant's direct integral approaches the
//      default's closed form as about 1 - 1/chi^2, derived, and is within 10^-4 of it at chi = 10^4, measured,
//      DirectIntegralLimits; the default's table is within 0.1% of the closed form); chi below -8 reads -8 (only zeta
//      below about 0.26 reaches it; Omega_bar is flat there, design part 7); lambda below 10^-3, 0 included, reads
//      10^-3 (moves Omega_bar by 0.076% at most, design part 7); zeta is clamped to its axis (Task 11 counts it).
//    - Generated by kz4ap-viterbi-tables omega-bar-variant into build/ and loaded (Ruling 65), never compiled in.
//    - A side table of 31 x 63 x 57 (upsilon, zeta, lambda) holds the chi where Omega_bar stops falling as chi falls,
//      the minimum of Omega_bar over chi in [-8, 10^4] (-8 when Omega_bar still falls there), by golden-section search
//      on the direct integral (interpolated, within 0.03 of the direct integral's minimum on part 7's cases, where
//      Omega_bar is flat: 0.4% over one unit of chi; measured): below it Omega_bar rises again, an artifact of taking
//      N as Gaussian (design part 7: the true likelihood has a monotone likelihood ratio); item 17 counts how often
//      Omega_hat falls below it.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kz4ap::viterbi {

// Hz: the bandwidth of the S500 level reference, Omega = 10^(L/10) N0 x 500 Hz at L dB S500.
inline constexpr double kS500BandwidthHz = 500.0;
// Hz: sigma_y^2 / N0 of the 192 kHz chain (output_noise; derived), with which Omega_bar's prior bounds in dB S500 are
// placed against sigma_y^2. The 48 kHz chain's differs by 0.03%, which moves the bounds by 0.0013 dB.
inline constexpr double kOmegaBarSigma2PerN0Hz = 74.4998;

// One story's long-run sums (header, stages 1 and 2).
class LongRun {
public:
    // spread_hz: f_D (Hz, >= 0; 0: no fading, rho^2 = 1); bin_samples: the bin's length in samples (16);
    // bins_kept: the closed bins a new bin's cross terms reach back over (10); sample_interval_s: Delta (s). Throws
    // std::invalid_argument for a negative or non-finite spread, bin_samples or bins_kept below 1, or a non-positive or
    // non-finite interval.
    LongRun(double spread_hz, int bin_samples, int bins_kept, double sample_interval_s);

    // Forgets every sum and the open bin (an over's start, a rescue newborn).
    void restart();
    // The sample k (the channel's sample index, from the stream's start) the next add() belongs to: when k lies in a
    // later bin than the open one (floor(k / bin_samples) larger), the open bin closes and that bin opens. Called before
    // add() at every sample with s != 0 (at others it is harmless). Throws std::invalid_argument for a k in an earlier
    // bin than the open one.
    void at_sample(std::int64_t k);
    // Adds one sample to the open bin and the sums: s the template (1), e_fs the envelope (FS), sigma2_fs2 sigma_y^2 at
    // the sample (FS^2). s = 0 adds nothing. Throws std::logic_error when no at_sample() came since the restart.
    void add(double s, double e_fs, double sigma2_fs2);

    // FS^2: Omega_hat; 0 before any sample with s != 0.
    double omega_hat_fs2() const;
    // nu (1), the open bin included; 1 before any sample with s != 0 (the value on one bin).
    double nu() const;
    // sum s^4 and sum s^2 over the samples since the restart (1).
    double sum_s4() const;
    double sum_s2() const;

private:
    struct Sum {  // a compensated running sum (Neumaier)
        double sum = 0.0;
        double carry = 0.0;
        void add(double x);
        double value() const { return sum + carry; }
    };
    int bin_samples_;
    int bins_kept_;
    std::vector<double> rho2_;  // rho^2(m b), m = 0 ... bins_kept
    Sum numerator_;             // sum s^2 (e^2 - sigma^2), FS^2
    Sum s4_;
    Sum s2_;
    Sum closed_w_;              // sum of the closed bins' W
    Sum closed_pairs_;          // the double sum over the closed bins
    bool open_ = false;
    std::int64_t open_bin_ = 0;
    double open_w_ = 0.0;       // the open bin's W
    double open_cross_ = 0.0;   // C of the open bin: sum over kept bins of W_j rho^2
    std::vector<std::int64_t> kept_bin_;  // the last bins_kept closed bins with W > 0, a ring
    std::vector<double> kept_w_;
    int kept_next_ = 0;
    int kept_count_ = 0;
};

// FS^2: Omega_bar by the closed form (header, stage 3) for nu (> 0), Omega_hat (FS^2; clipped at 0), sigma_y^2 (FS^2,
// > 0) and the prior's bounds lo_db < hi_db (dB S500); nu = infinity gives Omega_hat_+ clipped to the prior's range.
// beta = 0 by the exact expression in the header; otherwise by quadrature in y = ln(Omega / Omega_hi): the posterior's
// log-density g(y) = -nu y - (beta / Omega_hi) e^(-y) is concave; the integrals run over where g is within 45 + l nats
// of its peak (what lies beyond changes the ratio by about 10^-15 at most, a derived bound), by composite 10-point
// Gauss-Legendre with panels of length 2 / (1 + |g'| + sqrt(|g''|)) taken at each panel's start (heuristic; halving
// them moves the result by at most 8.2 x 10^-13 relative on 20 000 random points with nu up to 10^6, measured; about
// 17 us per call). Throws std::invalid_argument for a nu that is not positive, a non-positive or non-finite sigma2_fs2,
// a Omega_hat that is not a number, or bounds that are not finite and ordered.
double omega_bar_closed_form(double nu, double omega_hat_fs2, double sigma2_fs2, double lo_db, double hi_db);

// The table's grid (header, stage 4), relative to the prior's bounds: l = ln(Omega_hi / Omega_lo).
struct OmegaBarLayout {
    int p_nodes = 97;               // nodes of p, uniform on [0, 1]
    int zero_row_nodes = 257;       // nodes of p of the beta = 0 row, uniform on [0, 1]
    double below_lo = 9.0;          // z_min = -l - below_lo (Omega_hat = 1.2 x 10^-4 Omega_lo)
    double z_max = 8.0;             // z read at z_max above it
    double fine_half_width = 0.5;   // the fine segments: within this of z = -l and z = 0
    double fine_step = 0.01;        // their step, at most
    double medium_half_width = 4.0; // the medium segments: from fine_half_width out to this of each corner
    double medium_step = 0.06;      // their step, at most
    double coarse_step = 0.25;      // the other segments' step, at most
    friend bool operator==(const OmegaBarLayout&, const OmegaBarLayout&) = default;
};

class OmegaBarTable {
public:
    // The compiled table, for -15 and +85 dB S500 (generated: engine/src/viterbi/omega_bar_table.cpp).
    static OmegaBarTable compiled();
    // A table written by save() (kz4ap-viterbi-tables omega-bar with other bounds). Throws std::runtime_error for a file
    // that cannot be read or is not such a table.
    static OmegaBarTable load(const std::string& path);
    // The table for the bounds (dB S500) on the layout, every node from omega_bar_closed_form (about 1 s).
    static OmegaBarTable build(double lo_db, double hi_db, const OmegaBarLayout& layout);
    // A table from its values: zero_row the beta = 0 row (p_nodes values), values p-major (p_nodes x z_nodes values),
    // each ln(Omega_bar / Omega_hi). Throws std::invalid_argument for bounds that are not finite and ordered, a layout
    // that cannot be built (fewer than 2 p nodes, steps or widths not positive, z_max <= fine_half_width,
    // l <= 2 fine_half_width) or values of the wrong count.
    OmegaBarTable(double lo_db, double hi_db, const OmegaBarLayout& layout, std::vector<double> zero_row,
                  std::vector<double> values);

    // Writes the table (bounds, layout and values; little-endian doubles) to path. Throws std::runtime_error.
    void save(const std::string& path) const;

    double lo_db() const { return lo_db_; }  // dB S500
    double hi_db() const { return hi_db_; }  // dB S500
    const OmegaBarLayout& layout() const { return layout_; }
    const std::vector<double>& zero_row() const { return zero_row_; }
    const std::vector<double>& values() const { return values_; }
    // The z nodes (ln(Omega_hat_+ / Omega_hi)) and the p nodes.
    const std::vector<double>& z_nodes() const { return z_nodes_; }
    double p_node(int i) const { return static_cast<double>(i) / (layout_.p_nodes - 1); }

    // FS^2: Omega_bar for nu (1), Omega_hat (FS^2) and sigma_y^2 (FS^2, > 0): bilinear in (p, z) (header, stage 4); a
    // nu below 1 or not a number reads as 1, a Omega_hat that is not a number as 0. About 30 to 40 ns (rough
    // estimate: a square root, a division, a logarithm, an exponential and four table reads). Throws
    // std::invalid_argument for a non-positive or non-finite sigma2_fs2.
    double omega_bar_fs2(double nu, double omega_hat_fs2, double sigma2_fs2) const;

    // p (1) for nu: 1 / (1 + sqrt(nu - 1)), nu >= 1 (0 at infinity).
    static double p_of_nu(double nu);
    // nu for p in (0, 1]: 1 + ((1 - p) / p)^2.
    static double nu_of_p(double p);

private:
    double lo_db_;
    double hi_db_;
    OmegaBarLayout layout_;
    double l_;                     // ln(Omega_hi / Omega_lo)
    double hi_per_sigma2_;         // Omega_hi / sigma_y^2 (1)
    double z_min_;
    double ratio_min_;             // e^z_min
    static constexpr std::size_t kSegments = 9;
    std::array<double, kSegments + 1> breaks_;  // the segments' ends in z
    std::array<int, kSegments + 1> first_;      // the index of each segment's first node (and one past the last)
    std::array<double, kSegments> step_;        // each segment's step in z
    std::vector<double> z_nodes_;
    std::vector<double> zero_row_;
    std::vector<double> values_;
};

// --- The noise-in-Omega_bar variant (header, stages 5 to 7) ---

// One story's two noise-variance sums (header, stage 5).
class NoiseVarianceSums {
public:
    // Forgets both sums and the last 6 template values (an over's start, a rescue newborn).
    void restart();
    // One sample: s the story's template (1), rho the noise correlation rho_0 = 1 ... rho_6 at lags of Delta (1). Called
    // at every sample, s = 0 included (it adds nothing to the sums but enters the history).
    void add(double s, const std::array<double, 7>& rho);
    double a2() const { return a2_.sum + a2_.carry; }  // (1)
    double b2() const { return b2_.sum + b2_.carry; }  // (1)

private:
    struct Sum {  // a compensated running sum (Neumaier)
        double sum = 0.0;
        double carry = 0.0;
        void add(double x);
    };
    Sum a2_;
    Sum b2_;
    std::array<double, 6> s2_{};  // s^2 at the previous 6 samples, [0] the last
    std::array<double, 6> s3_{};  // s^3 there
};

// Omega_bar / sigma_y^2 (1) of the variant by the direct integral (header, stage 6), at the default's prior bounds (-15
// and +85 dB S500), for nu >= 1 (infinity allowed; below 1 reads as 1), chi = Omega_hat / SE0 (finite), zeta =
// SE0 / sigma_y^2 (> 0) and lambda = B / SE0 (>= 0). About 0.4 to 1.5 ms per call. Throws std::invalid_argument for an
// argument that is not a number, a non-finite chi or zeta, zeta <= 0 or lambda < 0.
double omega_bar_variant_direct(double nu, double chi, double zeta, double lambda);

class OmegaBarVariantTable {
public:
    // The axes (header, stage 7): each axis's ends, in its physical quantity, and its node count. The defaults are this
    // build's; load() accepts only them.
    struct Axes {
        int upsilon_nodes = 31;   // p uniform on [0, 1], upsilon = 1/nu = 1 - (1 - p)^2
        double chi_min = -8.0;    // chi: uniform in asinh(chi)
        double chi_max = 1e4;
        int chi_nodes = 85;
        double zeta_min = 0.003;  // zeta: uniform in ln(zeta)
        double zeta_max = 400.0;
        int zeta_nodes = 63;
        double lambda_min = 1e-3;  // lambda: uniform in ln(lambda)
        double lambda_max = 1120.0;
        int lambda_nodes = 57;
        friend bool operator==(const Axes&, const Axes&) = default;
    };

    // A table from its values (for tests and the generator): values ln(Omega_bar / sigma_y^2) at every node, layout
    // [upsilon][chi][zeta][lambda]; minimum_chi the side table's chi (1), layout [upsilon][zeta][lambda]. The prior's
    // bounds are the default's (-15 and +85 dB S500). Throws std::invalid_argument for an axis with fewer than 2 nodes,
    // ends that are not finite and ordered (zeta_min and lambda_min positive), or values of the wrong count.
    OmegaBarVariantTable(const Axes& axes, std::vector<float> values, std::vector<float> minimum_chi);

    // A table written by save(). Throws std::runtime_error for a file that cannot be read, or whose header (magic,
    // version, axes, prior bounds, sigma_y^2 / N0) differs from this build's, naming what differs.
    static OmegaBarVariantTable load(const std::string& path);
    // Writes the table: the magic, version 1, sigma_y^2 / N0 (Hz), the prior's bounds (dB S500), the axes, the values
    // and the side table (little-endian). Throws std::runtime_error.
    void save(const std::string& path) const;

    const Axes& axes() const { return axes_; }
    const std::vector<float>& values() const { return values_; }
    const std::vector<float>& minimum_chi_values() const { return minimum_chi_; }

    // FS^2: Omega_bar for nu (1; below 1 or not a number reads as 1, infinity as upsilon = 0), Omega_hat (FS^2), the
    // story's a2 and b2 (NoiseVarianceSums), sum s^4 (LongRun), sigma_y^2 (FS^2, > 0), with SE0 = sigma_y^2 sqrt(a2) /
    // sum s^4, chi = Omega_hat / SE0, zeta = SE0 / sigma_y^2, lambda = 2 b2 / (sum s^4 sqrt(a2)) (header, stage 7). chi
    // above chi_max, and a story with no sample yet (a2 or sum s^4 not positive), read the fallback, the default's
    // table, which must have the default's prior bounds; a Omega_hat that is not a number reads as 0. Throws
    // std::invalid_argument for a non-positive or non-finite sigma2_fs2, or a fallback for other bounds.
    double omega_bar_fs2(double nu, double omega_hat_fs2, double a2, double b2, double sum_s4, double sigma2_fs2,
                         const OmegaBarTable& fallback) const;
    // chi (1) below which Omega_bar rises again, for nu, zeta and lambda (clamped as in omega_bar_fs2): trilinear in the
    // side table.
    double minimum_chi(double nu, double zeta, double lambda) const;

    // The nodes: p_i = i / (upsilon_nodes - 1) and nu = 1 / (1 - (1 - p)^2) (infinity at p = 0); chi, zeta and lambda at
    // their nodes.
    double nu_node(int i) const;
    double chi_node(int j) const;
    double zeta_node(int k) const;
    double lambda_node(int m) const;
    // p (1) of nu: 1 - sqrt(1 - 1/nu), nu >= 1 (0 at infinity; 1 for nu below 1 or not a number).
    static double p_of_nu(double nu);

private:
    struct Position {  // a node index and the fraction toward the next
        int i;
        double f;
    };
    Position upsilon_position(double nu) const;
    Position chi_position(double chi) const;
    Position zeta_position(double zeta) const;
    Position lambda_position(double lambda) const;

    Axes axes_;
    double asinh_chi_min_;
    double asinh_chi_step_;
    double ln_zeta_min_;
    double ln_zeta_step_;
    double ln_lambda_min_;
    double ln_lambda_step_;
    std::vector<float> values_;
    std::vector<float> minimum_chi_;
};

}  // namespace kz4ap::viterbi
