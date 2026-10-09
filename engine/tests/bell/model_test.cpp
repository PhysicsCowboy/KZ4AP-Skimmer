// Bell's configuration and model routines: the tables against the listing's DATA statements (typed here in the
// listing's column-major order, independently of config.hpp's rows), XTRANS against the closed form of p. 100,
// SPDTR against Table X, MODEL's Φ and Q against the reference sheet's values, and PTRANS on the letter tree.
#include "kz4ap/bell/config.hpp"
#include "kz4ap/bell/model.hpp"
#include "kz4ap/bell/tree.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace {

using kz4ap::bell::at1;
using kz4ap::bell::BellConfig;
using kz4ap::bell::ms;

// INITL's DATA, column-major as listed [p. 161].
const double kElemtrData[96] = {
    .55, .5, .5, .5, .55, .5, .5, .5, 0, 0, 0, 0, 0, 0, 0, 0,                          // 08700
    .45, .5, .5, .5, .45, .5, .5, .5, 0, 0, 0, 0, 0, 0, 0, 0,                          // 08800
    0, 0, 0, 0, 0, 0, 0, 0, .581, .54, .923, .923, .923, .923, .95, .95,               // 08900
    0, 0, 0, 0, 0, 0, 0, 0, .335, .376, .062, .062, .062, .062, .04, .04,              // 09000
    0, 0, 0, 0, 0, 0, 0, 0, .067, .069, .012, .012, .012, .012, .009, .009,            // 09100
    0, 0, 0, 0, 0, 0, 0, 0, .015, .015, .003, .003, .003, .003, .001, .001,            // 09200
};
const double kRtransData[10] = {.1, .2, .4, .2, .1, .15, .2, .3, .2, .15};  // 09500
const int kMemdelData[36] = {0, 0, 2, 2, 5, 10, 0, 0, 2, 2, 5, 10, 2, 2, 0, 0, 0, 0,
                             2, 2, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0};  // 09600-09800
const int kMemprData[36] = {0, 0, 1, 2, 1, 2, 0, 0, 1, 2, 1, 2, 1, 1, 0, 0, 0, 0,
                            1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0};   // 09900-10000

// Table X (reference sheet (b)): the speed step in WPM by the element just completed, and the RTRANS column, for a
// change of key state. Typed from the sheet.
int table_x_step(int from) { return from == 5 ? 5 : from == 6 ? 10 : 2; }
int table_x_column(int from, int to) { return to <= 2 && (from == 4 || from == 6) ? 2 : 1; }
const std::array<std::array<double, 5>, 2> kTableX = {{{.1, .2, .4, .2, .1}, {.15, .2, .3, .2, .15}}};
bool is_mark(int element) { return element <= 2; }

// The element length's survival function S(b) = Pr[L >= b·mΔ] of reference sheet (b): Laplacian density with mode
// m·Δ and rate α_m per m·Δ.
double survival(double b, double alpha_m) {
    return b <= 1.0 ? 1.0 - 0.5 * std::exp(alpha_m * (b - 1.0)) : 0.5 * std::exp(-alpha_m * (b - 1.0));
}
// α_m = m · (3.0, 3.0, 3.0, 3.0, 1.5, 1.0 per baud) for dot ... pause (the listing's APARM with KIMAP; sheet (b)).
const std::array<double, 6> kAlphaM = {3.0, 9.0, 3.0, 9.0, 10.5, 14.0};
const std::array<int, 6> kNominalDots = {1, 3, 1, 3, 7, 14};

double rel(double a, double b) { return std::abs(a - b) / std::abs(b); }

}  // namespace

