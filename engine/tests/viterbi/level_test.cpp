#include "kz4ap/viterbi/level.hpp"

#include "kz4ap/channelizer.hpp"
#include "kz4ap/viterbi/config.hpp"
#include "kz4ap/viterbi/observation.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kz4ap::viterbi;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kPi = std::numbers::pi;

// s, dimensionless: the standard deviation over the mean of a Rayleigh amplitude, sqrt(4 / pi - 1) = 0.5227, written
// out here on its own, not read from level.cpp.
const double kRayleighRelativeSpread = std::sqrt(4.0 / kPi - 1.0);

// A learned level of mean mu (FS) whose standard deviation is relative_spread x mu, last updated at t_s (s).
Level learned(double mu, double relative_spread, double t_s = 0.0) {
    return {mu, (relative_spread * mu) * (relative_spread * mu), t_s, true};
}

// sqrt(P) / mu of a level.
double relative_spread_of(const Level& level) { return std::sqrt(level.p) / level.mu; }

// c(m), Hz, m = 0 ... max_samples, on the channelizer's own filter at 48 kHz (an FFT of 2 048 points: 1 500 samples/s).
std::vector<double> sum_noise_48k(int max_samples) {
    kz4ap::ChannelizerConfig config;
    config.sample_rate = 48000;
    config.fft_size = 2048;
    return sum_noise_hz(kz4ap::channel_filter_taps(config), config.sample_rate, config.fft_size / config.channel_bins,
                        max_samples);
}

// The noise variance V(m), FS^2, of a sum of m channel samples at 48 kHz with sigma = 0.02 FS per input sample.
double block_variance(int m) {
    const double n0 = 0.02 * 0.02 / 48000.0;  // FS^2/Hz
    return n0 * sum_noise_48k(m)[static_cast<std::size_t>(m)];
}

// The flat prior of the default configuration for blocks of 1 to 4 pieces of 3 samples, built once for the tests that
// read it (its tables take a fraction of a second).
const FlatPrior& flat_prior() {
    static const std::vector<double> c = sum_noise_48k(12);
    static const FlatPrior prior(ViterbiConfig{}, c, 4, 3);
    return prior;
}

}  // namespace

TEST(Level, ADefaultLevelIsANewborns) {
    const Level newborn;
    EXPECT_FALSE(newborn.learned);
    EXPECT_EQ(newborn.mu, 0.0);
    EXPECT_EQ(newborn.p, 0.0);
    EXPECT_EQ(newborn.t_s, 0.0);
}

TEST(Level, PredictionGivesTheNotesUncertainties) {
    // From P = 0 (a level known exactly, last updated at 10 s), the next mark two dots later (the lag tau =
    // 2 x 1.2 s / WPM) at f_D = 1 Hz is uncertain by +/-49%, +/-19% and +/-10% of the level at 5, 20 and 40 WPM: the
    // notes' roundings of sqrt(P) / mu = 0.4951, 0.1903 and 0.0977 (derived), to the digits shown.
    struct Row {
        double wpm;
        double notes;
        double derived;
    };
    for (const Row row : {Row{5.0, 0.49, 0.4951}, Row{20.0, 0.19, 0.1903}, Row{40.0, 0.10, 0.0977}}) {
        SCOPED_TRACE(std::to_string(row.wpm) + " WPM");
        const Level known = learned(0.25, 0.0, 10.0);
        const Level predicted = predict(known, 10.0 + 2.0 * 1.2 / row.wpm, 1.0);
        EXPECT_NEAR(relative_spread_of(predicted), row.notes, 0.01);
        EXPECT_NEAR(relative_spread_of(predicted), row.derived, 5e-5);
        // A prediction is not an update: the mean, the time of the last update and the flag are the level's.
        EXPECT_EQ(predicted.mu, known.mu);
        EXPECT_EQ(predicted.t_s, known.t_s);
        EXPECT_TRUE(predicted.learned);
    }
}

TEST(Level, NoSpreadKeepsTheVariance) {
    // With f_D = 0, rho = 1 at any lag: P is what it was, to the last bit, however long ago the level was measured.
    for (const double level_spread : {0.0, 1e-4, 0.5, 2.0}) {
        for (const double lag_s : {0.0, 0.002, 1.0, 600.0, 1e9}) {
            SCOPED_TRACE("level spread " + std::to_string(level_spread) + ", lag " + std::to_string(lag_s) + " s");
            const Level known = learned(0.01, level_spread, 3.0);
            const Level predicted = predict(known, 3.0 + lag_s, 0.0);
            EXPECT_EQ(predicted.p, known.p);
            EXPECT_EQ(predicted.mu, known.mu);
        }
    }
    // And with fading but no lag (rho = 1 again), at any finite spread: the level is predicted to the time it was
    // measured.
    for (const double level_spread : {0.0, 0.5, 2.0}) {
        const Level known = learned(0.01, level_spread, 3.0);
        for (const double fading_hz : {0.3, 1.0, 5.0, 1e308}) EXPECT_EQ(predict(known, 3.0, fading_hz).p, known.p);
    }
}

