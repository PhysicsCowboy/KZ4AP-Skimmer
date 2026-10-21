#include "kz4ap/viterbi/search.hpp"

#include "../test_signals.hpp"
#include "kz4ap/channelizer.hpp"
#include "kz4ap/decoder.hpp"
#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/config.hpp"
#include "kz4ap/viterbi/fist.hpp"
#include "kz4ap/viterbi/level.hpp"
#include "kz4ap/viterbi/morse_tree.hpp"
#include "kz4ap/viterbi/observation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace kz4ap::viterbi;
namespace signals = kz4ap::test;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

constexpr int kInputRate = 48000;                     // samples/s: DEV2's recordings
constexpr double kSigma = 0.02;                       // FS per input sample: DEV2's noise
constexpr double kN0 = kSigma * kSigma / kInputRate;  // FS^2/Hz: N0 = sigma^2 / fs
constexpr double kChannelRate = 1500.0;               // channel samples/s
constexpr int kSamplesPerPiece = 3;                   // a piece of 2 ms
constexpr int kDefaultBlockPieces = 195;  // floor(390.37 ms / 2 ms): f_D 1 Hz, loss 0.5 dB of signal energy
constexpr int kChannelBin = 32;                       // the channel's center, 750 Hz: the stations sit on it

// The engine's channelizer at 48 kHz: an FFT of 2 048 points, 1 500 channel samples/s.
kz4ap::ChannelizerConfig channelizer_config() {
    kz4ap::ChannelizerConfig config;
    config.sample_rate = kInputRate;
    config.fft_size = 2048;
    return config;
}

// c(m), Hz, on the channelizer's own filter at 48 kHz, for m = 0 ... 3 x 195 channel samples (the default block).
const std::vector<double>& sum_noise() {
    static const std::vector<double> c = [] {
        const kz4ap::ChannelizerConfig config = channelizer_config();
        return sum_noise_hz(kz4ap::channel_filter_taps(config), config.sample_rate,
                            config.fft_size / config.channel_bins, kSamplesPerPiece * kDefaultBlockPieces);
    }();
    return c;
}

// The rise of a mark's first pieces on the channelizer's filter at 48 kHz with the default 5 ms keying edges:
// g_1 ... g_4 (0.193, 0.614, 0.943 and 1.019 of a full piece), the matched filter's weights in a mark's first block.
const std::vector<double>& rise() {
    static const std::vector<double> g = [] {
        const kz4ap::ChannelizerConfig config = channelizer_config();
        return rise_fractions(kz4ap::channel_filter_taps(config), config.sample_rate,
                              config.fft_size / config.channel_bins, kSamplesPerPiece, ViterbiConfig{}.keying_rise_s);
    }();
    return g;
}

// The matched filter's systematic error at a mark's start on the same filter and edge: E_1 ... E_N (0.00037, 0.0095,
// 0.0224, ..., 0.0210 from n = 7), the edge's share of the systematic term (search.hpp, "The systematic term").
const std::vector<double>& template_errors() {
    static const std::vector<double> e = [] {
        const kz4ap::ChannelizerConfig config = channelizer_config();
        return rise_template_errors(kz4ap::channel_filter_taps(config), config.sample_rate,
                                    config.fft_size / config.channel_bins, kSamplesPerPiece,
                                    ViterbiConfig{}.keying_rise_s);
    }();
    return e;
}

// The noise's long-run rate, Hz per channel sample: c's last increment, 1 500 Hz.
double noise_rate_hz() {
    const std::vector<double>& c = sum_noise();
    return c[c.size() - 1] - c[c.size() - 2];
}

// The default configuration's flat prior for blocks of 1 to 195 pieces of 3 samples, with the rise and its template
// errors, as the decoder's shared parts build it, built once (0.22 s, 18.7 MB).
std::shared_ptr<const FlatPrior> flat_prior() {
    static const auto prior = std::make_shared<const FlatPrior>(ViterbiConfig{}, sum_noise(), kDefaultBlockPieces,
                                                                kSamplesPerPiece, rise(), template_errors());
    return prior;
}

// The pieces the search holds back for the look-ahead of its systematic term at 2 ms: ceil(4 ms / 2 ms).
constexpr std::size_t kLookahead = 2;

// The systematic term's carrier power at every piece (search.hpp, "The systematic term"), FS^2, recomputed from the
// pieces: at piece j, the largest |piece|^2 of pieces j - window + 1 ... j + kLookahead that exist, window being
// min(B, the flat prior's span) pieces; with lookahead false, of pieces j - window + 1 ... j only.
std::vector<double> carrier_powers(const std::vector<std::complex<double>>& pieces, int window, bool lookahead = true) {
    std::vector<double> powers(pieces.size(), 0.0);
    for (std::size_t j = 0; j < pieces.size(); ++j) {
        const std::size_t span = static_cast<std::size_t>(window);
        const std::size_t from = j + 1 >= span ? j + 1 - span : 0;
        const std::size_t to = lookahead ? std::min(pieces.size() - 1, j + kLookahead) : j;
        for (std::size_t i = from; i <= to; ++i) powers[j] = std::max(powers[j], std::norm(pieces[i]));
    }
    return powers;
}

// The station's frequency, Hz: the center of channel bin 32 (23.4375 Hz per bin at 48 kHz).
double station_hz() { return kChannelBin * static_cast<double>(kInputRate) / channelizer_config().fft_size; }

// The amplitude, FS, of a key-down carrier at s500_db dB S500 over DEV2's noise: S500 = 10 log10(a^2 / (N0 x 500 Hz)).
double amplitude_at(double s500_db) { return std::sqrt(std::pow(10.0, s500_db / 10.0) * kN0 * 500.0); }

// The pieces of a 48 kHz input: through the channelizer, on the channel centered on the station (so no re-centering is
// needed), 3 consecutive channel samples summed per piece.
std::vector<std::complex<double>> pieces_of(const std::vector<kz4ap::Sample>& input) {
    kz4ap::Channelizer channelizer(channelizer_config());
    channelizer.add_channel(1, kChannelBin);
    std::vector<std::complex<double>> pieces;
    std::complex<double> sum = 0.0;
    int held = 0;
    channelizer.push(input, [&](std::uint32_t, std::uint64_t, std::span<const kz4ap::Sample> samples) {
        for (const kz4ap::Sample& s : samples) {
            sum += std::complex<double>(s.real(), s.imag());
            if (++held == kSamplesPerPiece) {
                pieces.push_back(sum);
                sum = 0.0;
                held = 0;
            }
        }
    });
    return pieces;
}

// A station keyed by the given intervals at s500_db dB S500 on DEV2's noise, as pieces.
std::vector<std::complex<double>> station(const std::vector<std::pair<double, double>>& keying, double s500_db,
                                          double duration_s, unsigned seed) {
    return pieces_of(signals::keyed_signal_of(keying, kInputRate, duration_s, station_hz(), amplitude_at(s500_db),
                                              kSigma, seed));
}

// DEV2's noise alone, as pieces.
std::vector<std::complex<double>> noise(double duration_s, unsigned seed) {
    return pieces_of(signals::keyed_signal_of({}, kInputRate, duration_s, station_hz(), 0.0, kSigma, seed));
}

// The time of piece k, s: the end of its last channel sample, the first channel sample starting at 0 s; one expression,
// never a sum of increments.
double piece_time(std::size_t k) {
    return static_cast<double>(kSamplesPerPiece) * static_cast<double>(k + 1) / kChannelRate;
}

// Every piece given, then those the look-ahead held searched.
void search_all(Search& search, const std::vector<std::complex<double>>& pieces) {
    for (std::size_t k = 0; k < pieces.size(); ++k) search.step(pieces[k], piece_time(k), kN0);
    search.finish();
}

std::string text_of(const std::vector<kz4ap::DecodedSymbol>& symbols) {
    std::string text;
    for (const kz4ap::DecodedSymbol& symbol : symbols) text += symbol.text;
    return text;
}

std::string decode(const ViterbiConfig& config, const std::vector<std::complex<double>>& pieces) {
    Search search(config, sum_noise(), flat_prior());
    search_all(search, pieces);
    return text_of(search.best_text());
}

// The character error rate: the Levenshtein distance from the reference over its length.
double cer(const std::string& reference, const std::string& decoded) {
    std::vector<std::size_t> row(decoded.size() + 1);
    for (std::size_t j = 0; j < row.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= reference.size(); ++i) {
        std::size_t diagonal = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= decoded.size(); ++j) {
            const std::size_t above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (reference[i - 1] != decoded[j - 1] ? 1u : 0u)});
            diagonal = above;
        }
    }
    return static_cast<double>(row[decoded.size()]) / static_cast<double>(reference.size());
}