TEST(BellConfig, TablesEqualTheListingsData) {
    const BellConfig c;
    for (int k = 1; k <= 6; ++k)
        for (int m = 1; m <= 16; ++m)
            EXPECT_EQ(at1(at1(c.elemtr, m), k), kElemtrData[(k - 1) * 16 + (m - 1)]) << "ELEMTR(" << m << "," << k << ")";
    for (int col = 1; col <= 2; ++col)
        for (int i = 1; i <= 5; ++i) EXPECT_EQ(at1(at1(c.rtrans, i), col), kRtransData[(col - 1) * 5 + (i - 1)]);
    for (int to = 1; to <= 6; ++to)
        for (int from = 1; from <= 6; ++from) {
            EXPECT_EQ(at1(at1(c.memdel, from), to), kMemdelData[(to - 1) * 6 + (from - 1)]) << from << " " << to;
            EXPECT_EQ(at1(at1(c.mempr, from), to), kMemprData[(to - 1) * 6 + (from - 1)]) << from << " " << to;
        }
    EXPECT_EQ(c.isx, (std::array<int, 6>{1, 1, 0, 0, 0, 0}));                                       // 06300
    EXPECT_EQ(c.ilamx, (std::array<int, 6>{1, 1, 0, 0, 0, 0}));                                     // 07500
    EXPECT_EQ(c.ilam1, (std::array<int, 16>{3, 4, 5, 6, 3, 4, 5, 6, 1, 2, 1, 2, 1, 2, 1, 2}));      // 07400
    EXPECT_EQ(c.xtrans_kimap, (std::array<int, 6>{1, 3, 1, 3, 7, 14}));                             // 20500
    EXPECT_EQ(c.xtrans_aparm, (std::array<double, 3>{3.000, 1.500, 1.000}));                        // 20600
    // ELEMTR's rows: 1 for every element-state but 9 (0.998, as listed)
    for (int m = 1; m <= 16; ++m) {
        double sum = 0.0;
        for (int k = 1; k <= 6; ++k) sum += at1(at1(c.elemtr, m), k);
        EXPECT_NEAR(sum, m == 9 ? 0.998 : 1.0, 1e-12) << m;
    }
    // Table X is MEMDEL and MEMPR on the key-state changes, and 0 elsewhere
    for (int from = 1; from <= 6; ++from)
        for (int to = 1; to <= 6; ++to) {
            const bool change = is_mark(from) != is_mark(to);
            EXPECT_EQ(at1(at1(c.memdel, from), to), change ? table_x_step(from) : 0) << from << " " << to;
            EXPECT_EQ(at1(at1(c.mempr, from), to), change ? table_x_column(from, to) : 0) << from << " " << to;
        }
}

// The times held in s become the listing's literals exactly in ms, and the per-step counts are whole.
TEST(BellConfig, TimesConvertToTheListingsValuesExactly) {
    const BellConfig c;
    EXPECT_EQ(c.sample_period_s, 0.000125);                // TAU [p. 160 00600]
    EXPECT_EQ(ms(c.step_s), 5.0);                          // 5. [p. 170 21400]
    EXPECT_EQ(ms(c.dot_length_wpm_s), 1200.0);             // 1200. [p. 170 21200, p. 178 12400]
    EXPECT_EQ(ms(c.start_duration_s), 1000.0);             // DUR/25*1000./ [p. 167 06000]
    EXPECT_EQ(std::lround(c.step_s / c.sample_period_s), 40);  // NP.LT.40 [p. 159 01800]
    EXPECT_NEAR(c.step_s / c.sample_period_s, 40.0, 1e-12);
    EXPECT_EQ(std::lround(c.decision_delay_s / c.step_s), 199);
    EXPECT_NEAR(c.decision_delay_s / c.step_s, 199.0, 1e-9);
    EXPECT_EQ(std::lround(c.noise_long_window_s / c.step_s), 200);  // YLONG(200) [p. 190 10900]
    EXPECT_EQ(std::lround(c.noise_short_window_s / c.step_s), 50);  // YSHORT(50)
    EXPECT_EQ(c.start_speeds_wpm.size(), static_cast<std::size_t>(c.max_paths));
}

