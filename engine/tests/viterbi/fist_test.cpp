#include "kz4ap/viterbi/fist.hpp"

#include "kz4ap/viterbi/config.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

using namespace kz4ap::viterbi;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNoReference = std::numeric_limits<double>::quiet_NaN();

constexpr std::array<Kind, 5> kKinds = {{Kind::Dot, Kind::Dash, Kind::ElementGap, Kind::LetterGap, Kind::WordGap}};

const char* name_of(Kind kind) {
    switch (kind) {
        case Kind::Dot: return "Dot";
        case Kind::Dash: return "Dash";
        case Kind::ElementGap: return "ElementGap";
        case Kind::LetterGap: return "LetterGap";
        case Kind::WordGap: return "WordGap";
    }
    return "?";
}

std::string describe(Kind kind, int speed) { return std::string(name_of(kind)) + ", speed " + std::to_string(speed); }

// ln of the standard normal's upper tail, ln(1 - Phi(z)), by std::erfc.
double ln_upper_tail(double z) { return std::log(0.5 * std::erfc(z / std::sqrt(2.0))); }

// The model of the design notes (topic 2, items 1, 2 and 4), restated from the configuration alone: what the Fist is
// checked against. A kind's length, in pieces, is shift + a log-normal of median M and standard deviation sigma in
// ln length; the length is rounded to the nearest piece and is never shorter than one piece.
struct Model {
    ViterbiConfig config;

    double piece_ms() const { return config.piece_s * 1000.0; }
    double dot_ms(int speed) const { return config.dot_min_ms * std::pow(config.speed_ratio, speed); }
    double median_pieces(Kind kind, int speed) const {
        return config.fist_medians_dots[static_cast<std::size_t>(kind)] * dot_ms(speed) / piece_ms();
    }
    double shift_pieces(Kind kind, int speed) const {
        return config.fist_shifts_dots[static_cast<std::size_t>(kind)] * dot_ms(speed) / piece_ms();
    }
    // (ln(edge - shift) - ln M) / sigma for a length edge in pieces; -infinity at or below the shift.
    double z(Kind kind, int speed, double edge) const {
        const double above_shift = edge - shift_pieces(kind, speed);
        if (!(above_shift > 0.0)) return -kInf;
        return (std::log(above_shift) - std::log(median_pieces(kind, speed))) /
               config.fist_sigmas[static_cast<std::size_t>(kind)];
    }
    // ln S(tau): S(1) = 1 and S(tau) = 1 - F(tau - 1/2) for tau >= 2. NaN where S is below what std::erfc holds.
    double log_survival(Kind kind, int speed, int tau) const {
        if (tau == 1) return 0.0;
        const double survival = 0.5 * std::erfc(z(kind, speed, tau - 0.5) / std::sqrt(2.0));
        return survival > 0.0 ? std::log(survival) : kNoReference;
    }
    // ln P(tau): P(1) = F(3/2) and P(tau) = F(tau + 1/2) - F(tau - 1/2) for tau >= 2, each mass taken from the
    // tail it lies in, so that a tiny mass keeps its relative accuracy. -infinity where no length lies in the
    // interval (the mass is 0); NaN where the mass is positive but below what std::erfc holds.
    double log_end(Kind kind, int speed, int tau) const {
        const double a = tau == 1 ? -kInf : z(kind, speed, tau - 0.5);
        const double b = z(kind, speed, tau + 0.5);
        if (b == -kInf) return -kInf;
        const double root2 = std::sqrt(2.0);
        double mass;
        if (b <= 0.0)
            mass = 0.5 * std::erfc(-b / root2) - 0.5 * std::erfc(-a / root2);  // Phi(b) - Phi(a)
        else if (a >= 0.0)
            mass = 0.5 * std::erfc(a / root2) - 0.5 * std::erfc(b / root2);  // Phi_c(a) - Phi_c(b)
        else
            mass = 1.0 - 0.5 * std::erfc(-a / root2) - 0.5 * std::erfc(b / root2);  // across the median
        return mass > 0.0 ? std::log(mass) : kNoReference;
    }
};