// A story's state, as merging compares it.
std::tuple<Activity, bool, int, int, int> state_of(const StoryView& story) {
    return {story.activity, story.key_down, story.tau, story.speed, story.node};
}

// A story's element as the booking tests recompute it (design notes, topic 5, item 9): its score against "noise only"
// at the piece its state was created (its first piece is the next), and for a mark the evidence of its blocks, advanced
// one piece at a time as the search must book it.
struct Element {
    double start_score_nats;
    Level level;             // the level its current block is scored against: the story's, then each finished block's
    std::size_t next_piece;  // the next piece to add to the mark
    double finished = 0.0;   // nats: the finished blocks' evidence
    double current = 0.0;    // nats: the current block's evidence so far
    std::complex<double> sum = 0.0;  // FS: the current block's sum, weighted in the mark's first block
    int n = 0;                       // pieces in the current block
    double squares = 0.0;            // the current block's sum of squared weights
    int mark_pieces = 0;             // pieces of the mark so far
    std::optional<double> start;
    Level last_level;  // the level the last piece's scoring call left: a successor's, if the mark ends at that piece
};

// An element's identity: key down, speed, node and the piece its state was created.
using ElementKey = std::tuple<bool, int, int, std::size_t>;

// The systematic term's carrier power at every piece, with and without the look-ahead (carrier_powers), and counts of
// which part decided C^2 in the reference's evaluations, so that a test can show that each part did.
struct Carrier {
    std::vector<double> power;   // FS^2: the window and the look-ahead
    std::vector<double> causal;  // FS^2: the window alone
    std::int64_t by_window = 0;     // evaluations where the window's own pieces decided
    std::int64_t by_lookahead = 0;  // where a piece of the look-ahead decided
    std::int64_t by_own = 0;        // where the block's own estimate decided
};

// The evidence of an element's mark through piece `last`, from the pieces, advancing it from where it stands: the
// finished blocks' and the current one's, each block of at most `block` pieces scored against the level it carries,
// predicted to the piece's time at config.fading_spread_hz, starting Newton's method from the previous piece's mode in
// the block; a newborn's first block (of at most min(block, the flat prior's max_pieces) pieces) against the flat
// prior. The mark's first block is the matched filter to the rise: piece j of the mark weighted by g_j (rise(), and 1
// past it), m_signal = 3 x the sum of the squared weights W_n; a later block is the plain sum, m_signal = 3 n. The
// error budget, recomputed here (search.hpp, "The systematic term"): V = N0 x rate x m_signal, the pieces independent
// at the long-run rate, plus C^2 (E_n + floor^2 W_n), C^2 the larger of the carrier power at the piece (`carrier`) and
// the block's own estimate (3 |S| / m_signal)^2, E_n the flat prior's template error in the mark's first block (0 in a
// later one; Observation.TemplateErrorsMatchThePerPositionFractions holds the table), floor = systematic_floor.
double advance_to(Element& element, std::size_t last, const std::vector<std::complex<double>>& pieces, int block,
                  const ViterbiConfig& config, Carrier& carrier) {
    const std::vector<double>& g = rise();
    for (; element.next_piece <= last; ++element.next_piece) {
        const std::size_t j = element.next_piece;
        ++element.mark_pieces;
        const bool first_block = element.mark_pieces == element.n + 1;  // no block of the mark has finished yet
        const double weight =
            first_block && element.mark_pieces <= static_cast<int>(g.size())
                ? g[static_cast<std::size_t>(element.mark_pieces) - 1]
                : 1.0;
        element.sum += weight * pieces[j];
        element.squares += weight * weight;
        ++element.n;
        const double m_signal = kSamplesPerPiece * element.squares;
        const double own = kSamplesPerPiece * std::abs(element.sum) / m_signal;  // FS
        const double c2 = std::max(carrier.power[j], own * own);                 // FS^2
        if (own * own >= carrier.power[j])
            ++carrier.by_own;
        else if (carrier.power[j] > carrier.causal[j])
            ++carrier.by_lookahead;
        else
            ++carrier.by_window;
        const double edge = first_block ? flat_prior()->template_error(element.n) : 0.0;
        const double floor = config.systematic_floor;
        const double v = kN0 * noise_rate_hz() * m_signal + c2 * (edge + floor * floor * (m_signal / kSamplesPerPiece));
        const double t = piece_time(j);
        const int limit = element.level.learned ? block : std::min(block, flat_prior()->max_pieces());
        const BlockScore score =
            element.level.learned
                ? score_block(predict(element.level, t, config.fading_spread_hz), std::abs(element.sum), m_signal, v,
                              config.quadrature_order, t, element.start)
                : score_first_block(*flat_prior(), std::abs(element.sum), element.n, v, t);
        element.current = score.llr;
        element.start = score.mode_a;
        element.last_level = score.level;
        if (element.n == limit) {
            element.finished += score.llr;
            element.level = score.level;
            element.current = 0.0;
            element.sum = 0.0;
            element.n = 0;
            element.squares = 0.0;
            element.start.reset();
        }
    }
    return element.finished + element.current;
}

// Whether two levels are the same: both not learned, or learned with the same time and moments (to 1e-12, relatively).
bool same_level(const Level& a, const Level& b) {
    if (a.learned != b.learned) return false;
    if (!a.learned) return true;
    return a.t_s == b.t_s && std::fabs(a.mu - b.mu) <= 1e-12 * std::fabs(b.mu) && std::fabs(a.p - b.p) <= 1e-12 * b.p;
}

// Whether the search's full-size tests run: the environment variable KZ4AP_SEARCH_FULL_SWEEP=1, which the full-sweep
// presets set through the test Search.FullSweep (engine/CMakeLists.txt).
bool full_sweep() {
    const char* value = std::getenv("KZ4AP_SEARCH_FULL_SWEEP");
    return value != nullptr && std::string(value) == "1";
}

}  // namespace

TEST(Search, DecodesACleanStationAtThreeSpeeds) {
    // "PARIS PARIS" at 12, 25 and 40 WPM, machine keying (exact PARIS timing), +20 dB S500 on DEV2's noise (sigma =
    // 0.02 FS at 48 kHz): the exact text (plan §5, item 2).
    for (const double wpm : {12.0, 25.0, 40.0}) {
        SCOPED_TRACE(std::to_string(wpm) + " WPM");
        const auto keying = signals::keying("PARIS PARIS", wpm, 0.5);
        EXPECT_EQ(decode(ViterbiConfig{}, station(keying, 20.0, keying.back().second + 1.5, 11)), "PARIS PARIS");
    }
}

TEST(Search, FollowsASpeedStep) {
    // A step from 20 to 30 WPM in mid-text, at +20 dB S500: a CER of at most 0.05 over the text (plan §5, item 2). The
    // old story misfits the new speed by (ln 1.5)^2 / (2 x 0.3^2) = 0.91 nats per element; a rescue newborn takes the
    // new speed when its first mark ends.
    const auto keying = signals::keying_segments({{"PARIS PARIS PARIS", 20.0}, {"PARIS PARIS PARIS", 30.0}}, 0.5);
    const std::string decoded = decode(ViterbiConfig{}, station(keying, 20.0, keying.back().second + 1.5, 12));
    EXPECT_LE(cer("PARIS PARIS PARIS PARIS PARIS PARIS", decoded), 0.05) << "decoded \"" << decoded << "\"";
}