// XTRANS against the closed form of p. 100 with the listing's α: the stay probability is S(b1)/S(b0), the ratio of
// survival functions, at D0 below, at and above the mode m·Δ, for every element and speeds across the range.
TEST(BellXtrans, IsTheSurvivalRatioOfTheLaplacianWithTheListingsAlpha) {
    const BellConfig c;
    int below = 0, straddle = 0, above = 0;
    for (int e = 1; e <= 6; ++e) {
        for (int wpm : {10, 20, 33, 60}) {
            const double delta_ms = 1200.0 / wpm;
            const double mode_ms = at1(kNominalDots, e) * delta_ms;
            for (double d0 : {0.0, 0.5 * mode_ms, mode_ms - 7.5, mode_ms - 2.5, mode_ms, mode_ms + 2.5, 1.5 * mode_ms,
                              3.0 * mode_ms}) {
                if (d0 < 0.0) continue;
                const double b0 = d0 / mode_ms, b1 = (d0 + 5.0) / mode_ms;
                const double expected = survival(b1, at1(kAlphaM, e)) / survival(b0, at1(kAlphaM, e));
                const double p = kz4ap::bell::xtrans(e, d0, wpm, c);
                EXPECT_LT(rel(p, expected), 1e-12) << "element " << e << ", " << wpm << " WPM, D0 " << d0 << " ms";
                EXPECT_GT(p, 0.0);
                EXPECT_LT(p, 1.0);
                if (b1 <= 1.0) ++below;
                else if (b0 < 1.0) ++straddle;
                else ++above;
            }
        }
    }
    EXPECT_GT(below, 0);
    EXPECT_GT(straddle, 0);
    EXPECT_GT(above, 0);
}

// The stay probabilities telescope: their product over the steps from 0 to D is S(D/mΔ)/S(0). A dot at 20 WPM
// (Δ = 60 ms) outlasts 2 dots with probability ½e^{-3} = 0.0249 (sheet (b)), so from its start 0.0249/(1 - 0.0249).
TEST(BellXtrans, StayProbabilitiesTelescope) {
    const BellConfig c;
    double product = 1.0;
    for (int step = 0; step < 24; ++step) product *= kz4ap::bell::xtrans(1, 5.0 * step, 20, c);  // to 120 ms
    const double s2 = 0.5 * std::exp(-3.0);
    EXPECT_NEAR(s2, 0.0249, 5e-5);
    EXPECT_LT(rel(product, s2 / (1.0 - s2)), 1e-12);
}

// SPDTR against Table X: for every change of key state the five branches carry Table X's probabilities and sum to
// 1 where no branch leaves 10-60 WPM; branches outside the range are 0, not redistributed; no change of element
// keeps the speed; a change that keeps the key state has probability 0.
TEST(BellSpdtr, IsTableX) {
    const BellConfig c;
    for (int from = 1; from <= 6; ++from) {
        for (int to = 1; to <= 6; ++to) {
            double sum = 0.0;
            for (int i = 1; i <= 5; ++i) {
                const double p = kz4ap::bell::spdtr(i, 30, to, from, c);
                sum += p;
                if (from == to) {
                    EXPECT_EQ(p, i == 3 ? 1.0 : 0.0);
                } else if (is_mark(from) == is_mark(to)) {
                    EXPECT_EQ(p, 0.0) << from << " " << to;
                } else {
                    EXPECT_EQ(p, at1(at1(kTableX, table_x_column(from, to)), i)) << from << " " << to << " " << i;
                }
            }
            if (from == to || is_mark(from) != is_mark(to)) EXPECT_NEAR(sum, 1.0, 1e-15) << from << " " << to;
        }
    }
    // the range: after a pause (steps of 10 WPM) at 15 WPM, branches 1 and 2 (-5 and 5 WPM) are 0; after a c-sp
    // (2 WPM) at 58 WPM, branch 5 (62 WPM) is 0 and branch 4 (60 WPM) is kept; 10 WPM is kept
    EXPECT_EQ(kz4ap::bell::spdtr(1, 15, 1, 6, c), 0.0);
    EXPECT_EQ(kz4ap::bell::spdtr(2, 15, 1, 6, c), 0.0);
    EXPECT_EQ(kz4ap::bell::spdtr(3, 15, 1, 6, c), 0.3);
    EXPECT_EQ(kz4ap::bell::spdtr(4, 15, 2, 6, c), 0.2);
    EXPECT_EQ(kz4ap::bell::spdtr(5, 58, 1, 4, c), 0.0);
    EXPECT_EQ(kz4ap::bell::spdtr(4, 58, 1, 4, c), 0.2);
    EXPECT_EQ(kz4ap::bell::spdtr(1, 14, 3, 1, c), 0.1);  // 10 WPM
    EXPECT_EQ(kz4ap::bell::spdtr(1, 13, 3, 1, c), 0.0);  // 9 WPM
}