TEST(Level, PredictionTendsToTheFadingSpread) {
    // At f_D = 1 Hz, after 10 s (rho^2 = e^-987, which is 0 as a double), sqrt(P) / mu is s = sqrt(4 / pi - 1) =
    // 0.5227, from P = 0 and from a level known worse than that, sqrt(P) / mu = 2: the form interpolates between P and
    // (s mu)^2, so a level known worse than the fading's own spread becomes better known with time. The test pins that
    // behavior of the form as it stands.
    EXPECT_NEAR(kRayleighRelativeSpread, 0.5227, 5e-5);  // the digits the header prints
    for (const double start : {0.0, 2.0}) {
        SCOPED_TRACE("start sqrt(P) / mu = " + std::to_string(start));
        const Level predicted = predict(learned(0.25, start, 1.0), 11.0, 1.0);
        EXPECT_NEAR(relative_spread_of(predicted), kRayleighRelativeSpread, 1e-6);
    }
    // On the way P stays on its side of (s mu)^2 and comes closer to it with every lag, rho^2 only falling: in steps of
    // 50 ms to 0.5 s, where the gap (rho^2 times what it was, at least 8%) is far above the rounding of P.
    const double mu = 0.25;
    const double target = (kRayleighRelativeSpread * mu) * (kRayleighRelativeSpread * mu);
    for (const double start : {0.0, 0.1, 2.0}) {
        SCOPED_TRACE("start sqrt(P) / mu = " + std::to_string(start));
        const Level known = learned(mu, start, 0.0);
        double previous_gap = std::fabs(known.p - target);
        for (int k = 1; k <= 10; ++k) {
            const double lag_s = 0.05 * k;
            const double gap = predict(known, lag_s, 1.0).p - target;
            EXPECT_GT(gap * (known.p - target), 0.0) << "lag " << lag_s << " s";
            EXPECT_LT(std::fabs(gap), previous_gap) << "lag " << lag_s << " s";
            previous_gap = std::fabs(gap);
        }
    }
}

TEST(Level, PredictionMatchesTheFormula) {
    // P(t) = rho^2 P + (s mu)^2 (1 - rho^2), rho = exp(-pi^2 f_D^2 tau^2 / 2) at the lag tau = t - t_s, evaluated here
    // directly, over spreads of 0.3 to 5 Hz, lags of 10 ms to 1.5 s and levels known to 0 to 200%.
    for (const double fading_hz : {0.3, 1.0, 2.0, 5.0}) {
        for (const double lag_s : {0.01, 0.06, 0.12, 0.48, 1.5}) {
            for (const double level_spread : {0.0, 0.1, 0.5, 2.0}) {
                SCOPED_TRACE("f_D " + std::to_string(fading_hz) + " Hz, lag " + std::to_string(lag_s) +
                             " s, level spread " + std::to_string(level_spread));
                const double mu = 0.3;
                const Level known = learned(mu, level_spread, 7.0);
                const double rho = std::exp(-0.5 * kPi * kPi * fading_hz * fading_hz * lag_s * lag_s);
                const double stationary = kRayleighRelativeSpread * mu;  // FS: s mu
                const double expected = rho * rho * known.p + stationary * stationary * (1.0 - rho * rho);
                const Level predicted = predict(known, 7.0 + lag_s, fading_hz);
                EXPECT_NEAR(predicted.p, expected, 1e-9 * expected);
            }
        }
    }
}

TEST(Level, PredictionKeepsItsPrecisionAtShortLags) {
    // From P = 0, P / mu^2 = s^2 (1 - rho^2) = s^2 x (1 - x / 2 + x^2 / 6 ...) with x = pi^2 f_D^2 tau^2, down to
    // lags where 1 - exp(-x) would give 0 (x = 9.9e-18 at 1 ns, f_D = 1 Hz, is below the doubles' step at 1, 1.1e-16)
    // or only a few digits (x = 9.9e-12 at 1 us).
    for (const double lag_s : {1e-9, 1e-6, 1e-3}) {
        SCOPED_TRACE("lag " + std::to_string(lag_s) + " s");
        const double x = kPi * kPi * lag_s * lag_s;
        const double expected = kRayleighRelativeSpread * kRayleighRelativeSpread * x * (1.0 - x / 2.0 + x * x / 6.0);
        const Level predicted = predict(learned(1.0, 0.0, 0.0), lag_s, 1.0);
        EXPECT_NEAR(predicted.p, expected, 1e-13 * expected);
    }
}