// EXPECT_NEAR that also accepts -infinity on both sides, and, where the reference is out of std::erfc's reach (NaN),
// a finite value beneath e^-600.
void expect_close(double actual, double expected, double tolerance, const std::string& what) {
    if (std::isnan(expected))
        EXPECT_TRUE(std::isfinite(actual) && actual < -600.0) << what << ": " << actual;
    else if (std::isinf(expected))
        EXPECT_EQ(actual, expected) << what;
    else
        EXPECT_NEAR(actual, expected, tolerance) << what;
}

// The largest tau with S(tau) >= 1/2, which is round(M) for a median of M pieces (S(tau) = 1 - F(tau - 1/2)).
int median_from_survival(const Fist& fist, Kind kind, int speed) {
    const double ln_half = std::log(0.5);
    int lo = 1;         // S(1) = 1
    int hi = 1 << 24;   // far beyond every median here
    while (hi - lo > 1) {
        const int mid = lo + (hi - lo) / 2;
        if (fist.log_survival(kind, speed, mid) >= ln_half)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

// The ends of the grid, where lengths are shortest and longest: every kind at the first and last speeds (100 and 5.2
// WPM: dots of 12.0 and 230.3 ms). There a dot is 6 pieces (12 ms), a dash 691 ms (345 pieces) and a word gap 1 612 ms
// (806 pieces). visit(kind, speed, last) gets the longest length to try, 40 times the kind's median, in pieces.
template <class Visit>
void for_each_grid_end(const Model& model, Visit visit) {
    for (const Kind kind : kKinds) {
        for (const int speed : {0, 31}) {
            SCOPED_TRACE(describe(kind, speed));
            visit(kind, speed, static_cast<int>(std::ceil(40.0 * model.median_pieces(kind, speed))));
        }
    }
}

// The Fist's largest differences from the reference over the lengths first ... last: of ln S and of ln P, nats, the
// length of the largest ln P difference, and how many lengths the reference cannot give (not finite).
struct Worst {
    double ln_s = 0.0;
    double ln_p = 0.0;
    int at_ln_p = 0;
    int skipped = 0;
};

Worst worst_against_model(const Fist& fist, const Model& model, Kind kind, int speed, int first, int last) {
    Worst worst;
    for (int tau = first; tau <= last; ++tau) {
        const double ln_s = model.log_survival(kind, speed, tau);
        const double ln_p = model.log_end(kind, speed, tau);
        if (!std::isfinite(ln_s) || !std::isfinite(ln_p)) {
            ++worst.skipped;
            continue;
        }
        worst.ln_s = std::max(worst.ln_s, std::fabs(fist.log_survival(kind, speed, tau) - ln_s));
        const double error = std::fabs(fist.log_end(kind, speed, tau) - ln_p);
        if (error > worst.ln_p) {
            worst.ln_p = error;
            worst.at_ln_p = tau;
        }
    }
    return worst;
}

}  // namespace

TEST(Fist, EndProbabilitiesSumToOne) {
    const ViterbiConfig config{};
    const Fist fist(config);
    const Model model{config};
    for_each_grid_end(model, [&](Kind kind, int speed, int last) {
        double sum = 0.0;
        for (int tau = 1; tau <= last; ++tau) sum += std::exp(fist.log_end(kind, speed, tau));
        EXPECT_NEAR(sum, 1.0, 1e-9) << "up to " << last << " pieces";
    });
}

TEST(Fist, SurvivalIsTheRoundedLogNormal) {
    // A dot at speed 0 is 12.0 ms = 6 pieces of 2 ms, with sigma 0.3: S(tau) = Phi_c((ln(tau - 1/2) - ln 6) / 0.3).
    const Fist fist{ViterbiConfig{}};
    for (int tau = 1; tau <= 60; ++tau) {
        const double z = (std::log(tau - 0.5) - std::log(6.0)) / 0.3;
        EXPECT_NEAR(fist.log_survival(Kind::Dot, 0, tau), ln_upper_tail(z), 1e-6) << "tau " << tau;
    }
    // Never shorter than one piece: every element lasts the first.
    EXPECT_EQ(fist.log_survival(Kind::Dot, 0, 1), 0.0);
}

TEST(Fist, EndProbabilityIsTheRoundedLogNormal) {
    // P(1) = F(3/2), P(tau) = F(tau + 1/2) - F(tau - 1/2): every kind, at speeds with medians up to 109 pieces (the
    // word gap at speed 10, 7 dots of 31.1 ms), over lengths up to 40 times the median. The Fist's table makes ln P
    // agree to 9.1e-6 here (measured); the test allows 5e-5.
    const ViterbiConfig config{};
    const Fist fist(config);
    const Model model{config};
    for (const Kind kind : kKinds) {
        for (const int speed : {0, 4, 10}) {
            SCOPED_TRACE(describe(kind, speed));
            const int last = static_cast<int>(std::ceil(40.0 * model.median_pieces(kind, speed)));
            for (int tau = 1; tau <= last; ++tau)
                expect_close(fist.log_end(kind, speed, tau), model.log_end(kind, speed, tau), 5e-5,
                             "tau " + std::to_string(tau));
        }
    }
}

TEST(Fist, MediansAreStandardSpacing) {
    // Standard spacing at every speed (the owner's decision D14 of 2026-10-21 removed the Farnsworth factor): the
    // medians are the notes' 1, 3, 1, 3 and 7 dots, a letter gap 3 dots and a word gap 7, to the nearest piece of
    // 2 ms. The median found from S is round(M) for a median of M pieces; the table's 1.25e-7 nats in ln S move the
    // crossing of S = 1/2 by at most 5e-8 M pieces (ln S falls by 2.66 / M nats per piece there, at sigma 0.3).
    const ViterbiConfig config{};
    const Fist fist(config);
    const Model model{config};
    const std::array<double, 5> dots = {{1.0, 3.0, 1.0, 3.0, 7.0}};
    for (int speed = 0; speed < config.speed_count; ++speed) {
        for (const Kind kind : kKinds) {
            SCOPED_TRACE(describe(kind, speed));
            const double median = dots[static_cast<std::size_t>(kind)] * model.dot_ms(speed) / 2.0;  // pieces
            EXPECT_LE(std::fabs(median_from_survival(fist, kind, speed) - median), 0.5 + 1e-4);
        }
    }
    // At the first speed, dots of 6 pieces: 6, 18, 6, 18 and 42 pieces.
    EXPECT_EQ(median_from_survival(fist, Kind::Dot, 0), 6);
    EXPECT_EQ(median_from_survival(fist, Kind::Dash, 0), 18);
    EXPECT_EQ(median_from_survival(fist, Kind::ElementGap, 0), 6);
    EXPECT_EQ(median_from_survival(fist, Kind::LetterGap, 0), 18);
    EXPECT_EQ(median_from_survival(fist, Kind::WordGap, 0), 42);
    // The longest median: a word gap, 7 dots of 230.3 ms (speed 31), is 806 pieces of 2 ms.
    EXPECT_EQ(median_from_survival(fist, Kind::WordGap, 31), 806);
}

TEST(Fist, MisfitCostsMatchTheNotes) {
    // Speed 31: a dot of 12.0 ms x 1.1^31 = 230.3 ms, a median of 115.17 pieces. The notes' misfit cost of a length
    // k times the median is (ln k)^2 / (2 sigma^2) nats, to which the log-normal density adds its 1/length factor,
    // ln k: ln P(k M) - ln P(M) = -(ln k)^2 / 0.18 - ln k at sigma 0.3.
    const Fist fist{ViterbiConfig{}};
    EXPECT_EQ(median_from_survival(fist, Kind::Dot, 31), 115);
    const auto ln_p = [&fist](int tau) { return fist.log_end(Kind::Dot, 31, tau); };
    const double ln2 = std::log(2.0);
    const double ln3 = std::log(3.0);
    const double twice = -(ln2 * ln2) / 0.18 - ln2;
    const double thrice = -(ln3 * ln3) / 0.18 - ln3;
    EXPECT_NEAR(twice, -3.36, 0.005);
    EXPECT_NEAR(thrice, -7.80, 0.005);
    EXPECT_NEAR(ln_p(230) - ln_p(115), twice, 0.05);
    EXPECT_NEAR(ln_p(345) - ln_p(115), thrice, 0.05);
}

TEST(Fist, EveryKindFollowsItsOwnMedianSigmaAndShift) {
    // The genie fist: the speed grid collapsed to one speed (an 80 ms dot, 40 pieces) and, for each kind, the
    // sender's own median, sigma and shift.
    ViterbiConfig config{};
    config.speed_count = 1;
    config.dot_min_ms = 80.0;
    config.fist_medians_dots = {{1.1, 3.4, 0.9, 3.0, 6.0}};
    config.fist_sigmas = {{0.10, 0.15, 0.20, 0.25, 0.35}};
    config.fist_shifts_dots = {{0.05, -0.05, -0.10, 0.20, 0.30}};
    const Fist fist(config);
    const Model model{config};
    for (const Kind kind : kKinds) {
        SCOPED_TRACE(describe(kind, 0));
        const int last = static_cast<int>(std::ceil(20.0 * model.median_pieces(kind, 0)));
        for (int tau = 1; tau <= last; ++tau) {
            const std::string at = "tau " + std::to_string(tau);
            expect_close(fist.log_survival(kind, 0, tau), model.log_survival(kind, 0, tau), 1e-6, at);
            expect_close(fist.log_end(kind, 0, tau), model.log_end(kind, 0, tau), 1e-4, at);
        }
    }
    // A shift of 0.2 dots = 8 pieces: a letter gap is never shorter than 8 pieces, so nothing ends before the 8th
    // piece and everything survives to it.
    for (int tau = 1; tau <= 7; ++tau) {
        EXPECT_EQ(fist.log_end(Kind::LetterGap, 0, tau), -kInf) << "tau " << tau;
        EXPECT_EQ(fist.log_survival(Kind::LetterGap, 0, tau), 0.0) << "tau " << tau;
    }
    EXPECT_GT(fist.log_end(Kind::LetterGap, 0, 8), -kInf);
    // The shift moves the whole length: the letter gap's median is 3.0 x 40 = 120 pieces above the 8-piece shift.
    EXPECT_NEAR(median_from_survival(fist, Kind::LetterGap, 0), 8.0 + 120.0, 1.0);
}

TEST(Fist, SurvivalAndEndStayFiniteInTheFarTail) {
    // A dot at speed 0 (median 6 pieces, sigma 0.3) held for ever longer: z = (ln(tau - 1/2) - ln 6) / 0.3 reaches 38,
    // where Phi_c is about 1e-316, a subnormal double (past z = 38.5 it is 0 and std::erfc is of no use), and then 45
    // and 60, past the table's end at z = 40. The reference values are from an 80-digit evaluation (Python's decimal,
    // the continued fraction of Mills' ratio) of ln Phi_c(z(tau - 1/2)) and of ln(Phi_c(z(tau - 1/2)) -
    // Phi_c(z(tau + 1/2))).
    struct Row {
        int tau;
        double z;
        double ln_s;
        double ln_p;
    };
    const Row rows[] = {
        {27, 4.9512842125, -1.48134813752278e+1, -1.55606020062242e+1},
        {220, 11.9986425443, -7.53942728461506e+1, -7.71818617789216e+1},
        {10883, 25.0105060140, -3.16902532365034e+2, -3.21776456264543e+2},
        {535900, 37.9998081775, -7.26549921740523e+2, -7.34899497525405e+2},
        {4400000, 45.0178517201, -1.01802997722313e+3, -1.02831558462550e+3},
        {390000000, 59.9663260886, -1.80299313188451e+3, -1.81747675556396e+3},
    };
    const Fist fist{ViterbiConfig{}};
    for (const Row& row : rows) {
        SCOPED_TRACE("tau " + std::to_string(row.tau) + ", z " + std::to_string(row.z));
        const bool in_table = row.z < 40.0;
        EXPECT_NEAR(fist.log_survival(Kind::Dot, 0, row.tau), row.ln_s, in_table ? 1.25e-7 : 1e-8);
        EXPECT_NEAR(fist.log_end(Kind::Dot, 0, row.tau), row.ln_p, in_table ? 5e-5 : 1e-5);
    }
}

TEST(Fist, SurvivalFallsFromOneOverTheGridsWholeRange) {
    // At the ends of the grid, from the first piece to 40 times the median: ln S starts at 0 (S(1) = 1), is finite, is
    // never above 0 and never rises, and has fallen far by the end (S there is about e^-79, z = ln 40 / sigma = 12.3).
    const ViterbiConfig config{};
    const Fist fist(config);
    const Model model{config};
    for_each_grid_end(model, [&](Kind kind, int speed, int last) {
        double previous = fist.log_survival(kind, speed, 1);
        EXPECT_EQ(previous, 0.0);
        int wrong_at = 0;
        for (int tau = 2; tau <= last && wrong_at == 0; ++tau) {
            const double ln_s = fist.log_survival(kind, speed, tau);
            if (!std::isfinite(ln_s) || ln_s > 0.0 || ln_s > previous) wrong_at = tau;
            previous = ln_s;
        }
        EXPECT_EQ(wrong_at, 0) << "ln S is not finite, is above 0 or rises from the length before, at this length";
        EXPECT_LT(previous, -75.0) << "ln S at " << last << " pieces";
    });
    // The extremes in time: a dash at 5.2 WPM lasts 691 ms (345 pieces of 2 ms), a word gap 1 612 ms (806 pieces).
    EXPECT_NEAR(median_from_survival(fist, Kind::Dash, 31) * 2.0, 691.0, 2.0);
    EXPECT_NEAR(median_from_survival(fist, Kind::WordGap, 31) * 2.0, 1612.0, 2.0);
}

TEST(Fist, EndProbabilityIsTheDropInSurvivalOverTheGridsWholeRange) {
    // The identity of fist.hpp, P(tau) = S(tau) - S(tau + 1), from the first piece to 40 times the median. Past the
    // median (S <= 1/2) P is taken from the same upper-tail values as S, so the two agree to rounding: 1e-9 of P is
    // allowed, 1.6e-13 measured. Up to the median they read different tail tables (S the table at z, P the lower
    // tail's at -z), and each read is off by the table's error: at most 1.25e-7 x Phi_c x |second derivative of ln
    // Phi_c|, which is 4.4e-8 of probability at the worst z (-0.48; 4.0e-8 from z = 0 on). Two edges, two reads each:
    // 1.7e-7 at most; 2e-7 is allowed, 7.3e-8 measured.
    const ViterbiConfig config{};
    const Fist fist(config);
    const Model model{config};
    const double ln_half = std::log(0.5);
    for_each_grid_end(model, [&](Kind kind, int speed, int last) {
        double worst_to_median = 0.0;
        double worst_past_median = 0.0;
        int at_to_median = 0;
        int at_past_median = 0;
        double ln_s = 0.0;  // ln S(1)
        for (int tau = 1; tau <= last; ++tau) {
            const double ln_s_next = fist.log_survival(kind, speed, tau + 1);
            const double end = std::exp(fist.log_end(kind, speed, tau));
            const double drop = std::exp(ln_s) - std::exp(ln_s_next);
            if (ln_s > ln_half) {
                if (std::fabs(end - drop) > worst_to_median) {
                    worst_to_median = std::fabs(end - drop);
                    at_to_median = tau;
                }
            } else if (std::fabs(end - drop) / drop > worst_past_median) {
                worst_past_median = std::fabs(end - drop) / drop;
                at_past_median = tau;
            }
            ln_s = ln_s_next;
        }
        EXPECT_LE(worst_to_median, 2e-7) << "up to the median, at tau " << at_to_median;
        EXPECT_LE(worst_past_median, 1e-9) << "past the median (relative), at tau " << at_past_median;
    });
}

TEST(Fist, MatchesTheReferenceAtTheGridsEnds) {
    // ln S within the interpolation's 1.25e-7 nats of the std::erfc reference at every length from 1 to 40 times the
    // median, at every end of the grid (measured at most 1.24e-7); ln P within 1.5e-7 M nats for a median of M
    // pieces (measured at most 1.05e-7 M, at M = 18; 6.1e-7 at 6 pieces, 1.1e-5 at 115, 4.2e-5 at 806, as fist.hpp
    // states).
    const ViterbiConfig config{};
    const Fist fist(config);
    const Model model{config};
    for_each_grid_end(model, [&](Kind kind, int speed, int last) {
        const Worst worst = worst_against_model(fist, model, kind, speed, 1, last);
        EXPECT_EQ(worst.skipped, 0) << "lengths the reference cannot give";
        EXPECT_LE(worst.ln_s, 1.25e-7);
        EXPECT_LE(worst.ln_p, 1.5e-7 * model.median_pieces(kind, speed)) << "at tau " << worst.at_ln_p;
    });
}

TEST(Fist, LnPIsWithinTheAccuracyTheHeaderStates) {
    // fist.hpp's figures for ln P, the worst over every length up to 40 times the median: 6.2e-7 nats for the dot at
    // 100 WPM (6 pieces), 1.1e-5 for the dot at 5.2 WPM (115 pieces), 4.2e-5 for the word gap at 5.2 WPM (806
    // pieces, measured 4.16e-5 at the median), the longest median, where the cell of a length in z (1 / (sigma tau) =
    // 4.1e-3 at the median, about 4 of the table's steps of 0.001) is narrowest and ln P is least exact.
    struct Stated {
        Kind kind;
        int speed;
        double ln_p;
    };
    const Stated stated[] = {{Kind::Dot, 0, 6.2e-7}, {Kind::Dot, 31, 1.1e-5}, {Kind::WordGap, 31, 4.2e-5}};
    const ViterbiConfig config{};
    const Fist fist(config);
    const Model model{config};
    for (const Stated& s : stated) {
        SCOPED_TRACE(describe(s.kind, s.speed));
        const int last = static_cast<int>(std::ceil(40.0 * model.median_pieces(s.kind, s.speed)));
        const Worst worst = worst_against_model(fist, model, s.kind, s.speed, 1, last);
        EXPECT_LE(worst.ln_p, s.ln_p) << "at tau " << worst.at_ln_p;
    }
    // The lengths around the longest median: 400 pieces (0.8 s) either side of 806.
    const int median = static_cast<int>(std::lround(model.median_pieces(Kind::WordGap, 31)));
    EXPECT_EQ(median, 806);
    const Worst around = worst_against_model(fist, model, Kind::WordGap, 31, median - 400, median + 400);
    EXPECT_EQ(around.skipped, 0);
    EXPECT_LE(around.ln_s, 1.25e-7);
    EXPECT_LE(around.ln_p, 4.2e-5) << "at tau " << around.at_ln_p;
}

TEST(Fist, RefusesWhatItCannotRead) {
    const auto refuses = [](const ViterbiConfig& config) { EXPECT_THROW((void)Fist(config), std::invalid_argument); };
    const double nan = std::numeric_limits<double>::quiet_NaN();
    ViterbiConfig config{};
    config.piece_s = 0.0;
    refuses(config);
    config = ViterbiConfig{};
    config.piece_s = nan;
    refuses(config);
    config = ViterbiConfig{};
    config.dot_min_ms = -12.0;
    refuses(config);
    config = ViterbiConfig{};
    config.speed_count = 0;
    refuses(config);
    config = ViterbiConfig{};
    config.speed_ratio = 0.0;
    refuses(config);
    config = ViterbiConfig{};
    config.fist_medians_dots[3] = 0.0;
    refuses(config);
    config = ViterbiConfig{};
    config.fist_sigmas[1] = 0.0;
    refuses(config);
    config = ViterbiConfig{};
    config.fist_sigmas[4] = nan;
    refuses(config);
    config = ViterbiConfig{};
    config.fist_shifts_dots[2] = nan;
    refuses(config);

    const Fist fist{ViterbiConfig{}};
    EXPECT_THROW(fist.log_survival(Kind::Dot, -1, 1), std::out_of_range);
    EXPECT_THROW(fist.log_survival(Kind::Dot, 32, 1), std::out_of_range);
    EXPECT_THROW(fist.log_end(Kind::Dot, 0, 0), std::out_of_range);
    EXPECT_THROW(fist.log_end(Kind::Dot, 0, -5), std::out_of_range);
    EXPECT_THROW(fist.log_end(static_cast<Kind>(5), 0, 1), std::out_of_range);
}

TEST(Config, BlockLengthFollowsTheSpread) {
    // The longest block T with (2/T^2) integral_0^T (T - tau) exp(-pi^2 f^2 tau^2 / 2) dtau >= 10^(-0.5/10) (0.5 dB of
    // signal energy), at a Doppler spread f: computed 2026-10-20; the notes round them to 78 ms, 195 ms, 390 ms and
    // 1.3 s (topic 4, item 9).
    EXPECT_NEAR(block_length_s(5.0, 0.5) * 1000.0, 78.07, 78.07 * 0.005);
    EXPECT_NEAR(block_length_s(2.0, 0.5) * 1000.0, 195.19, 195.19 * 0.005);
    EXPECT_NEAR(block_length_s(1.0, 0.5) * 1000.0, 390.37, 390.37 * 0.005);
    EXPECT_NEAR(block_length_s(0.3, 0.5) * 1000.0, 1301.25, 1301.25 * 0.005);
    // The configuration's own spread (1 Hz) and loss (0.5 dB).
    const ViterbiConfig config{};
    EXPECT_NEAR(block_length_s(config.fading_spread_hz, config.block_loss_db) * 1000.0, 390.37, 390.37 * 0.005);
}

TEST(Config, BlockLengthScalesAsTheInverseSpread) {
    // Exactly 1/f: the integral depends on f and T only through f T.
    const double at_one = block_length_s(1.0, 0.5);
    EXPECT_NEAR(block_length_s(4.0, 0.5), at_one / 4.0, at_one * 1e-12);
    EXPECT_NEAR(block_length_s(0.25, 0.5), at_one * 4.0, at_one * 1e-12);
    // A larger loss allows a longer block; a smaller one a shorter.
    EXPECT_GT(block_length_s(1.0, 1.0), at_one);
    EXPECT_LT(block_length_s(1.0, 0.1), at_one);
    // The block's average is what the loss allows, by the definition: (2/T^2) integral (T - tau) rho(tau) dtau, by
    // Simpson's rule at the T found, equals 10^(-loss/10).
    const double pi = std::numbers::pi;
    for (const double loss_db : {0.1, 0.5, 1.0, 3.0}) {
        const double spread_hz = 1.7;
        const double block = block_length_s(spread_hz, loss_db);
        const int n = 20000;  // an even number of steps
        const double step = block / n;
        double sum = 0.0;
        for (int i = 0; i <= n; ++i) {
            const double tau = i * step;
            const double weight = (i == 0 || i == n) ? 1.0 : (i % 2 != 0 ? 4.0 : 2.0);
            sum += weight * (block - tau) * std::exp(-0.5 * pi * pi * spread_hz * spread_hz * tau * tau);
        }
        const double average = 2.0 / (block * block) * sum * step / 3.0;
        EXPECT_NEAR(average, std::pow(10.0, -loss_db / 10.0), 1e-9) << loss_db << " dB of signal energy";
    }
    // No fading: a block is the whole mark.
    EXPECT_EQ(block_length_s(0.0, 0.5), kInf);
}

TEST(Config, BlockLengthRefusesNonsense) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW((void)block_length_s(-1.0, 0.5), std::invalid_argument);
    EXPECT_THROW((void)block_length_s(nan, 0.5), std::invalid_argument);
    EXPECT_THROW((void)block_length_s(kInf, 0.5), std::invalid_argument);
    EXPECT_THROW((void)block_length_s(1.0, 0.0), std::invalid_argument);
    EXPECT_THROW((void)block_length_s(1.0, -0.5), std::invalid_argument);
    EXPECT_THROW((void)block_length_s(1.0, nan), std::invalid_argument);
}