// MODEL's Φ and Q from a space to a mark at B = 1, 3, 7 and 14 bauds (and beyond, capped), against the formula and
// the reference sheet's values (0.01006, 0.02031, 0.02333, 0.16040 AU² per 5 ms step).
TEST(BellModel, SpaceToMarkQ) {
    const BellConfig c;
    const std::array<std::pair<double, double>, 5> cases = {
        {{1.0, 0.01006}, {3.0, 0.02031}, {7.0, 0.02333}, {14.0, 0.16040}, {20.0, 0.16040}}};
    for (int wpm : {10, 20, 60}) {
        const double delta_ms = 1200.0 / wpm;
        for (const auto& [b, sheet] : cases) {
            for (int ielm = 3; ielm <= 6; ++ielm) {
                double phi = -1.0, qa = -1.0, hz = -1.0;
                kz4ap::bell::model(b * delta_ms, ielm, wpm, 3, 1, phi, qa, hz, c);
                const double bc = std::min(b, 14.0);
                const double formula = 0.15 * std::exp(0.6 * (bc - 14.0)) + 0.01 * bc * std::exp(0.2 * (1.0 - bc));
                EXPECT_EQ(phi, 1.0);
                EXPECT_EQ(hz, 1.0);
                EXPECT_LT(rel(qa, formula), 1e-13) << b << " bauds, " << wpm << " WPM";
                EXPECT_NEAR(qa, sheet, 5e-6) << b << " bauds";
            }
        }
    }
}

// While the path is in a mark: Φ = 1 and Q = 10⁻⁴ AU² per 5 ms step for every successor; h is the successor's key
// state.
TEST(BellModel, MarkPath) {
    const BellConfig c;
    for (int ielm = 1; ielm <= 2; ++ielm)
        for (int ixs = 0; ixs <= 1; ++ixs)
            for (double dur : {5.0, 60.0, 900.0}) {
                double phi = -1.0, qa = -1.0, hz = -1.0;
                kz4ap::bell::model(dur, ielm, 20, 1, ixs, phi, qa, hz, c);
                EXPECT_EQ(phi, 1.0);
                EXPECT_EQ(qa, 0.0001);
                EXPECT_EQ(hz, static_cast<double>(ixs));
            }
}

// The key-up Φ (a space staying a space) at 10, 20 and 60 WPM: 0.99829, 0.99658, 0.98977 per 5 ms step (sheet (b)),
// 10^(-1/56) per baud of amplitude, independent of speed; 1 from 14 bauds on; Q = 0; h = 0.
TEST(BellModel, KeyUpPhi) {
    const BellConfig c;
    const std::array<std::pair<int, double>, 3> cases = {{{10, 0.99829}, {20, 0.99658}, {60, 0.98977}}};
    for (const auto& [wpm, sheet] : cases) {
        const double delta_ms = 1200.0 / wpm;
        for (int ielm = 3; ielm <= 6; ++ielm) {
            double phi = -1.0, qa = -1.0, hz = -1.0;
            kz4ap::bell::model(2.0 * delta_ms, ielm, wpm, 3, 0, phi, qa, hz, c);
            EXPECT_NEAR(phi, sheet, 5e-6) << wpm;
            EXPECT_LT(rel(std::pow(phi, delta_ms / 5.0), std::pow(10.0, -1.0 / 56.0)), 1e-12) << wpm;
            EXPECT_EQ(qa, 0.0);
            EXPECT_EQ(hz, 0.0);
            kz4ap::bell::model(14.0 * delta_ms, ielm, wpm, 3, 0, phi, qa, hz, c);
            EXPECT_EQ(phi, 1.0) << "at 14 bauds";
            kz4ap::bell::model(13.9 * delta_ms, ielm, wpm, 3, 0, phi, qa, hz, c);
            EXPECT_LT(phi, 1.0) << "below 14 bauds";
        }
    }
}