TEST(Level, PredictRefusesWhatItCannotRead) {
    const Level known = learned(0.25, 0.1, 5.0);
    EXPECT_NO_THROW(predict(known, 5.0, 1.0));  // a lag of 0
    EXPECT_NO_THROW(predict(known, 6.0, 0.0));  // no fading
    // A newborn has nothing to predict from.
    EXPECT_THROW(predict(Level{}, 6.0, 1.0), std::invalid_argument);
    Level not_yet = known;
    not_yet.learned = false;
    EXPECT_THROW(predict(not_yet, 6.0, 1.0), std::invalid_argument);
    // The search predicts forward only.
    EXPECT_THROW(predict(known, 4.999, 1.0), std::invalid_argument);
    for (const double t_s : {kNaN, kInf, -kInf}) EXPECT_THROW(predict(known, t_s, 1.0), std::invalid_argument) << t_s;
    for (const double spread_hz : {-1.0, -1e-12, kNaN, kInf, -kInf})
        EXPECT_THROW(predict(known, 6.0, spread_hz), std::invalid_argument) << spread_hz;
}

TEST(Level, OneDotMovesTheLevel) {
    // The notes' example (topic 3, item 1), in the notes' units: independent pieces of noise of unit variance, V = n
    // for a sum of n pieces, a piece's carrier amplitude sqrt(S500) (a piece of 2 ms is 1 / (500 Hz)). A 20 WPM dot
    // lasts 60 ms, n = 30 pieces; at 10 dB S500 its energy over N0 is E/N0 = 10 x 500 Hz x 0.060 s = 300 and its
    // noise-free sum is |S| = n a with a = sqrt(E/N0 / n) = sqrt(10). The stored level, 10 dB S500 above it, has the
    // amplitude 10^(10/20) a = 10 and is known to +/-10% (sqrt(P) = 1). The linear Kalman gain, P / (P + V / (2 n^2)) =
    // 0.984, moves the estimate 98% of the way to the dot's own amplitude; the moment-matched update must move it at
    // least 95% of the way (it moves it 98.40%).
    const int n = 30;
    const double e_over_n0 = std::pow(10.0, 10.0 / 10.0) * 500.0 * 0.060;  // 300: the dot is at 10 dB S500
    const double own = std::sqrt(e_over_n0 / n);                          // FS: sqrt(10)
    const double stored = std::pow(10.0, 10.0 / 20.0) * own;             // FS: 10
    const Level before = learned(stored, 0.1, 0.0);
    ASSERT_NEAR(before.p, 1.0, 1e-12);
    const double v = n;  // FS^2
    const BlockScore score = score_block(before, n * own, n, v, ViterbiConfig{}.quadrature_order, 0.060);
    const double moved = (stored - score.level.mu) / (stored - own);
    EXPECT_GE(moved, 0.95);
    EXPECT_LE(moved, 1.0 + 1e-3);  // and not beyond the dot's own level
    // The level is left as well known as the Kalman filter's, P R / (P + R) with R = V / (2 n^2) (within 3%).
    const double r = v / (2.0 * n * n);
    EXPECT_NEAR(score.level.p, before.p * r / (before.p + r), 0.03 * before.p * r / (before.p + r));
    EXPECT_EQ(score.level.t_s, 0.060);
    EXPECT_TRUE(score.level.learned);
}

TEST(Level, ANewbornStartsFromItsFirstBlock) {
    // After one block of 2 pieces (m = 6 samples) at z = |S| / sqrt(V) = 20, the level's mean is within 2% of
    // z sqrt(V) / m = |S| / m, the block's mean amplitude (the flat prior's posterior of b = m a / sqrt(V) has the mean
    // z - 3 / (4 z) = 19.96 there, 0.2% below z); its variance is near the likelihood's, V / (2 m^2) in FS^2.
    const FlatPrior& prior = flat_prior();
    const int pieces = 2;
    const int m = prior.samples_per_piece() * pieces;
    const double v = block_variance(m);
    const double z = 20.0;
    const double s_abs = z * std::sqrt(v);
    const BlockScore score = score_first_block(prior, s_abs, pieces, v, 0.004);
    const double block_amplitude = z * std::sqrt(v) / m;
    EXPECT_NEAR(score.level.mu, block_amplitude, 0.02 * block_amplitude);
    EXPECT_NEAR(score.level.p, v / (2.0 * m * m), 0.02 * v / (2.0 * m * m));
    EXPECT_TRUE(score.level.learned);
    EXPECT_EQ(score.level.t_s, 0.004);
    EXPECT_EQ(score.mode_a, score.level.mu);
}