TEST(Search, FollowsASpeedStepAtEverySnr) {
    // The full sweep, labeled full-sweep (the presets windows-full-sweep and linux-full-sweep run it, through the test
    // Search.FullSweep of engine/CMakeLists.txt, which sets KZ4AP_SEARCH_FULL_SWEEP=1; skipped otherwise): the step
    // from 20 to 30 WPM at +10, +11, ... +28 dB S500 on seeds 12, 1 and 2, a CER of at most 0.05 at every point, and
    // "PARIS PARIS" at 12, 25 and 40 WPM at the same levels on seeds 11, 1 and 2, exact.
    //
    // With the Farnsworth factor (before the owner's decision D14 of 2026-10-21) one point failed: at +10 dB S500 on
    // seed 12 the step decoded "PARIS PARIS PARIS P<AR>IS PARIS PARIS", CER 0.057, the ceiling's (search.hpp, "The
    // mark's rise and what remains", item 3); without it every point decodes exactly. Measured with MSVC's standard
    // library on the Windows PC: the test noise is std::normal_distribution's, whose algorithm is the library's, so
    // under another library the draws differ and a point may fail.
    if (!full_sweep()) GTEST_SKIP() << "the full sweep runs under the full-sweep presets (KZ4AP_SEARCH_FULL_SWEEP=1)";
    const std::string reference = "PARIS PARIS PARIS PARIS PARIS PARIS";
    const auto step = signals::keying_segments({{"PARIS PARIS PARIS", 20.0}, {"PARIS PARIS PARIS", 30.0}}, 0.5);
    for (int db = 10; db <= 28; ++db) {
        for (const unsigned seed : {12u, 1u, 2u}) {
            SCOPED_TRACE("the step at " + std::to_string(db) + " dB S500, seed " + std::to_string(seed));
            const std::string decoded = decode(ViterbiConfig{}, station(step, db, step.back().second + 1.5, seed));
            EXPECT_LE(cer(reference, decoded), 0.05) << "decoded \"" << decoded << "\"";
        }
    }
    for (const double wpm : {12.0, 25.0, 40.0}) {
        const auto keying = signals::keying("PARIS PARIS", wpm, 0.5);
        for (int db = 10; db <= 28; ++db) {
            for (const unsigned seed : {11u, 1u, 2u}) {
                SCOPED_TRACE(std::to_string(wpm) + " WPM at " + std::to_string(db) + " dB S500, seed " +
                             std::to_string(seed));
                EXPECT_EQ(decode(ViterbiConfig{}, station(keying, db, keying.back().second + 1.5, seed)),
                          "PARIS PARIS");
            }
        }
    }
}

TEST(Search, FarnsworthTextIsRight) {
    // Characters at 18 WPM sent at 5 WPM overall, so the gaps between letters and words are stretched by
    // F = (50 x 18 / 5 - 31) / 19 = 7.84, at +20 dB S500: the plan's criterion is the exact text (plan §5, item 2).
    //
    // A strict expected failure since the owner's decision D14 of 2026-10-21: the decoder has no Farnsworth factor
    // (standard spacing only), so Farnsworth-spaced signals decode poorly until a later fix (docs/backlog.md,
    // "Farnsworth spacing in the new decoder"). The test fails if the text is exact, so that the marker is removed
    // once the fix comes, and is skipped otherwise, with the decoded text.
    const double factor = signals::farnsworth_factor(18.0, 5.0);
    ASSERT_NEAR(factor, 7.84, 0.005);
    const auto keying = signals::keying_segments({{"PARIS PARIS", 18.0, factor}}, 0.5);
    const std::string decoded = decode(ViterbiConfig{}, station(keying, 20.0, keying.back().second + 1.5, 13));
    if (decoded == "PARIS PARIS")
        FAIL() << "Farnsworth spacing decodes exactly: the limitation is gone, remove the expected failure";
    GTEST_SKIP() << "strict expected failure: F = 7.84 decodes \"" << decoded
                 << "\" (no Farnsworth factor: docs/backlog.md, \"Farnsworth spacing in the new decoder\")";
}

TEST(Search, AnOverStartsAfterSilence) {
    // 5 s of noise alone, while the population fills with stories of noise, then "CQ CQ" at 25 WPM, +10 dB S500: the
    // exact text, with no space before it (plan §5, item 2).
    const auto keying = signals::keying("CQ CQ", 25.0, 5.0);
    EXPECT_EQ(decode(ViterbiConfig{}, station(keying, 10.0, keying.back().second + 1.5, 14)), "CQ CQ");
}

TEST(Search, MergesIdenticalStates) {
    // The rule, detail::replaces, by which the search's offers decide: a candidate replaces the one kept in its state
    // with a better score, or with an equal score if created earlier. Merging by it: of the candidates in one state,
    // the better score is kept, and of equal scores the earlier created; candidates in different states are all kept,
    // in the order given.
    EXPECT_TRUE(detail::replaces(-1.0, 9, -2.0, 1));
    EXPECT_FALSE(detail::replaces(-2.0, 1, -1.0, 9));
    EXPECT_TRUE(detail::replaces(-1.0, 2, -1.0, 9));
    EXPECT_FALSE(detail::replaces(-1.0, 9, -1.0, 2));
    EXPECT_FALSE(detail::replaces(-1.0, 4, -1.0, 4));
    using detail::Ranked;
    const std::vector<Ranked> candidates = {{7, -1.0, 1}, {3, -5.0, 2}, {7, -0.5, 3},
                                            {7, -0.5, 4}, {9, -2.0, 5}, {3, -6.0, 6}};
    EXPECT_EQ(detail::merge(candidates), (std::vector<std::size_t>{1, 2, 4}));
    // An equal score given earlier but created later loses to the earlier created.
    const std::vector<Ranked> reversed = {{5, -1.0, 9}, {5, -1.0, 2}};
    EXPECT_EQ(detail::merge(reversed), (std::vector<std::size_t>{1}));

    // In the search: after every piece searched no two live stories share a state, and there is at most one no-signal
    // story. At each piece where a newborn is born (a mark at the root, speed open, tau = 0), the newborn from silence
    // and a rescue newborn share that state and only one is left, the better: the newborn from silence stands at the
    // continuing no-signal story's score plus ln P plus ln(1/62) (to 1e-9 nats), and a rescue newborn kept instead
    // stands above that. (The search holds the last kLookahead pieces given for its look-ahead: the step that gives
    // piece k + kLookahead searches piece k.)
    ViterbiConfig config;
    config.stats = true;
    const double log_over_start = std::log(config.over_rate_per_s * config.piece_s);
    const double log_letter = -std::log(62.0);
    const auto keying = signals::keying("PARIS", 25.0, 0.6);
    const auto pieces = station(keying, 5.0, keying.back().second + 1.0, 15);
    Search search(config, sum_noise(), flat_prior());
    ASSERT_EQ(static_cast<std::size_t>(search.lookahead_pieces()), kLookahead);
    std::optional<double> no_signal_before;  // the no-signal story's own score after the previous piece, nats
    int newborns = 0;
    int rescues_kept = 0;
    int births_kept = 0;
    for (std::size_t given = 0; given < pieces.size(); ++given) {
        search.step(pieces[given], piece_time(given), kN0);
        if (given < kLookahead) continue;  // nothing searched yet
        const std::size_t k = given - kLookahead;  // the piece searched
        const double offset = search.normalization_nats();
        const std::vector<StoryView> stories = search.live_stories();
        int no_signal = 0;
        std::optional<double> no_signal_now;
        for (std::size_t i = 0; i < stories.size(); ++i) {
            if (i > 0) ASSERT_NE(state_of(stories[i - 1]), state_of(stories[i])) << "piece " << k;
            if (stories[i].activity == Activity::NoSignal) {
                ++no_signal;
                no_signal_now = stories[i].score_nats + offset;
            }
            const StoryView& s = stories[i];
            if (s.activity == Activity::Morse && s.key_down && s.tau == 0 && s.speed < 0 && s.node == 0 &&
                no_signal_before) {
                const double from_silence = *no_signal_before + std::log1p(-config.over_rate_per_s * config.piece_s) +
                                            log_over_start + log_letter;
                const double kept = s.score_nats + offset;
                EXPECT_GE(kept, from_silence - 1e-6) << "piece " << k;
                const LineKind kind = search.stats().lines.at(static_cast<std::size_t>(s.line)).kind;
                if (kind == LineKind::Rescue) {
                    EXPECT_GT(kept, from_silence) << "piece " << k;
                    ++rescues_kept;
                } else {
                    EXPECT_EQ(kind, LineKind::Silence) << "piece " << k;
                    EXPECT_NEAR(kept, from_silence, 1e-9) << "piece " << k;
                    ++births_kept;
                }
                ++newborns;
            }
        }
        ASSERT_LE(no_signal, 1) << "piece " << k;
        no_signal_before = no_signal_now;
    }
    EXPECT_GT(newborns, 100);
    EXPECT_GT(rescues_kept, 0);
    EXPECT_GT(births_kept, 100);
}