// PTRANS on the independent-letter model, from every state of the letter tree, at speeds where branches are clipped
// and where none is: as TRPROB calls it (k = 1 ... 6, i_r = 1 ... 5, n = (i_r - 1)·6 + k), the successors that get
// probability are exactly the stay (branch 3) and the elements ELEMTR allows, with
//   stay: p_stay;   k != e: (1 - p_stay)·ELEMTR(IELMST(λ), k)·RTRANS(i_r, column)·[new speed in 10-60 WPM],
// whether the tree allows k or not (PTRANS does not consult MEMFCN).
TEST(BellPtrans, AllowedElementsAndTheirProbabilitiesFromEveryState) {
    const BellConfig c;
    const kz4ap::bell::MorseTree tree = kz4ap::bell::bell_tree();
    int tree_forbidden_with_probability = 0;
    for (int lambda = 1; lambda <= tree.states(); ++lambda) {
        const int m = at1(tree.ielmst, lambda);
        const int e = at1(c.ilam1, m);
        for (int wpm : {10, 30, 60}) {
            for (double dur : {5.0, 40.0, 300.0}) {
                const double p_stay = kz4ap::bell::xtrans(e, dur, wpm, c);
                std::array<double, kz4ap::bell::kSuccessors> pin{};
                double psum = 0.0;
                for (int k = 1; k <= 6; ++k)
                    for (int i = 1; i <= 5; ++i)
                        kz4ap::bell::ptrans(k, i, lambda, wpm, p_stay, psum, pin, (i - 1) * 6 + k, tree, c);
                double sum = 0.0, expected_sum = 0.0;
                for (int k = 1; k <= 6; ++k) {
                    double element_total = 0.0;
                    for (int i = 1; i <= 5; ++i) {
                        const double p = at1(pin, (i - 1) * 6 + k);
                        double expected = 0.0;
                        if (k == e) {
                            expected = i == 3 ? p_stay : 0.0;
                        } else {
                            const double elemtr = kElemtrData[(k - 1) * 16 + (m - 1)];
                            if (elemtr > 0.0) {
                                const int new_wpm = wpm + (i - 3) * table_x_step(e);
                                const bool in_range = new_wpm >= 10 && new_wpm <= 60;
                                expected = in_range ? (1.0 - p_stay) * elemtr *
                                                          at1(at1(kTableX, table_x_column(e, k)), i)
                                                    : 0.0;
                            }
                        }
                        EXPECT_LT(std::abs(p - expected), 1e-15) << lambda << " " << k << " " << i;
                        element_total += p;
                        sum += p;
                        expected_sum += expected;
                    }
                    const bool allowed = k == e || kElemtrData[(k - 1) * 16 + (m - 1)] > 0.0;
                    EXPECT_EQ(element_total > 0.0, allowed) << "state " << lambda << " element " << k;
                    if (k != e && element_total > 0.0 && at1(at1(tree.memfcn, lambda), k) == 0)
                        ++tree_forbidden_with_probability;
                }
                EXPECT_EQ(psum, sum);
                EXPECT_NEAR(psum, expected_sum, 1e-15);
                if (wpm == 30) {  // no branch clipped: Σ = p_stay + (1 - p_stay)·(ELEMTR's row sum)
                    double row = 0.0;
                    for (int k = 1; k <= 6; ++k) row += kElemtrData[(k - 1) * 16 + (m - 1)];
                    EXPECT_NEAR(psum, p_stay + (1.0 - p_stay) * row, 1e-15) << lambda;
                }
            }
        }
    }
    EXPECT_GT(tree_forbidden_with_probability, 0);
}

// PTRANS adds to the running sum it is given and writes only PIN(n).
TEST(BellPtrans, AccumulatesAndWritesOnlyItsEntry) {
    const BellConfig c;
    const kz4ap::bell::MorseTree tree = kz4ap::bell::bell_tree();
    std::array<double, kz4ap::bell::kSuccessors> pin;
    pin.fill(-1.0);
    double psum = 0.25;
    // state 5, E(-): an e-sp; a dot next at branch 3 of 20 WPM
    kz4ap::bell::ptrans(1, 3, 5, 20, 0.9, psum, pin, 13, tree, c);
    const double expected = (1.0 - 0.9) * 0.55 * 0.4;
    EXPECT_EQ(at1(pin, 13), expected);
    EXPECT_EQ(psum, 0.25 + expected);
    for (int n = 1; n <= 30; ++n)
        if (n != 13) EXPECT_EQ(at1(pin, n), -1.0);
}