TEST(Level, ScoreFirstBlockReadsTheFlatPrior) {
    // The score is the flat prior's evidence at z = |S| / sqrt(V) and the level is its posterior of b converted to FS:
    // mu = mean_b sqrt(V) / m, P = var_b V / m^2, with m = samples_per_piece x pieces; on and between the table's
    // nodes and above its top (z = 40), where the flat prior's large-z form serves (z = 1 234.5 and 1e5 included).
    const FlatPrior& prior = flat_prior();
    for (const int pieces : {1, 2, 4}) {
        const int m = prior.samples_per_piece() * pieces;
        const double v = block_variance(m);
        const double root_v = std::sqrt(v);
        for (const double z : {0.0, 0.37, 5.0, 20.0, 39.99, 45.0, 1234.5, 1e5}) {
            SCOPED_TRACE(std::to_string(pieces) + " pieces, z = " + std::to_string(z));
            const double s_abs = z * root_v;
            const double z_read = s_abs / root_v;
            const BlockScore score = score_first_block(prior, s_abs, pieces, v, 1.5);
            EXPECT_EQ(score.llr, prior.llr(pieces, z_read));
            const Moments b = prior.posterior(pieces, z_read);
            EXPECT_DOUBLE_EQ(score.level.mu, b.mean * root_v / m);
            EXPECT_DOUBLE_EQ(score.level.p, b.var * v / (static_cast<double>(m) * m));
            EXPECT_EQ(score.mode_a, score.level.mu);
            EXPECT_EQ(score.level.t_s, 1.5);
            EXPECT_TRUE(score.level.learned);
        }
    }
}

TEST(Level, ScoreBlockIsOneEvidenceCall) {
    // score_block is block_evidence called once with the predicted level as the prior: its evidence, the level's mean
    // and variance and the mode are the same doubles, with no starting point, with the mode of the same block's
    // previous piece, and with a poor one; the level's time is the given one and it is learned. Blocks of 24 samples
    // with the prior's center b = m mu / sqrt(V) of 1, 10 and 30, z = 0, b / 2 and 2 b, and levels known to 0%
    // (exactly), 10%, 50% and 200%, at orders 2, 5 and 12.
    const int m = 24;
    const double v = block_variance(m);
    const double root_v = std::sqrt(v);
    for (const int order : {2, 5, 12}) {
        for (const double b : {1.0, 10.0, 30.0}) {
            for (const double z : {0.0, b / 2.0, 2.0 * b}) {
                for (const double level_spread : {0.0, 0.1, 0.5, 2.0}) {
                    SCOPED_TRACE("order " + std::to_string(order) + ", b = " + std::to_string(b) + ", z = " +
                                 std::to_string(z) + ", level spread " + std::to_string(level_spread));
                    const Level predicted = learned(b * root_v / m, level_spread, 2.0);
                    const double s_abs = z * root_v;
                    const double t_s = 2.5;
                    const auto check = [&](std::optional<double> start_a) {
                        const BlockEvidence expected =
                            block_evidence(s_abs, m, predicted.mu, predicted.p, v, order, start_a);
                        const BlockScore score = score_block(predicted, s_abs, m, v, order, t_s, start_a);
                        EXPECT_EQ(score.llr, expected.llr);
                        EXPECT_EQ(score.level.mu, expected.level.mean);
                        EXPECT_EQ(score.level.p, expected.level.var);
                        EXPECT_EQ(score.mode_a, expected.mode_a);
                        EXPECT_EQ(score.level.t_s, t_s);
                        EXPECT_TRUE(score.level.learned);
                    };
                    check(std::nullopt);
                    const BlockEvidence cold = block_evidence(s_abs, m, predicted.mu, predicted.p, v, order);
                    check(cold.mode_a);
                    check(3.0 * cold.mode_a + 0.1 * predicted.mu);
                }
            }
        }
    }
}

