#include "kz4ap/viterbi/level.hpp"

#include <cmath>
#include <numbers>
#include <optional>
#include <stdexcept>

namespace kz4ap::viterbi {
namespace {

// s, dimensionless: the standard deviation over the mean of a Rayleigh amplitude (derived): an amplitude of scale
// sigma has the mean sigma sqrt(pi / 2) and the variance (2 - pi / 2) sigma^2, so its variance over its squared mean
// is (2 - pi / 2) / (pi / 2) = 4 / pi - 1, and s = 0.5227.
const double kRayleighSpread = std::sqrt(4.0 / std::numbers::pi - 1.0);

}  // namespace

Level predict(const Level& level, double t_s, double spread_hz) {
    if (!level.learned)
        throw std::invalid_argument("predict: the level has not been learned (a newborn has nothing to predict from)");
    if (!std::isfinite(spread_hz) || spread_hz < 0.0)
        throw std::invalid_argument("predict: spread_hz must be finite and not negative (Hz)");
    const double lag_s = t_s - level.t_s;
    if (!std::isfinite(lag_s) || lag_s < 0.0)
        throw std::invalid_argument("predict: t_s must be finite and not before the level's last update (s)");
    // rho = exp(-pi^2 f_D^2 tau^2 / 2), so rho^2 = e^-x and 1 - rho^2 = -expm1(-x) with x = pi^2 f_D^2 tau^2: both
    // terms of P(t) are not negative and as accurate as exp and expm1, and x = 0 (f_D = 0 or tau = 0) gives P back to
    // the bit.
    // The product of the spread and the lag first, so that a zero lag gives 0 at any finite spread, not infinity x 0.
    const double pi_f_tau = std::numbers::pi * (spread_hz * lag_s);
    const double x = pi_f_tau * pi_f_tau;
    const double kept = std::exp(-x);
    const double fresh = -std::expm1(-x);
    const double stationary = kRayleighSpread * level.mu;  // FS: s mu, the fading's own spread about the level
    return {level.mu, kept * level.p + stationary * stationary * fresh, level.t_s, true};
}

BlockScore score_block(const Level& predicted, double s_abs, int m, double v, int order, double t_s,
                       std::optional<double> start_a) {
    if (!predicted.learned)
        throw std::invalid_argument(
            "score_block: the predicted level has not been learned (a newborn's first block is score_first_block's)");
    if (!std::isfinite(t_s)) throw std::invalid_argument("score_block: t_s must be finite (s)");
    const BlockEvidence evidence = block_evidence(s_abs, m, predicted.mu, predicted.p, v, order, start_a);
    return {evidence.llr, {evidence.level.mean, evidence.level.var, t_s, true}, evidence.mode_a};
}

BlockScore score_first_block(const FlatPrior& prior, double s_abs, int pieces, double v, double t_s) {
    if (!(s_abs >= 0.0) || !std::isfinite(s_abs))
        throw std::invalid_argument("score_first_block: s_abs must be finite and not negative (FS)");
    if (!(v > 0.0) || !std::isfinite(v))
        throw std::invalid_argument("score_first_block: v must be positive and finite (FS^2)");
    if (!std::isfinite(t_s)) throw std::invalid_argument("score_first_block: t_s must be finite (s)");
    const double root_v = std::sqrt(v);
    // The flat prior refuses pieces outside 1 ... max_pieces, and a z that is not finite (s_abs / root_v overflowing).
    const double z = s_abs / root_v;
    const double llr = prior.llr(pieces, z);
    const Moments b = prior.posterior(pieces, z);
    const double m = static_cast<double>(prior.samples_per_piece()) * pieces;  // channel samples
    const Level level{b.mean * (root_v / m), b.var * (v / (m * m)), t_s, true};
    return {llr, level, level.mu};
}

}  // namespace kz4ap::viterbi
