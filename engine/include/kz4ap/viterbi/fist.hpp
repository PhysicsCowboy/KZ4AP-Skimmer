// The fist: how probable the lengths of a sender's dots, dashes and gaps are, at each speed (design notes, topic 2,
// items 1, 2 and 4; item 3's Farnsworth factor is removed from the first prototype, the owner's decision D14 of
// 2026-10-21: standard spacing only, docs/backlog.md, "Farnsworth spacing in the new decoder"). Lengths are counted in
// samples of sample_interval_s (2.667 ms by default). For one of the five kinds, at speed s, the length D is
//     D = shift + M e^(sigma N),    N a standard normal variable,
// a log-normal of median M and standard deviation sigma in ln(length), moved by a constant, where, with the speed's
// dot T_s = dot_min_ms x speed_ratio^s and the kind's entries of the configuration,
//     M = median_dots x T_s / (1000 sample_interval_s),    shift = shift_dots x T_s / (1000 sample_interval_s).
// The sender's length is rounded to the nearest sample and is never shorter than one sample: with F the distribution
// function of D, the probability of a length of tau samples is
//     P(1) = F(3/2),    P(tau) = F(tau + 1/2) - F(tau - 1/2)    for tau >= 2,
// which sum to 1, and the survival, the probability of lasting at least tau samples, is
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
// difference grows as the edges come closer in z (a long median). Measured with a 2 ms step (the first prototype's;
// the tests keep it): ln P is within 6.2e-7 nats at a median of 6 samples (the dot at 100 WPM), 1.1e-5 at 115 (the
// dot at 5.2 WPM) and 4.2e-5 at the longest, 806 samples (the word gap at 5.2 WPM), and within 1.5e-7 M for a median
// of M samples at the other ends of the grid of speeds, against std::erfc at every length up to 40 times the median;
// the tests check them. At the default 2.667 ms the medians are 0.75 times as long (4.50 to 604.5 samples), within
// the same range of medians.
#pragma once

#include "kz4ap/viterbi/config.hpp"

#include <vector>

namespace kz4ap::viterbi {

// The five kinds of mark and gap, in the order of ViterbiConfig's fist_* arrays: the dot and the dash (the key down),
// the gap between the marks of a letter, the gap between letters and the gap between words (the key up).
enum class Kind { Dot, Dash, ElementGap, LetterGap, WordGap };

class Fist {
public:
    // Throws std::invalid_argument for a configuration it cannot read: a sample_interval_s, dot_min_ms, speed_ratio, median or
    // sigma that is not positive and finite, a shift that is not finite, or fewer than one speed. It keeps nothing of
    // the configuration itself.
    explicit Fist(const ViterbiConfig& config);

    // ln S(tau), nats: the probability that a mark or gap of this kind, at this speed (0 ... speed_count - 1), lasts at
    // least tau samples (tau >= 1). Throws std::out_of_range for a kind, speed or tau outside these.
    double log_survival(Kind kind, int speed, int tau) const;
    // ln P(tau), nats: the probability that it lasts exactly tau samples; -infinity where that is 0 (a shift longer
    // than tau samples). The same arguments and errors as log_survival.
    double log_end(Kind kind, int speed, int tau) const;

private:
    // One kind's length at one speed: ln of its median in samples, its shift in samples and its sigma in ln(length).
    struct Params {
        double ln_median;
        double shift;
        double sigma;
        // z = (ln(edge - shift) - ln M) / sigma for a length edge in samples, where the edge lies in the standard
        // normal that the log-normal part of the length maps to; -infinity at or below the shift, where no length lies.
        double z(double edge) const;
    };
    // The parameters for a kind and speed, after checking them and tau.
    Params params(Kind kind, int speed, int tau) const;

    std::vector<Params> by_kind_and_speed_;  // kind-major: kind x speed_count_ + speed
    int speed_count_;
};

}  // namespace kz4ap::viterbi