TEST(Search, PrunesByBeamThenCeiling) {
    // The rule: the beam drops those more than beam_nats behind the best (one exactly beam_nats behind is kept), then
    // the ceiling keeps the `ceiling` first by score, ties broken by the state, then by creation order.
    using detail::Ranked;
    const std::vector<Ranked> candidates = {{1, 0.0, 1},   {2, -10.0, 2}, {3, -40.0, 3},
                                            {4, -40.1, 4}, {6, -5.0, 5},  {5, -5.0, 6}};
    const detail::Pruning beam_only = detail::prune(candidates, 40.0, 10);
    EXPECT_EQ(beam_only.beam_dropped, (std::vector<std::size_t>{3}));
    EXPECT_TRUE(beam_only.ceiling_dropped.empty());
    EXPECT_EQ(beam_only.kept.size(), 5u);
    detail::Pruning both = detail::prune(candidates, 40.0, 2);
    std::sort(both.kept.begin(), both.kept.end());
    std::sort(both.ceiling_dropped.begin(), both.ceiling_dropped.end());
    EXPECT_EQ(both.kept, (std::vector<std::size_t>{0, 5}));  // 0 nats; then -5 nats in state 5 before state 6
    EXPECT_EQ(both.beam_dropped, (std::vector<std::size_t>{3}));
    EXPECT_EQ(both.ceiling_dropped, (std::vector<std::size_t>{1, 2, 4}));
    EXPECT_TRUE(detail::ranks_before({5, -5.0, 4}, {5, -5.0, 9}));
    EXPECT_FALSE(detail::ranks_before({5, -5.0, 9}, {5, -5.0, 4}));
    EXPECT_TRUE(detail::ranks_before({9, -4.0, 9}, {1, -5.0, 1}));

    // In the search, at a ceiling of 10 stories on a station at +5 dB S500 after a second of noise: after every piece
    // searched at most 10 stories live, every one within the beam of the best, the best at 0; the ceiling decided at
    // some pieces.
    ViterbiConfig config;
    config.ceiling = 10;
    config.stats = true;
    const auto keying = signals::keying("PARIS", 25.0, 1.0);
    const auto pieces = station(keying, 5.0, keying.back().second + 1.0, 16);
    Search search(config, sum_noise(), flat_prior());
    for (std::size_t given = 0; given < pieces.size(); ++given) {
        search.step(pieces[given], piece_time(given), kN0);
        if (given < kLookahead) continue;  // nothing searched yet
        const std::size_t k = given - kLookahead;  // the piece searched
        ASSERT_LE(search.live(), 10u) << "piece " << k;
        double best = -kInf;
        for (const StoryView& story : search.live_stories()) {
            ASSERT_GE(story.score_nats, -config.beam_nats) << "piece " << k;
            best = std::max(best, story.score_nats);
        }
        ASSERT_EQ(best, 0.0) << "piece " << k;
    }
    EXPECT_GT(search.stats().ceiling_pieces, 0);
}

TEST(Search, BooksEveryPointAtEveryPiece) {
    // Every story's score, at every piece, holds exactly what its story has earned so far (design notes, topic 5,
    // item 9). With f_D = 5 Hz the blocks are floor(78.07 ms / 2 ms) = 39 pieces, so the 144 ms dashes of "AT" at
    // 25 WPM (72 pieces) span two blocks. After every piece, each live story in an element begun at piece k0 must
    // stand, against "noise only" (its relative score plus normalization_nats()), above its own score at k0 by exactly:
    //   - in a mark at a known speed, at a node with both children: ln S_dash(tau), the merged mark's better survival
    //     (the dash's, whose median is three times the dot's), plus the evidence of its blocks so far, recomputed here
    //     from the pieces: the finished blocks' and the current one's, each block scored against the level it carries,
    //     predicted to the piece's time, starting Newton's method from the previous piece's mode in the block, the
    //     mark's first block weighted by the rise (the matched filter), every block's pieces at the long-run rate, and
    //     the systematic term added in quadrature to the noise in each block's error budget, its carrier power from
    //     the pieces of the last 39 (the window: min(B, the flat prior's span)) and the next 2 (the look-ahead), or the
    //     block's own estimate where larger (advance_to);
    //   - in a newborn's first mark (speed open): ln(1/32) + the best over the speeds and labels of ln S, plus the
    //     first block's evidence against the flat prior and the later blocks' against the level it left;
    //   - in a gap: the best over the kinds the node allows of the kind's cost and ln S (standard spacing: a letter
    //     gap's median 3 dots, a word gap's 7).
    // At +20 dB S500 the systematic term is 4% of a later block's noise variance and about 13% of a 24-piece first
    // block's, far above the tolerance; each part of the carrier power (the window's own pieces, the look-ahead and the
    // block's own estimate) decides it in many of the evaluations. The last piece, searched by finish() with no
    // look-ahead left, is checked too (its stories in an element begun at least 2 pieces before it).
    ViterbiConfig config;
    config.fading_spread_hz = 5.0;
    const int block = static_cast<int>(std::floor(block_length_s(config.fading_spread_hz, config.block_loss_db) /
                                                  config.piece_s));
    ASSERT_EQ(block, 39);
    const Fist fist(config);
    const MorseTree tree;
    const double log_speed_prior = -std::log(32.0);
    const double letter_gap_cost = -std::log1p(-config.word_end_p);
    const double word_gap_cost = -std::log(config.word_end_p * (1.0 - config.over_end_q));
    const auto keying = signals::keying("AT", 25.0, 0.5);
    const auto pieces = station(keying, 20.0, keying.back().second + 1.0, 17);
    const auto gap_timing = [&](int speed, int node, int tau) {
        double best = -kInf;
        if (tree.has_children(node)) best = fist.log_survival(Kind::ElementGap, speed, tau);
        if (!tree.symbol(node).empty()) {
            best = std::max(best, fist.log_survival(Kind::LetterGap, speed, tau) - letter_gap_cost);
            best = std::max(best, fist.log_survival(Kind::WordGap, speed, tau) - word_gap_cost);
        }
        return best;
    };

    std::map<ElementKey, Element> elements;
    int long_dashes = 0;   // known-speed marks checked past their first block
    int first_marks = 0;   // newborns' first marks checked past their first block
    int gaps = 0;
    Carrier carrier{carrier_powers(pieces, block), carrier_powers(pieces, block, false)};
    Search search(config, sum_noise(), flat_prior());
    // The check after the search of piece k; first_tau: the shortest element checked (2 after finish(), whose last
    // pieces are searched together: an element begun at piece k - 1 started at a piece not observed).
    int checked = 0;  // the stories the last call checked
    const auto check = [&](std::size_t k, int first_tau) {
        checked = 0;
        const double offset = search.normalization_nats();
        for (const StoryView& story : search.live_stories()) {
            if (story.activity != Activity::Morse) continue;
            const double own = story.score_nats + offset;
            const auto key = std::tuple{story.key_down, story.speed, story.node};
            if (story.tau == 0) {
                elements[std::tuple_cat(key, std::tuple{k})] = Element{own, story.level, k + 1};
                continue;
            }
            if (story.tau < first_tau) continue;
            const auto at = elements.find(std::tuple_cat(key, std::tuple{k - static_cast<std::size_t>(story.tau)}));
            ASSERT_NE(at, elements.end()) << "piece " << k;
            Element& element = at->second;
            double expected = 0.0;
            if (!story.key_down) {
                expected = gap_timing(story.speed, story.node, story.tau);
                ++gaps;
            } else if (story.speed >= 0) {
                if (!tree.has_children(story.node) || tree.child(story.node, false) < 0 ||
                    tree.child(story.node, true) < 0)
                    continue;
                const double dash = fist.log_survival(Kind::Dash, story.speed, story.tau);
                ASSERT_GE(dash, fist.log_survival(Kind::Dot, story.speed, story.tau));
                expected = dash + advance_to(element, k, pieces, block, config, carrier);
                if (story.tau > block) ++long_dashes;
            } else {
                double survival = -kInf;
                for (int s = 0; s < config.speed_count; ++s)
                    for (const Kind label : {Kind::Dot, Kind::Dash})
                        survival = std::max(survival, log_speed_prior + fist.log_survival(label, s, story.tau));
                expected = survival + advance_to(element, k, pieces, block, config, carrier);
                if (story.tau > block) ++first_marks;
            }
            ASSERT_NEAR(own - element.start_score_nats, expected, 1e-6)
                << "piece " << k << ", tau " << story.tau << ", speed " << story.speed << ", node " << story.node;
            ++checked;
        }
    };
    for (std::size_t given = 0; given < pieces.size(); ++given) {
        search.step(pieces[given], piece_time(given), kN0);
        if (given < kLookahead) continue;  // nothing searched yet
        check(given - kLookahead, 1);      // the piece searched
        if (::testing::Test::HasFatalFailure()) return;
    }
    // The last pieces, searched by finish() with the look-ahead that remains (none for the last): the last checked.
    search.finish();
    check(pieces.size() - 1, 2);
    if (::testing::Test::HasFatalFailure()) return;
    EXPECT_GT(checked, 0);
    EXPECT_GT(long_dashes, 10);
    EXPECT_GT(first_marks, 10);
    EXPECT_GT(gaps, 100);
    ::testing::Test::RecordProperty("carrier_by_window", std::to_string(carrier.by_window));
    ::testing::Test::RecordProperty("carrier_by_lookahead", std::to_string(carrier.by_lookahead));
    ::testing::Test::RecordProperty("carrier_by_own_estimate", std::to_string(carrier.by_own));
    EXPECT_GT(carrier.by_window, 100000);
    EXPECT_GT(carrier.by_lookahead, 10000);
    EXPECT_GT(carrier.by_own, 5000);
}