TEST(Level, ScoreBlockRefusesWhatItCannotScore) {
    const int m = 24;
    const double v = block_variance(m);
    const Level predicted = learned(0.01, 0.1, 2.0);
    const double s_abs = 0.1;
    EXPECT_NO_THROW(score_block(predicted, s_abs, m, v, 5, 2.5));
    // A newborn's first block goes to score_first_block.
    EXPECT_THROW(score_block(Level{}, s_abs, m, v, 5, 2.5), std::invalid_argument);
    Level not_yet = predicted;
    not_yet.learned = false;
    EXPECT_THROW(score_block(not_yet, s_abs, m, v, 5, 2.5), std::invalid_argument);
    for (const double t_s : {kNaN, kInf, -kInf})
        EXPECT_THROW(score_block(predicted, s_abs, m, v, 5, t_s), std::invalid_argument) << t_s;
    // What block_evidence refuses passes through: a block without samples, an order that holds no variance, a noise
    // variance that is not positive, a sum that is negative, a starting point that is not finite.
    EXPECT_THROW(score_block(predicted, s_abs, 0, v, 5, 2.5), std::invalid_argument);
    EXPECT_THROW(score_block(predicted, s_abs, m, v, 1, 2.5), std::invalid_argument);
    EXPECT_THROW(score_block(predicted, s_abs, m, 0.0, 5, 2.5), std::invalid_argument);
    EXPECT_THROW(score_block(predicted, -s_abs, m, v, 5, 2.5), std::invalid_argument);
    EXPECT_THROW(score_block(predicted, s_abs, m, v, 5, 2.5, kNaN), std::invalid_argument);
}

TEST(Level, ScoreFirstBlockRefusesWhatItCannotScore) {
    const FlatPrior& prior = flat_prior();
    const int m = 6;
    const double v = block_variance(m);
    const double s_abs = 5.0 * std::sqrt(v);
    EXPECT_NO_THROW(score_first_block(prior, s_abs, 2, v, 0.004));
    EXPECT_NO_THROW(score_first_block(prior, 0.0, 2, v, 0.004));
    // Blocks of 1 ... max_pieces pieces only: the flat prior's table ends there. (Only the arguments are checked: the
    // v of 2 pieces does not belong to the other lengths.)
    EXPECT_THROW(score_first_block(prior, s_abs, 0, v, 0.004), std::out_of_range);
    EXPECT_THROW(score_first_block(prior, s_abs, prior.max_pieces() + 1, v, 0.004), std::out_of_range);
    EXPECT_NO_THROW(score_first_block(prior, s_abs, prior.max_pieces(), v, 0.004));
    for (const double bad_v : {0.0, -1.0, kNaN, kInf})
        EXPECT_THROW(score_first_block(prior, s_abs, 2, bad_v, 0.004), std::invalid_argument) << bad_v;
    for (const double bad_s_abs : {-1.0, kNaN, kInf})
        EXPECT_THROW(score_first_block(prior, bad_s_abs, 2, v, 0.004), std::invalid_argument) << bad_s_abs;
    // A sum so large against the noise that z = s_abs / sqrt(v) leaves the doubles is refused by the flat prior.
    EXPECT_THROW(score_first_block(prior, 1e300, 2, 1e-300, 0.004), std::invalid_argument);
    for (const double bad_t_s : {kNaN, kInf, -kInf})
        EXPECT_THROW(score_first_block(prior, s_abs, 2, v, bad_t_s), std::invalid_argument) << bad_t_s;
}

TEST(Level, ANewbornsLevelServesItsNextBlock) {
    // The level a newborn learns is in FS, and so is the prior of its next block: a steady carrier of amplitude a per
    // channel sample, read noise-free (|S| = m a), first as a block of 2 pieces (4 ms) by score_first_block, then, 6 ms
    // later, as a block of 3 pieces by score_block against the level predicted to that time at f_D = 1 Hz. The second
    // block finds the level where the first left it (within 2%) and leaves it better known than the prediction; as a
    // mark of a known carrier it is strong evidence (z = 23 here: 520 nats, asserted above 100).
    const FlatPrior& prior = flat_prior();
    const int first_m = 6;
    const int second_m = 9;
    const double first_v = block_variance(first_m);
    const double second_v = block_variance(second_m);
    const double a = 20.0 * std::sqrt(first_v) / first_m;  // FS: z = 20 in the first block
    const BlockScore first = score_first_block(prior, first_m * a, 2, first_v, 0.004);
    const Level predicted = predict(first.level, 0.010, 1.0);
    EXPECT_GE(predicted.p, first.level.p);
    const BlockScore second = score_block(predicted, second_m * a, second_m, second_v, 5, 0.010, first.mode_a);
    EXPECT_NEAR(second.level.mu, a, 0.02 * a);
    EXPECT_LT(second.level.p, predicted.p);
    EXPECT_GT(second.llr, 100.0);
    EXPECT_EQ(second.level.t_s, 0.010);
    EXPECT_TRUE(second.level.learned);
}
