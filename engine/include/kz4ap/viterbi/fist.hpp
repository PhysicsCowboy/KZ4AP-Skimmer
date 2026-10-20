// The fist: how probable the lengths of a sender's dots, dashes and gaps are, at each speed and Farnsworth factor
// (design notes, topic 2, items 1 to 4). Lengths are counted in pieces of piece_s. For one of the five kinds, at
// speed s and factor f, the length D is
//     D = shift + M e^(sigma N),    N a standard normal variable,
// a log-normal of median M and standard deviation sigma in ln(length), moved by a constant, where, with the speed's
// dot T_s = dot_min_ms x speed_ratio^s, the factor F_f = factor_ratio^f and the kind's entries of the configuration,
//     M = median_dots x T_s / (1000 piece_s)   (and x F_f for LetterGap and WordGap),
//     shift = shift_dots x T_s / (1000 piece_s)   (the factor does not stretch it).
// The sender's length is rounded to the nearest piece and is never shorter than one piece: with F the distribution
// function of D, the probability of a length of tau pieces is
//     P(1) = F(3/2),    P(tau) = F(tau + 1/2) - F(tau - 1/2)    for tau >= 2,
// which sum to 1, and the survival, the probability of lasting at least tau pieces, is
//     S(1) = 1,    S(tau) = 1 - F(tau - 1/2)    for tau >= 2,
// so that P(tau) = S(tau) - S(tau + 1). Both are returned as natural logarithms, in nats.
//
// Everything is computed in the log, so that nothing underflows: ln S of a word gap far past its median is finite
// where S itself is not a double. With z = (ln(length - shift) - ln M) / sigma, the number of standard deviations, in
// ln(length), that a length lies above the median, S is 0 as a double from z = 38.5, while ln S is -804.6 nats at
// z = 40. The log of the standard normal's upper tail, ln Phi_c(z), is read from a table on z in [-8, 40] with a step
// of 0.001 and linear interpolation, and computed directly outside it (std::erfc below z = 30, the continued
// fraction of Mills' ratio above). The interpolation is within 1.25e-7 nats of exact (derived: step^2 / 8 times the
// largest second derivative of ln Phi_c, whose size is below 1; measured 1.249e-7), so ln S is. ln P is the log of the
// mass between two edges, taken from the tail its edges lie in (the lower tail below the median, the upper above it,
// and 1 minus both across it) so that a tiny mass keeps its relative accuracy. The edges' tail values are the ones the
// neighboring lengths use, so the P(tau) sum to 1 to rounding, whatever the interpolation's error. The error of a
// difference grows as the edges come closer than a table step (a long median): ln P is within 6e-7 nats at a median
// of 6 pieces, 1.1e-5 at 115 and 2.4e-4 at the longest, 6 562 pieces (measured against std::erfc, at every length up
// to 40 times the median).
#pragma once

#include "kz4ap/viterbi/config.hpp"

#include <vector>

namespace kz4ap::viterbi {

// The five kinds of mark and gap, in the order of ViterbiConfig's fist_* arrays: the dot and the dash (the key down),
// the gap between the marks of a letter, the gap between letters and the gap between words (the key up).
enum class Kind { Dot, Dash, ElementGap, LetterGap, WordGap };

class Fist {
public:
    // Throws std::invalid_argument for a configuration it cannot read: a piece_s, dot_min_ms, speed_ratio,
    // factor_ratio, median or sigma that is not positive and finite, a shift that is not finite, or fewer than one
    // speed or factor. It keeps nothing of the configuration itself.
    explicit Fist(const ViterbiConfig& config);

    // ln S(tau), nats: the probability that a mark or gap of this kind, at this speed (0 ... speed_count - 1) and
    // factor (0 ... factor_count - 1), lasts at least tau pieces (tau >= 1). The factor matters only to LetterGap and
    // WordGap and is not read for the other kinds. Throws std::out_of_range for a kind, speed, tau or (where it
    // matters) factor outside these.
    double log_survival(Kind kind, int speed, int factor, int tau) const;
    // ln P(tau), nats: the probability that it lasts exactly tau pieces; -infinity where that is 0 (a shift longer
    // than tau pieces). The same arguments and errors as log_survival.
    double log_end(Kind kind, int speed, int factor, int tau) const;

private:
    // One kind's length at one speed: ln of its median in pieces (before the Farnsworth factor), its shift in pieces
    // and its sigma in ln(length).
    struct Params {
        double ln_median;
        double shift;
        double sigma;
        // z = (ln(edge - shift) - ln M) / sigma for a length edge in pieces, where the edge lies in the standard
        // normal that the log-normal part of the length maps to; -infinity at or below the shift, where no length lies.
        double z(double edge) const;
    };
    // The parameters for a kind, speed and factor (the factor's stretch included in ln_median), after checking them.
    Params params(Kind kind, int speed, int factor, int tau) const;

    std::vector<Params> by_kind_and_speed_;  // kind-major: kind x speed_count_ + speed
    int speed_count_;
    int factor_count_;
    double ln_factor_ratio_;  // ln(factor_ratio)
};

}  // namespace kz4ap::viterbi