TEST(Search, BooksEveryTransition) {
    // Every story created at a piece (tau = 0: its next element starts at the next piece), and the no-signal story,
    // must stand, against "noise only", at the best over the previous piece's live stories that can make it
    // (search.hpp, point 2) of the following, within 1e-6 nats, each parent's element recomputed from its start score:
    //   - a gap at node N, after a mark that ended: the parent mark's start score (the mark from N's parent node, by
    //     N's label), plus its blocks' evidence through this piece, the current block finished, plus ln P_label(s, tau)
    //     at its speed s; for a newborn's first mark (speed open), ln(1/32) + ln P_label(s, tau) at the gap's speed (a
    //     gap at E or T is reached by both: a newborn's first mark and a letter's first mark at a known speed);
    //   - a mark at a node other than the root, an element's start: the parent gap's start score plus
    //     ln P_ElementGap(s, tau);
    //   - a mark at the root at a known speed s, a letter's start: the parent gap's start score plus
    //     ln(1/62) + ln(1 - epsilon), less the kind's cost, plus ln P_kind(s, tau), for a letter gap and a word gap;
    //   - the newborn (a mark at the root, speed open): the better of the birth from silence, the no-signal
    //     story's score plus ln(1 - P) + ln P + ln(1/62), and the rescue, the best letter start of the piece (as above,
    //     whether or not it was created) less ln(1 - epsilon) plus ln epsilon;
    //   - the no-signal story: the better of its own continuation, plus ln(1 - P), and every over's end: a mark that
    //     ends at a child holding a symbol, as for the gap, less ln(1/(p q)) (a newborn's first mark: its best speed).
    // Each must also carry its level: after a mark, the posterior of the block finished there; after a gap, the parent
    // gap's; none (not learned) for the newborn. A newborn that is not kept (it loses the merge, or the beam or the
    // ceiling drops it) is checked too, from the statistics: its line ends at its birth piece, and its deficit at birth
    // is the best score less its own. The stories of one piece in three are checked (a sample), after the step that
    // searches it (the look-ahead holds the last kLookahead pieces given). A mark's evidence is advance_to's, with the
    // systematic term in each block's error budget. On a station ("PARIS PARIS" at 25 WPM, +10 dB S500) every kind of
    // transition decides many of the stories checked. On 4 s of noise, where the no-signal story leads, no over's end,
    // word start or rescue decides one (counted), and every newborn from silence is kept; the rescues there are checked
    // as newborns not kept.
    ViterbiConfig config;
    config.stats = true;
    const int block = kDefaultBlockPieces;
    const Fist fist(config);
    const MorseTree tree;
    const int speeds = config.speed_count;
    const double over_start = config.over_rate_per_s * config.piece_s;  // P
    const double log_stay_silent = std::log1p(-over_start);
    const double log_over_start = std::log(over_start);
    const double log_letter = -std::log(62.0);
    const double log_keep = std::log1p(-config.rescue_epsilon);
    const double log_restart = std::log(config.rescue_epsilon);
    const double log_speed_prior = -std::log(static_cast<double>(speeds));
    const double letter_gap_cost = -std::log1p(-config.word_end_p);
    const double word_gap_cost = -std::log(config.word_end_p * (1.0 - config.over_end_q));
    const double over_end_cost = -std::log(config.word_end_p * config.over_end_q);
    const auto mark_kind = [](int label) { return label == 1 ? Kind::Dash : Kind::Dot; };
    const int root = tree.root();

    // What decided a story: the transition (or the continuation) its best candidate makes; and the newborns not kept.
    enum Decided {
        kMarkEnd, kElementStart, kLetterStart, kWordStart, kOverEnd, kBirth, kRescue, kSilence,
        kBirthNotKept, kRescueNotKept, kKinds
    };
    const std::array<const char*, kKinds> names = {
        "mark_ends", "element_starts", "letter_starts", "word_starts", "over_ends", "births_from_silence", "rescues",
        "silences", "births_from_silence_not_kept", "rescues_not_kept"};
    // The best candidate for a story, and the levels of all the candidates that reach it (to 1e-9 nats: ties).
    struct Best {
        double score = -kInf;
        Decided decided = kKinds;
        std::vector<Level> levels;
        void offer(double s, Decided d, const Level& level) {
            if (s > score + 1e-9) {
                score = s;
                decided = d;
                levels.assign(1, level);
            } else if (s >= score - 1e-9) {
                levels.push_back(level);
                if (s > score) {
                    score = s;
                    decided = d;
                }
            }
        }
    };
    // The previous piece's live stories, as the parents of this piece's, with their terms at this piece.
    struct PreviousMark {
        StoryView view;
        Element* element;
        double with_block = 0.0;  // nats: the start score plus the blocks' evidence through this piece
        // In `mark_ends`: at a known speed, ln P of the dot and the dash at it; at an open speed, ln P of each label at
        // each speed (label-major), then per label the best over the speeds of ln(1/32) + ln P.
        std::size_t ends = 0;
    };
    struct PreviousGap {
        StoryView view;
        double start;  // nats: its start score
        // In `letter_starts`, at a node holding a symbol, its two letter starts' scores: the letter gap's, then the
        // word gap's, the start score plus ln(1/62) + ln(1 - epsilon), less the kind's cost, plus ln P_kind(s, tau).
        std::size_t starts = 0;
    };

    const auto keying = signals::keying("PARIS PARIS", 25.0, 0.5);
    struct Case {
        std::string name;
        std::vector<std::complex<double>> pieces;
    };
    const std::vector<Case> cases = {{"station", station(keying, 10.0, keying.back().second + 1.0, 22)},
                                     {"noise", noise(4.0, 23)}};
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        std::array<int, kKinds> checked{};
        std::map<ElementKey, Element> elements;
        std::vector<PreviousMark> marks;
        std::vector<PreviousGap> gaps;
        std::vector<double> mark_ends;
        std::vector<double> letter_starts;
        std::optional<double> no_signal = 0.0;  // the no-signal story's score after the previous piece, nats
        Carrier carrier{carrier_powers(c.pieces, block), carrier_powers(c.pieces, block, false)};
        Search search(config, sum_noise(), flat_prior());
        std::size_t lines_before = search.stats().lines.size();  // line 0, the channel's first story
        for (std::size_t given = 0; given < c.pieces.size(); ++given) {
            search.step(c.pieces[given], piece_time(given), kN0);
            if (given < kLookahead) continue;  // nothing searched yet
            const std::size_t k = given - kLookahead;  // the piece searched
            const double t = piece_time(k);
            const double offset = search.normalization_nats();
            const std::vector<StoryView> stories = search.live_stories();

            std::optional<double> no_signal_now;  // this piece's no-signal story, for the next piece
            for (const StoryView& x : stories)
                if (x.activity == Activity::NoSignal) no_signal_now = x.score_nats + offset;

            // A sample: the stories of one piece in three are checked (an element is advanced, when its story is
            // a parent at a checked piece, from wherever it stands).
            if (k % 3 == 0) {
                // The parents' terms at this piece, and indexes: marks by the child their end reaches (at a known
                // speed, by child and speed), gaps by state and, at nodes holding a symbol, by speed; and the piece's
                // best letter start, the rescue's.
                mark_ends.clear();
                std::map<std::tuple<int, int>, std::vector<std::pair<std::size_t, int>>> known_marks;
                std::map<int, std::vector<std::pair<std::size_t, int>>> open_marks;
                for (std::size_t i = 0; i < marks.size(); ++i) {
                    PreviousMark& m = marks[i];
                    m.with_block =
                        m.element->start_score_nats + advance_to(*m.element, k, c.pieces, block, config, carrier);
                    m.ends = mark_ends.size();
                    const int tau = m.view.tau + 1;
                    if (m.view.speed >= 0) {
                        for (int label = 0; label < 2; ++label)
                            mark_ends.push_back(fist.log_end(mark_kind(label), m.view.speed, tau));
                    } else {
                        std::array<double, 2> best = {-kInf, -kInf};
                        for (int label = 0; label < 2; ++label) {
                            for (int s = 0; s < speeds; ++s) {
                                mark_ends.push_back(fist.log_end(mark_kind(label), s, tau));
                                best[static_cast<std::size_t>(label)] =
                                    std::max(best[static_cast<std::size_t>(label)], log_speed_prior + mark_ends.back());
                            }
                        }
                        mark_ends.insert(mark_ends.end(), best.begin(), best.end());
                    }
                    for (int label = 0; label < 2; ++label) {
                        const int child = tree.child(m.view.node, label == 1);
                        if (child < 0) continue;
                        if (m.view.speed >= 0)
                            known_marks[{child, m.view.speed}].emplace_back(i, label);
                        else
                            open_marks[child].emplace_back(i, label);
                    }
                }
                letter_starts.clear();
                std::map<std::tuple<int, int>, std::vector<std::size_t>> gaps_by_state;  // node, speed
                std::vector<std::vector<std::size_t>> symbol_gaps_by_speed(static_cast<std::size_t>(speeds));
                double best_letter_start = -kInf;
                for (std::size_t i = 0; i < gaps.size(); ++i) {
                    PreviousGap& g = gaps[i];
                    gaps_by_state[{g.view.node, g.view.speed}].push_back(i);
                    if (tree.symbol(g.view.node).empty()) continue;
                    symbol_gaps_by_speed[static_cast<std::size_t>(g.view.speed)].push_back(i);
                    g.starts = letter_starts.size();
                    const int tau = g.view.tau + 1;
                    for (const auto [kind, cost] :
                         {std::pair{Kind::LetterGap, letter_gap_cost}, std::pair{Kind::WordGap, word_gap_cost}}) {
                        letter_starts.push_back(g.start + log_letter + log_keep - cost +
                                                fist.log_end(kind, g.view.speed, tau));
                        best_letter_start = std::max(best_letter_start, letter_starts.back());
                    }
                }
                const double birth = no_signal ? *no_signal + log_stay_silent + log_over_start + log_letter : -kInf;
                const double rescue = best_letter_start - log_keep + log_restart;

                for (const StoryView& x : stories) {
                    const double own = x.score_nats + offset;
                    if (x.activity == Activity::Morse && x.tau != 0) continue;
                    Best best;
                    if (x.activity == Activity::NoSignal) {
                        if (no_signal) best.offer(*no_signal + log_stay_silent, kSilence, Level{});
                        for (const PreviousMark& m : marks) {
                            for (int label = 0; label < 2; ++label) {
                                const int child = tree.child(m.view.node, label == 1);
                                if (child < 0 || tree.symbol(child).empty()) continue;
                                const double log_end =
                                    m.view.speed >= 0
                                        ? mark_ends[m.ends + static_cast<std::size_t>(label)]
                                        : mark_ends[m.ends + 2 * static_cast<std::size_t>(speeds) +
                                                    static_cast<std::size_t>(label)];
                                best.offer(m.with_block + log_end - over_end_cost, kOverEnd, Level{});
                            }
                        }
                    } else if (!x.key_down) {  // a gap: a mark ended
                        const auto known = known_marks.find({x.node, x.speed});
                        if (known != known_marks.end()) {
                            for (const auto& [i, label] : known->second) {
                                const PreviousMark& m = marks[i];
                                const std::size_t at = m.ends + static_cast<std::size_t>(label);
                                best.offer(m.with_block + mark_ends[at], kMarkEnd, m.element->last_level);
                            }
                        }
                        const auto open = open_marks.find(x.node);
                        if (open != open_marks.end()) {
                            for (const auto& [i, label] : open->second) {
                                const PreviousMark& m = marks[i];
                                const std::size_t at = m.ends + static_cast<std::size_t>(label * speeds + x.speed);
                                best.offer(m.with_block + log_speed_prior + mark_ends[at], kMarkEnd,
                                           m.element->last_level);
                            }
                        }
                    } else if (x.node != root) {  // an element starts
                        const auto parents = gaps_by_state.find({x.node, x.speed});
                        if (parents != gaps_by_state.end()) {
                            for (const std::size_t i : parents->second) {
                                const PreviousGap& g = gaps[i];
                                const double log_end = fist.log_end(Kind::ElementGap, x.speed, g.view.tau + 1);
                                best.offer(g.start + log_end, kElementStart, g.view.level);
                            }
                        }
                    } else if (x.speed >= 0) {  // a letter starts
                        for (const std::size_t i : symbol_gaps_by_speed[static_cast<std::size_t>(x.speed)]) {
                            const PreviousGap& g = gaps[i];
                            best.offer(letter_starts[g.starts], kLetterStart, g.view.level);
                            best.offer(letter_starts[g.starts + 1], kWordStart, g.view.level);
                        }
                    } else {  // the newborn
                        if (no_signal) best.offer(birth, kBirth, Level{});
                        if (best_letter_start > -kInf) best.offer(rescue, kRescue, Level{});
                    }
                    ASSERT_TRUE(std::isfinite(best.score)) << "piece " << k << ": no parent for this story";
                    ASSERT_NEAR(own, best.score, 1e-6)
                        << "piece " << k << ", key down " << x.key_down << ", speed " << x.speed << ", node " << x.node
                        << ", decided by " << names[best.decided];
                    if (x.activity == Activity::Morse) {
                        const auto matches = [&](const Level& level) { return same_level(x.level, level); };
                        ASSERT_TRUE(std::any_of(best.levels.begin(), best.levels.end(), matches))
                            << "piece " << k << ": the level, decided by " << names[best.decided];
                    }
                    ++checked[static_cast<std::size_t>(best.decided)];
                }

                // The newborns born at this piece that were not kept: their lines end here, at their deficit at birth.
                const std::vector<NewbornLine>& lines = search.stats().lines;
                for (std::size_t i = lines_before; i < lines.size(); ++i) {
                    const NewbornLine& line = lines[i];
                    ASSERT_EQ(line.birth_s, t) << "piece " << k;
                    if (!line.end_s || *line.end_s != t) continue;
                    const bool from_silence = line.kind == LineKind::Silence;
                    ASSERT_TRUE(from_silence || line.kind == LineKind::Rescue);
                    ASSERT_NEAR(offset - line.largest_deficit_nats, from_silence ? birth : rescue, 1e-6)
                        << "piece " << k << ", a newborn not kept, " << (from_silence ? "from silence" : "a rescue");
                    ++checked[from_silence ? kBirthNotKept : kRescueNotKept];
                }
            }
            lines_before = search.stats().lines.size();

            // The elements of this piece's live stories (new ones registered), and the parents of the next piece;
            // every 64 pieces, the elements no live story holds are dropped.
            marks.clear();
            gaps.clear();
            std::set<const Element*> held;
            for (const StoryView& x : stories) {
                if (x.activity != Activity::Morse) continue;
                const double own = x.score_nats + offset;
                const ElementKey key{x.key_down, x.speed, x.node, k - static_cast<std::size_t>(x.tau)};
                Element* element = nullptr;
                if (x.tau == 0) {
                    element = &elements.emplace(key, Element{own, x.level, k + 1}).first->second;
                } else {
                    const auto at = elements.find(key);
                    ASSERT_NE(at, elements.end()) << "piece " << k;
                    element = &at->second;
                }
                if (k % 64 == 63) held.insert(element);
                if (x.key_down)
                    marks.push_back(PreviousMark{x, element});
                else
                    gaps.push_back(PreviousGap{x, element->start_score_nats});
            }
            if (k % 64 == 63)
                std::erase_if(elements, [&held](const auto& entry) { return !held.contains(&entry.second); });
            no_signal = no_signal_now;
        }
        for (std::size_t kind = 0; kind < kKinds; ++kind)
            ::testing::Test::RecordProperty(c.name + "_" + names[kind], checked[kind]);
        std::vector<Decided> covered = {kMarkEnd, kElementStart, kLetterStart, kBirth, kRescueNotKept};
        if (c.name == "station") covered.insert(covered.end(), {kWordStart, kOverEnd, kRescue, kBirthNotKept});
        for (const Decided kind : covered) EXPECT_GT(checked[static_cast<std::size_t>(kind)], 50) << names[kind];
    }
}

