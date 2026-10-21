#include "kz4ap/viterbi/fist.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <tuple>

namespace kz4ap::viterbi {
namespace {

constexpr std::size_t kKinds = 5;  // Dot, Dash, ElementGap, LetterGap, WordGap
static_assert(std::tuple_size_v<decltype(ViterbiConfig::fist_medians_dots)> == kKinds,
              "ViterbiConfig's fist_* arrays hold one entry per Kind");

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kInvSqrt2 = 1.0 / std::numbers::sqrt2;
constexpr double kHalfLn2Pi = 0.91893853320467274178;  // ln(sqrt(2 pi))

// The table of ln Phi_c(z), the standard normal's upper tail 1 - Phi(z) in the log, at the nodes z = -8, -7.999, ...,
// 40: 48 000 steps of 0.001 and 48 001 nodes. Below the first node Phi_c is 1 to within 6.2e-16 and ln Phi_c is
// -Phi(z); past the last, Phi_c is under e^-804.
constexpr double kTableFirstZ = -8.0;
constexpr double kTableLastZ = 40.0;
constexpr double kNodesPerZ = 1000.0;  // 1 / the step of 0.001
constexpr int kTableSteps = 48000;

// ln Phi_c(z) evaluated directly, for any z. Below 0, where Phi_c is near 1, it is log1p of -Phi(z), with Phi from
// std::erfc. From 0 to 30 it is the log of std::erfc. Past 30 it is the continued fraction of Mills' ratio,
// Phi_c(z) = phi(z) / (z + 1/(z + 2/(z + 3/(z + ...)))), phi the standard normal density, with 12 terms (exact to the
// double's last digit from z = 30, checked against a 60-digit evaluation). std::erfc cannot be used there: from
// z = 38.5 Phi_c is below the smallest double and std::erfc returns 0, while its log is a finite number for every z.
double ln_phic_direct(double z) {
    constexpr double kErfcLimit = 30.0;
    constexpr int kContinuedFractionTerms = 12;
    if (z < 0.0) return std::log1p(-0.5 * std::erfc(-z * kInvSqrt2));
    if (z <= kErfcLimit) return std::log(0.5 * std::erfc(z * kInvSqrt2));
    double denominator = z;
    for (int k = kContinuedFractionTerms; k >= 1; --k) denominator = z + k / denominator;
    return -0.5 * z * z - kHalfLn2Pi - std::log(denominator);
}

const std::vector<double>& ln_phic_table() {
    static const std::vector<double> table = [] {
        std::vector<double> nodes(static_cast<std::size_t>(kTableSteps) + 1);
        for (int i = 0; i <= kTableSteps; ++i)
            nodes[static_cast<std::size_t>(i)] = ln_phic_direct(kTableFirstZ + i / kNodesPerZ);
        return nodes;
    }();
    return table;
}

// ln Phi_c(z), nats: the table with linear interpolation on [-8, 40), direct evaluation elsewhere (including
// infinite z: 0 at -infinity, -infinity at +infinity).
double ln_phic(double z) {
    if (!(z >= kTableFirstZ && z < kTableLastZ)) return ln_phic_direct(z);
    const std::vector<double>& table = ln_phic_table();
    const double position = (z - kTableFirstZ) * kNodesPerZ;  // in steps from the first node
    std::size_t cell = static_cast<std::size_t>(position);
    if (cell >= static_cast<std::size_t>(kTableSteps)) cell = static_cast<std::size_t>(kTableSteps) - 1;
    const double fraction = position - static_cast<double>(cell);
    return table[cell] + fraction * (table[cell + 1] - table[cell]);
}

// ln(1 - e^d) for d <= 0 (-infinity at d = 0): log of -expm1 near 0, where e^d is close to 1, log1p of -exp elsewhere.
double log1m_exp(double d) {
    return d > -std::numbers::ln2 ? std::log(-std::expm1(d)) : std::log1p(-std::exp(d));
}

// ln(e^hi - e^lo) from the logs; -infinity if the difference is not positive.
double log_diff_exp(double hi, double lo) {
    if (!(lo < hi)) return -kInf;
    return hi + log1m_exp(lo - hi);
}

// ln of the standard normal's mass in (lower, upper], lower <= upper (lower may be -infinity), each mass taken from
// the tail it lies in so that a tiny one keeps its relative accuracy: Phi(upper) - Phi(lower) from the lower tail
// when the interval lies below 0, Phi_c(lower) - Phi_c(upper) from the upper tail when it lies above 0, and 1 minus
// the two tails when it contains 0. The tails use the same ln_phic as ln S does, at the same edges, so the masses of
// neighboring intervals telescope: their sum is 1 whatever the table's interpolation error.
double ln_mass(double lower, double upper) {
    if (upper <= 0.0) return log_diff_exp(ln_phic(-upper), ln_phic(-lower));
    if (lower >= 0.0) return log_diff_exp(ln_phic(lower), ln_phic(upper));
    return std::log1p(-(std::exp(ln_phic(-lower)) + std::exp(ln_phic(upper))));
}

bool positive_and_finite(double x) { return x > 0.0 && std::isfinite(x); }

}  // namespace

Fist::Fist(const ViterbiConfig& config) : speed_count_(config.speed_count) {
    if (!positive_and_finite(config.piece_s)) throw std::invalid_argument("fist: piece_s must be positive (s)");
    if (!positive_and_finite(config.dot_min_ms)) throw std::invalid_argument("fist: dot_min_ms must be positive (ms)");
    if (config.speed_count < 1) throw std::invalid_argument("fist: speed_count must be at least 1");
    if (!positive_and_finite(config.speed_ratio)) throw std::invalid_argument("fist: speed_ratio must be positive");
    for (std::size_t kind = 0; kind < kKinds; ++kind) {
        if (!positive_and_finite(config.fist_medians_dots[kind]))
            throw std::invalid_argument("fist: every median must be positive (dots)");
        if (!positive_and_finite(config.fist_sigmas[kind]))
            throw std::invalid_argument("fist: every sigma must be positive (ln length)");
        if (!std::isfinite(config.fist_shifts_dots[kind]))
            throw std::invalid_argument("fist: every shift must be finite (dots)");
    }
    const double piece_ms = config.piece_s * 1000.0;
    by_kind_and_speed_.reserve(kKinds * static_cast<std::size_t>(speed_count_));
    for (std::size_t kind = 0; kind < kKinds; ++kind) {
        for (int speed = 0; speed < speed_count_; ++speed) {
            const double dot_pieces = config.dot_min_ms * std::pow(config.speed_ratio, speed) / piece_ms;
            by_kind_and_speed_.push_back(Params{std::log(config.fist_medians_dots[kind] * dot_pieces),
                                                config.fist_shifts_dots[kind] * dot_pieces,
                                                config.fist_sigmas[kind]});
        }
    }
}

Fist::Params Fist::params(Kind kind, int speed, int tau) const {
    const std::size_t k = static_cast<std::size_t>(kind);  // a negative value wraps to a large one
    if (k >= kKinds) throw std::out_of_range("fist: unknown kind");
    if (speed < 0 || speed >= speed_count_) throw std::out_of_range("fist: speed outside the grid");
    if (tau < 1) throw std::out_of_range("fist: tau must be at least 1 piece");
    return by_kind_and_speed_[k * static_cast<std::size_t>(speed_count_) + static_cast<std::size_t>(speed)];
}

double Fist::Params::z(double edge) const {
    const double above_shift = edge - shift;
    if (!(above_shift > 0.0)) return -kInf;
    return (std::log(above_shift) - ln_median) / sigma;
}

double Fist::log_survival(Kind kind, int speed, int tau) const {
    const Params p = params(kind, speed, tau);
    if (tau == 1) return 0.0;  // never shorter than one piece
    return ln_phic(p.z(tau - 0.5));
}

double Fist::log_end(Kind kind, int speed, int tau) const {
    const Params p = params(kind, speed, tau);
    // The first length takes everything up to 3/2, the others the rounding interval (tau - 1/2, tau + 1/2].
    const double lower = tau == 1 ? -kInf : p.z(tau - 0.5);
    return ln_mass(lower, p.z(tau + 0.5));
}

}  // namespace kz4ap::viterbi
