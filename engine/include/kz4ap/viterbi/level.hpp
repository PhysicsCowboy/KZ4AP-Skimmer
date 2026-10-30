// The new decoder's signal level: what one story (a hypothesis) knows of the amplitude a, FS, of its key-down carrier
// (design notes, topic 3, item 1: per-survivor processing, Raheli, Polydoros and Tzou 1995; topic 5, items 2 and 3).
// The knowledge is a Gaussian N(mu, P) for a, mu in FS and P in FS^2, the time t_s, s, of its last update (the end of
// the story's last finished block), and a flag: a newborn story has not learned its level until its first block has
// finished. A story meets the operations below in this order. Its first block is scored by score_first_block, which
// also starts its level. Each later block is scored by score_block against the level predicted to the time in hand by
// predict, and the level the block gives, moment matched, replaces the story's when the block finishes. Merging two
// stories keeps the better one's level (the standard per-survivor approximation); the search does that, not this file.
#pragma once

#include "kz4ap/viterbi/observation.hpp"

#include <optional>

namespace kz4ap::viterbi {

// A story's level. A default Level is a newborn's: not learned, and mu, p and t_s are then not read.
struct Level {
    double mu = 0.0;        // FS: the mean of the key-down amplitude a
    double p = 0.0;         // FS^2: its variance
    double t_s = 0.0;       // s: the time of the last update, the end of the last finished block
    bool learned = false;   // false for a newborn until its first block has finished
};

// The level at time t_s, s, predicted from its last update at level.t_s, with the lag tau = t_s - level.t_s >= 0.
// The correlation of the complex gain of Rayleigh fading with a Gaussian Doppler spectrum of spread f_D = spread_hz
// (twice the spectrum's standard deviation) at the lag tau is
//     rho = exp(-pi^2 f_D^2 tau^2 / 2)
// (derived: the Fourier transform of the spectrum). It is taken as the amplitude's correlation (an approximation,
// ours), and the amplitude as a stationary Gaussian process of mean mu and standard deviation s mu about it, so that
// the Gaussian conditional gives
//     mu(t) = mu,    P(t) = rho^2 P + (s mu)^2 (1 - rho^2),    s = sqrt(4 / pi - 1) = 0.5227,
// s being the standard deviation over the mean of a Rayleigh amplitude (derived: scale sigma gives the mean
// sigma sqrt(pi / 2) and the variance (2 - pi / 2) sigma^2). The form is ours, with no measurement behind it, and
// f_D is the configuration's placeholder (fading_spread_hz, 1 Hz). P(t) lies between P and (s mu)^2, from the one at
// tau = 0 to the other as tau grows; with f_D = 0, rho = 1 and P is unchanged at any lag. From P = 0, after two dots
// (tau = 2 x 1.2 s / WPM) at f_D = 1 Hz, sqrt(P) / mu is 0.4951, 0.1903 and 0.0977 at 5, 20 and 40 WPM (derived; the
// design notes round them to +/-49%, +/-19% and +/-10%). A level known worse than the fading's own spread,
// sqrt(P) > s mu, is therefore predicted better known with time: the form carries that spread about mu, not mu's own
// error (a limitation of the form).
//
// With x = pi^2 f_D^2 tau^2, rho^2 = e^-x and 1 - rho^2 = -expm1(-x), so that both terms of P(t) are not negative and
// each is as accurate as the library's exp and expm1, at a short lag as at a long one: 1 - e^-x would carry the
// relative error 1e-16 / x and give 0 below x = 5.6e-17 (a lag of 2.4 ns at 1 Hz). The result keeps the t_s of the
// level it is predicted from: the prediction is not an update. It is to be used once and not predicted again, since a
// second prediction from it would age the level a second time.
// Throws std::invalid_argument for a level that is not learned, a spread_hz that is negative or not finite, and a
// t_s that is not finite or lies before the level's.
Level predict(const Level& level, double t_s, double spread_hz);

// What scoring a block gives: the block's evidence so far and the level it would leave.
struct BlockScore {
    double llr;     // nats: the block's evidence so far, Lambda_P (score_block) or Lambda_flat (score_first_block)
    Level level;    // the level if the block ended here: moment matched, t_s the block's end time, learned
    double mode_a;  // FS: the mode of the posterior of a, which the search hands back as the next piece's start_a
};

// The score of a block of a story whose level is learned: the sum's magnitude s_abs, FS, and noise variance v, FS^2
// (observation.hpp), the carrier's expected sum being m_signal x a (m_signal in channel samples: the block's m for a
// plain sum of full pieces; in a mark's first block, the matched filter to the rise, samples_per_piece x the sum of the
// squared weights; observation.hpp), against the level predicted to the block's end time, t_s, s. It is one call of
// block_evidence with predicted.mu and predicted.p as the Gaussian prior, and with the order and start_a given, and it
// costs no more: the evidence Lambda_P, the posterior's mean and variance as the new level, and its mode. The search
// scores every story in a mark at every piece, and the score and the level come from the one quadrature, never two.
// The level is the update of the story's level by the block: at high SNR the Kalman update, with the gain
// P / (P + v / (2 m_signal^2)) (observation.hpp has the accuracy). The design notes' dot (20 WPM, 10 dB S500, arriving
// 10 dB below a level known to +/-10%) moves it 98.40% of the way to the dot's own amplitude, where the linear gain
// gives 98.36%, and leaves P within 0.1% of the Kalman filter's; it scores 270.8 nats as a mark, within 0.01 nats of a
// dense integral (measured; level_test.cpp, OneDotMovesTheLevel, holds each of these figures). start_a (FS) is the
// previous piece's mode_a of the same block, where Newton's method starts; the result does not depend on it beyond
// Newton's tolerance. Throws std::invalid_argument for a predicted level that is not learned (a newborn's first block
// goes to score_first_block), a t_s that is not finite, and what block_evidence refuses.
BlockScore score_block(const Level& predicted, double s_abs, double m_signal, double v, int order, double t_s,
                       std::optional<double> start_a = std::nullopt);

// The score of a newborn story's first block of `pieces` pieces, with the magnitude s_abs (FS) of its sum, weighted by
// the rise (the matched filter: the block is a mark's first), and its noise variance v (FS^2), N0 x the long-run rate x
// m_signal: the flat prior's evidence Lambda_flat(pieces, z), z = s_abs / sqrt(v), and the level the block leaves from
// the flat prior's posterior of b = m_signal a / sqrt(v), m_signal = prior.signal_samples(pieces), mean_b and var_b,
// converted to FS:
//     mu = mean_b sqrt(v) / m_signal,    P = var_b v / m_signal^2,
// a Gaussian with the posterior's mean and variance (moment matching, as in every update); learned, with t_s the given
// end time, s. mode_a is mu, the posterior's mean, which stands in for the mode that the table does not hold. Two
// reads of the flat prior's table, and nothing more. Throws what FlatPrior::llr throws (std::out_of_range for pieces
// outside 1 ... prior.max_pieces(); std::invalid_argument for a z that is not finite), and std::invalid_argument for an
// s_abs that is negative or not finite, a v that is not positive and finite, and a t_s that is not finite.
BlockScore score_first_block(const FlatPrior& prior, double s_abs, int pieces, double v, double t_s);

}  // namespace kz4ap::viterbi