TEST(Search, NoiseKeepsScoresFinite) {
    // 60 s of DEV2's noise alone at the defaults, where the ceiling binds (design notes, topic 5, item 5): after every
    // step every live score is finite and the best is 0; never more stories than the ceiling (plan §8, item 1). The
    // statistics count every piece, once finish has searched those the look-ahead held.
    ViterbiConfig config;
    config.stats = true;
    // The channelizer passes whole blocks of 32 channel samples: 60.1 s of input holds the first 60 s of pieces.
    auto pieces = noise(60.1, 18);
    ASSERT_GE(pieces.size(), 30000u);
    pieces.resize(30000);
    Search search(config, sum_noise(), flat_prior());
    for (std::size_t k = 0; k < pieces.size(); ++k) {
        search.step(pieces[k], piece_time(k), kN0);
        ASSERT_LE(search.live(), static_cast<std::size_t>(config.ceiling)) << "piece " << k;
        double best = -kInf;
        for (const StoryView& story : search.live_stories()) {
            ASSERT_TRUE(std::isfinite(story.score_nats)) << "piece " << k;
            best = std::max(best, story.score_nats);
        }
        ASSERT_EQ(best, 0.0) << "piece " << k;
    }
    EXPECT_EQ(search.stats().pieces, 30000 - static_cast<std::int64_t>(kLookahead));
    search.finish();
    EXPECT_TRUE(std::isfinite(search.normalization_nats()));
    const SearchStats& stats = search.stats();
    EXPECT_EQ(stats.pieces, 30000);
    std::int64_t counted = 0;
    for (const std::int64_t pieces_in_bin : stats.live_histogram) counted += pieces_in_bin;
    EXPECT_EQ(counted, 30000);
    EXPECT_EQ(stats.live_mean_100ms.size(), 600u);
    EXPECT_EQ(stats.live_max_100ms.size(), 600u);
    EXPECT_GT(stats.ceiling_pieces, 0);
}