TEST(Config, DefaultsAreTheDesignTable) {
    const ViterbiConfig c{};
    EXPECT_EQ(c.piece_s, 0.002);
    EXPECT_EQ(c.dot_min_ms, 12.0);
    EXPECT_EQ(c.speed_ratio, 1.1);
    EXPECT_EQ(c.speed_count, 32);
    EXPECT_EQ(c.fist_medians_dots, (std::array<double, 5>{{1.0, 3.0, 1.0, 3.0, 7.0}}));
    EXPECT_EQ(c.fist_sigmas, (std::array<double, 5>{{0.3, 0.3, 0.3, 0.3, 0.3}}));
    EXPECT_EQ(c.fist_shifts_dots, (std::array<double, 5>{{0.0, 0.0, 0.0, 0.0, 0.0}}));
    EXPECT_EQ(c.beam_nats, 40.0);
    EXPECT_EQ(c.ceiling, 1700);
    EXPECT_EQ(c.fading_spread_hz, 1.0);
    EXPECT_EQ(c.block_loss_db, 0.5);
    EXPECT_EQ(c.keying_rise_s, 0.005);
    EXPECT_EQ(c.systematic_floor, 0.02);
    EXPECT_EQ(c.over_rate_per_s, 0.05);
    EXPECT_EQ(c.rescue_epsilon, 1.0e-3);
    EXPECT_EQ(c.word_end_p, 1.0 / 3.06);
    EXPECT_EQ(c.over_end_q, 1.0 / 24.0);
    EXPECT_EQ(c.level_min_db, -15.0);
    EXPECT_EQ(c.level_max_db, 85.0);
    EXPECT_EQ(c.quadrature_order, 5);
    EXPECT_EQ(c.hiss_half_width_hz, 2000.0);
    EXPECT_EQ(c.hiss_percentile, 25.0);
    EXPECT_EQ(c.hiss_average_s, 1.0);
    EXPECT_EQ(c.tracker_window_s, 1.0);
    EXPECT_EQ(c.tracker_hop_s, 0.5);
    EXPECT_EQ(c.tracker_search_hz, 15.0);
    EXPECT_EQ(c.tracker_gate_db, 9.0);
    EXPECT_EQ(c.tracker_drift_sd_hz_per_s, 0.3);
    EXPECT_EQ(c.tracker_drift_noise, 1.5e-4);
    EXPECT_FALSE(c.genie_frequency_offset_hz.has_value());
    EXPECT_EQ(c.genie_frequency_drift_hz_per_s, 0.0);
    EXPECT_FALSE(c.genie_fading_spread_hz.has_value());
    EXPECT_FALSE(c.stats);
    // What the table says of them: 32 speeds of 12.0 to 230.3 ms dots (100 to 5.2 words per minute, a dot being
    // 1.2 s / WPM), and an over starting in a piece of silence with probability 1.0e-4.
    EXPECT_NEAR(c.dot_min_ms * std::pow(c.speed_ratio, c.speed_count - 1), 230.3, 0.05);
    EXPECT_NEAR(1200.0 / c.dot_min_ms, 100.0, 1e-9);
    EXPECT_NEAR(1200.0 / (c.dot_min_ms * std::pow(c.speed_ratio, c.speed_count - 1)), 5.2, 0.05);
    EXPECT_NEAR(c.over_rate_per_s * c.piece_s, 1.0e-4, 1e-18);
    // The tracker's drift noise is a change of the drift rate by its start deviation over ten minutes:
    // (0.3 Hz/s)^2 / 600 s.
    EXPECT_NEAR(c.tracker_drift_noise, c.tracker_drift_sd_hz_per_s * c.tracker_drift_sd_hz_per_s / 600.0, 1e-18);
}