TEST(Search, IsDeterministic) {
    // Two runs over the same pieces give the same text, with the same times, and the same statistics, to the bit: a
    // weak station (+3 dB S500) after a second of noise, so that the population is full.
    ViterbiConfig config;
    config.stats = true;
    const auto keying = signals::keying("PARIS", 25.0, 1.0);
    const auto pieces = station(keying, 3.0, keying.back().second + 1.0, 19);
    Search first(config, sum_noise(), flat_prior());
    Search second(config, sum_noise(), flat_prior());
    search_all(first, pieces);
    search_all(second, pieces);
    const auto a = first.best_text();
    const auto b = second.best_text();
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].text, b[i].text);
        EXPECT_EQ(a[i].start_s, b[i].start_s);
        EXPECT_EQ(a[i].end_s, b[i].end_s);
    }
    EXPECT_TRUE(first.stats() == second.stats());
    EXPECT_GT(first.stats().lines.size(), 100u);
    EXPECT_EQ(first.normalization_nats(), second.normalization_nats());
}

TEST(Search, SkippingSuccessorsBeyondTheBeamChangesNothing) {
    // A successor more than beam_nats behind a score known at its piece is not created (plan §2.7, item 5): with and
    // without that skip, the same text, live stories, scores and statistics, on noise alone (the ceiling binding) and
    // on a station at +10 dB S500. The skipping run skipped many times (detail::SearchOptions' counter), the other
    // never.
    ViterbiConfig config;
    config.stats = true;
    const auto keying = signals::keying("PARIS", 25.0, 0.8);
    struct Case {
        std::string name;
        std::vector<std::complex<double>> pieces;
        std::string text;
    };
    const std::vector<Case> cases = {{"noise", noise(4.0, 20), ""},
                                     {"station", station(keying, 10.0, keying.back().second + 1.0, 21), "PARIS"}};
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        std::int64_t skipped = 0;
        std::int64_t skipped_without = 0;
        detail::SearchOptions skip;
        skip.skipped_successors = &skipped;
        detail::SearchOptions create;
        create.skip_successors_beyond_beam = false;
        create.skipped_successors = &skipped_without;
        Search skipping(config, sum_noise(), flat_prior(), skip);
        Search creating(config, sum_noise(), flat_prior(), create);
        std::size_t most = 0;
        for (std::size_t k = 0; k < c.pieces.size(); ++k) {
            skipping.step(c.pieces[k], piece_time(k), kN0);
            creating.step(c.pieces[k], piece_time(k), kN0);
            ASSERT_EQ(skipping.live(), creating.live()) << "piece " << k;
            most = std::max(most, skipping.live());
        }
        skipping.finish();
        creating.finish();
        EXPECT_EQ(text_of(skipping.best_text()), c.text);
        EXPECT_EQ(text_of(creating.best_text()), c.text);
        const auto a = skipping.live_stories();
        const auto b = creating.live_stories();
        ASSERT_EQ(a.size(), b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            EXPECT_EQ(state_of(a[i]), state_of(b[i]));
            EXPECT_EQ(a[i].score_nats, b[i].score_nats);
            EXPECT_EQ(a[i].line, b[i].line);
        }
        EXPECT_TRUE(skipping.stats() == creating.stats());
        // Not a comparison of two empty runs: the population grew, newborns were born at most pieces, and the skip
        // skipped.
        EXPECT_GT(most, 100u);
        EXPECT_GT(skipping.stats().lines.size(), c.pieces.size() / 2);
        ::testing::Test::RecordProperty(c.name + "_skipped_successors", std::to_string(skipped));
        EXPECT_GT(skipped, 1000);
        EXPECT_EQ(skipped_without, 0);
    }
}

TEST(Search, OpenTimingCapChangesNothing) {
    // The open speed's tables (search.hpp, "Work saved") capped at 16 taus, every later maximum of a newborn's first
    // mark computed on demand, against the default cap (7 352 taus at 32 speeds): on 10 s of noise (the ceiling
    // binding, newborns' first marks of every length) and on a station at +10 dB S500, the same live count after every
    // piece and the same stories, scores, lines, statistics and text at the end, with newborns' first marks longer than
    // the small cap among the live stories. The two runs of a case are independent: each runs on its own thread.
    ViterbiConfig config;
    config.stats = true;
    const auto keying = signals::keying("PARIS", 25.0, 0.8);
    struct Case {
        std::string name;
        std::vector<std::complex<double>> pieces;
        std::string text;
    };
    // The channelizer passes whole blocks of 32 channel samples: 0.1 s more input holds the first whole seconds.
    std::vector<std::complex<double>> long_noise = noise(10.1, 18);
    long_noise.resize(5000);
    const std::vector<Case> cases = {{"noise", std::move(long_noise), ""},
                                     {"station", station(keying, 10.0, keying.back().second + 1.0, 21), "PARIS"}};
    sum_noise();  // both built before the threads start
    flat_prior();

    // A run: the live count after each piece, the longest newborn's first mark among the live stories, and the end
    // state as the comparison reads it.
    struct Run {
        std::vector<std::size_t> live;
        int longest_first_mark = 0;  // pieces
        std::vector<StoryView> stories;
        SearchStats stats;
        double normalization_nats = 0.0;
        std::string text;
    };
    const auto run = [&config](const std::vector<std::complex<double>>& pieces, detail::SearchOptions options) {
        Run r;
        Search search(config, sum_noise(), flat_prior(), options);
        for (std::size_t k = 0; k < pieces.size(); ++k) {
            search.step(pieces[k], piece_time(k), kN0);
            r.live.push_back(search.live());
            for (const StoryView& s : search.live_stories())
                if (s.activity == Activity::Morse && s.key_down && s.speed < 0)
                    r.longest_first_mark = std::max(r.longest_first_mark, s.tau);
        }
        search.finish();
        r.stories = search.live_stories();
        r.stats = search.stats();
        r.normalization_nats = search.normalization_nats();
        r.text = text_of(search.best_text());
        return r;
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        detail::SearchOptions small_tables;
        small_tables.open_timing_cap_pieces = 16;
        auto default_run = std::async(std::launch::async, run, std::cref(c.pieces), detail::SearchOptions{});
        auto capped_run = std::async(std::launch::async, run, std::cref(c.pieces), small_tables);
        const Run a = default_run.get();
        const Run b = capped_run.get();
        EXPECT_EQ(a.text, c.text);
        ASSERT_EQ(a.live.size(), b.live.size());
        for (std::size_t k = 0; k < a.live.size(); ++k) ASSERT_EQ(a.live[k], b.live[k]) << "piece " << k;
        ASSERT_EQ(a.stories.size(), b.stories.size());
        for (std::size_t i = 0; i < a.stories.size(); ++i) {
            EXPECT_EQ(state_of(a.stories[i]), state_of(b.stories[i]));
            EXPECT_EQ(a.stories[i].score_nats, b.stories[i].score_nats);
            EXPECT_EQ(a.stories[i].line, b.stories[i].line);
        }
        EXPECT_TRUE(a.stats == b.stats);
        EXPECT_EQ(a.normalization_nats, b.normalization_nats);
        EXPECT_EQ(a.text, b.text);
        // Not a comparison of short marks only: newborns' first marks outlived the small cap, so the capped run
        // computed their maxima on demand.
        ::testing::Test::RecordProperty(c.name + "_longest_first_mark", std::to_string(b.longest_first_mark));
        EXPECT_EQ(a.longest_first_mark, b.longest_first_mark);
        EXPECT_GT(b.longest_first_mark, 16);
    }
}

TEST(Search, RefusesWhatItCannotUse) {
    const ViterbiConfig defaults;
    const std::vector<double>& c = sum_noise();
    EXPECT_NO_THROW(Search(defaults, c, flat_prior()));
    EXPECT_THROW(Search(defaults, c, nullptr), std::invalid_argument);
    // c must cover the longest block, 3 x 195 samples, and be positive and finite from m = 1 on.
    const std::vector<double> short_c(c.begin(), c.end() - 1);
    EXPECT_THROW(Search(defaults, short_c, flat_prior()), std::invalid_argument);
    for (const double bad : {0.0, -1.0, kNaN, kInf}) {
        std::vector<double> broken = c;
        broken[100] = bad;
        EXPECT_THROW(Search(defaults, broken, flat_prior()), std::invalid_argument) << bad;
    }
    // c is read for the noise's long-run rate, its last increment, which must agree with the flat prior's.
    std::vector<double> other_rate = c;
    other_rate.back() += 1.0;  // Hz: a last increment 1 Hz off
    EXPECT_THROW(Search(defaults, other_rate, flat_prior()), std::invalid_argument);
    const auto refuses = [&](auto change) {
        ViterbiConfig config;
        change(config);
        EXPECT_THROW(Search(config, c, flat_prior()), std::invalid_argument);
    };
    refuses([](ViterbiConfig& config) { config.beam_nats = 0.0; });
    refuses([](ViterbiConfig& config) { config.beam_nats = kNaN; });
    refuses([](ViterbiConfig& config) { config.ceiling = 0; });
    refuses([](ViterbiConfig& config) { config.speed_count = 256; });
    refuses([](ViterbiConfig& config) { config.over_rate_per_s = 0.0; });
    refuses([](ViterbiConfig& config) { config.over_rate_per_s = 500.0; });  // P = 1
    refuses([](ViterbiConfig& config) { config.rescue_epsilon = 0.0; });
    refuses([](ViterbiConfig& config) { config.rescue_epsilon = 1.0; });
    refuses([](ViterbiConfig& config) { config.word_end_p = 0.0; });
    refuses([](ViterbiConfig& config) { config.over_end_q = 1.0; });
    refuses([](ViterbiConfig& config) { config.quadrature_order = 1; });
    refuses([](ViterbiConfig& config) { config.quadrature_order = 21; });
    refuses([](ViterbiConfig& config) { config.fading_spread_hz = 1000.0; });  // a block of 0.39 ms: under one piece
    refuses([](ViterbiConfig& config) { config.fading_spread_hz = -1.0; });    // block_length_s refuses it
    refuses([](ViterbiConfig& config) { config.dot_min_ms = 0.0; });           // Fist refuses it
    refuses([](ViterbiConfig& config) { config.systematic_floor = -0.01; });   // a relative amplitude in [0, 1]
    refuses([](ViterbiConfig& config) { config.systematic_floor = 1.01; });
    refuses([](ViterbiConfig& config) { config.systematic_floor = kNaN; });
    EXPECT_NO_THROW([&] {
        ViterbiConfig ends = defaults;
        ends.systematic_floor = 0.0;
        Search none(ends, c, flat_prior());
        ends.systematic_floor = 1.0;
        Search whole(ends, c, flat_prior());
    }());

    // A refused piece changes nothing: neither the stories nor the pieces held for the look-ahead. Three pieces given,
    // the first searched; a time not later than the last piece given, held or not, is refused.
    ViterbiConfig with_stats = defaults;
    with_stats.stats = true;
    Search search(with_stats, c, flat_prior());
    for (std::size_t k = 0; k < 3; ++k) search.step({0.01, 0.0}, piece_time(k), kN0);
    ASSERT_EQ(search.stats().pieces, 1);
    const double offset = search.normalization_nats();
    const std::size_t live = search.live();
    for (const double n0 : {0.0, -kN0, kNaN, kInf})
        EXPECT_THROW(search.step({0.01, 0.0}, piece_time(3), n0), std::invalid_argument) << n0;
    EXPECT_THROW(search.step({0.01, 0.0}, piece_time(2), kN0), std::invalid_argument);  // not later than the last
    EXPECT_THROW(search.step({0.01, 0.0}, kNaN, kN0), std::invalid_argument);
    EXPECT_THROW(search.step({kNaN, 0.0}, piece_time(3), kN0), std::invalid_argument);
    EXPECT_THROW(search.step({0.0, kInf}, piece_time(3), kN0), std::invalid_argument);
    EXPECT_EQ(search.normalization_nats(), offset);
    EXPECT_EQ(search.live(), live);
    EXPECT_EQ(search.stats().pieces, 1);
    EXPECT_NO_THROW(search.step({0.01, 0.0}, piece_time(3), kN0));
    EXPECT_EQ(search.stats().pieces, 2);
    search.finish();
    EXPECT_EQ(search.stats().pieces, 4);
}

TEST(Search, HoldsTheLookAheadsPiecesUntilTheyAreSearched) {
    // The search runs lookahead_pieces() = ceil(4 ms / piece_s) pieces behind the pieces it is given (search.hpp, "The
    // systematic term"): 2 at the default 2 ms, 4 at 1 ms, 2 at 3 ms and 1 at 5 ms (without fading, so that the blocks
    // fit c at every piece). After k pieces given, max(k - 2, 0) have been searched (the statistics count them);
    // finish searches the rest, a second finish nothing, and a piece given after it is held again. The text after
    // finish is the text of the pieces given ("E" at 25 WPM and +20 dB S500, then 100 ms of noise).
    for (const auto& [piece_s, held] : {std::pair{0.002, 2}, std::pair{0.001, 4}, std::pair{0.003, 2},
                                        std::pair{0.005, 1}}) {
        ViterbiConfig config;
        config.piece_s = piece_s;
        config.fading_spread_hz = 0.0;
        EXPECT_EQ(Search(config, sum_noise(), flat_prior()).lookahead_pieces(), held) << piece_s << " s";
    }
    ViterbiConfig config;
    config.stats = true;
    const auto keying = signals::keying("E", 25.0, 0.5);
    auto pieces = station(keying, 20.0, keying.back().second + 0.2, 25);
    // The pieces to 100 ms after the mark's end (0.548 s, the end of the 274th piece).
    ASSERT_GE(pieces.size(), 324u);
    pieces.resize(324);
    Search search(config, sum_noise(), flat_prior());
    for (std::size_t k = 0; k < pieces.size(); ++k) {
        search.step(pieces[k], piece_time(k), kN0);
        ASSERT_EQ(search.stats().pieces, static_cast<std::int64_t>(k + 1 >= 2 ? k + 1 - 2 : 0)) << "piece " << k;
    }
    search.finish();
    EXPECT_EQ(search.stats().pieces, 324);
    const double offset = search.normalization_nats();
    search.finish();
    EXPECT_EQ(search.stats().pieces, 324);
    EXPECT_EQ(search.normalization_nats(), offset);
    EXPECT_EQ(text_of(search.best_text()), "E");
    search.step(pieces[0], piece_time(324), kN0);
    EXPECT_EQ(search.stats().pieces, 324);
    search.finish();
    EXPECT_EQ(search.stats().pieces, 325);
}
