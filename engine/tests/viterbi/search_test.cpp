#include "kz4ap/viterbi/search.hpp"

#include "kz4ap/channelizer.hpp"
#include "kz4ap/decoder.hpp"
#include "kz4ap/types.hpp"
#include "kz4ap/viterbi/amplitude.hpp"
#include "kz4ap/viterbi/config.hpp"
#include "kz4ap/viterbi/fist.hpp"
#include "kz4ap/viterbi/front_filter.hpp"
#include "kz4ap/viterbi/mark_template.hpp"
#include "kz4ap/viterbi/morse_tree.hpp"
#include "kz4ap/viterbi/newborn.hpp"
#include "kz4ap/viterbi/observation.hpp"
#include "kz4ap/viterbi/symbol_prior.hpp"
#include "kz4ap/viterbi/systematic.hpp"
#include "kz4ap/viterbi_decoder.hpp"
#include "keyed_signals.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
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
constexpr int kSpacing = 4;                           // channel samples per envelope sample
constexpr double kDelta = kSpacing / kChannelRate;    // s: the sample interval, 2.667 ms
constexpr int kChannelBin = 32;                       // the channel's center, 750 Hz: the stations sit on it

// The engine's channelizer at 48 kHz: an FFT of 2 048 points, 1 500 channel samples/s.
kz4ap::ChannelizerConfig channelizer_config() {
    kz4ap::ChannelizerConfig config;
    config.sample_rate = kInputRate;
    config.fft_size = 2048;
    return config;
}

// The default configuration's shared parts on the 48 kHz channelizer, built once.
const std::shared_ptr<const kz4ap::ViterbiShared>& shared() {
    static const auto parts = kz4ap::make_viterbi_shared(ViterbiConfig{}, channelizer_config());
    return parts;
}

// The station's frequency, Hz: the center of channel bin 32 (23.4375 Hz per bin at 48 kHz).
double station_hz() { return kChannelBin * static_cast<double>(kInputRate) / channelizer_config().fft_size; }

// The amplitude, FS, of a key-down carrier at s500_db dB S500 over DEV2's noise: S500 = 10 log10(a^2 / (N0 x 500 Hz)).
double amplitude_at(double s500_db) { return std::sqrt(std::pow(10.0, s500_db / 10.0) * kN0 * 500.0); }

// The envelope samples of a 48 kHz input: through the channelizer, on the channel centered on the station, and the
// front filter read every 4th channel sample (EnvelopeSampler), sample k at absolute channel index 4 k.
std::vector<double> samples_of(const std::vector<kz4ap::Sample>& input) {
    kz4ap::Channelizer channelizer(channelizer_config());
    channelizer.add_channel(1, kChannelBin);
    EnvelopeSampler sampler(shared()->front_taps(), kSpacing);
    std::vector<EnvelopeSample> out;
    channelizer.push(input, [&](std::uint32_t, std::uint64_t first, std::span<const kz4ap::Sample> samples) {
        sampler.push(samples, first, out);
    });
    std::vector<double> e;
    e.reserve(out.size());
    for (const EnvelopeSample& s : out) {
        EXPECT_EQ(s.k, static_cast<std::int64_t>(e.size()));
        e.push_back(s.e_fs);
    }
    return e;
}

// A station keyed by the given intervals with D17's edges at s500_db dB S500 on DEV2's noise, as envelope samples.
std::vector<double> station(const std::vector<std::pair<double, double>>& keying, double s500_db, double duration_s,
                            unsigned seed) {
    return samples_of(signals::keyed_signal_d17(keying, kInputRate, duration_s, station_hz(), amplitude_at(s500_db),
                                                kSigma, seed));
}

// DEV2's noise alone, as envelope samples.
std::vector<double> noise(double duration_s, unsigned seed) {
    return samples_of(signals::keyed_signal_d17({}, kInputRate, duration_s, station_hz(), 0.0, kSigma, seed));
}

// The time of sample k, s: 4 k / r, one expression, never a sum of increments.
double sample_time(std::size_t k) { return static_cast<double>(kSpacing * k) / kChannelRate; }

// Every sample given, then those the look-ahead held searched.
void search_all(Search& search, const std::vector<double>& samples) {
    for (std::size_t k = 0; k < samples.size(); ++k) search.step(samples[k], sample_time(k), kN0);
    search.finish();
}

std::string text_of(const std::vector<kz4ap::DecodedSymbol>& symbols) {
    std::string text;
    for (const kz4ap::DecodedSymbol& symbol : symbols) text += symbol.text;
    return text;
}

std::string decode(const ViterbiConfig& config, const std::vector<double>& samples) {
    Search search(config, shared());
    search_all(search, samples);
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

// The reference's timing of a story (search.hpp, "Durations"): a mark's at a known speed (its node's labels), a newborn's
// first mark's (speed open), a gap's; 0 at tau = 0.
struct Timing {
    ViterbiConfig config;
    Fist fist{config};
    MorseTree tree;
    std::vector<double> log_prior = on_air_log_prior(tree, config.symbol_prior_floor);
    // What a symbol's letter still pays when it ends, the flat ln(1/62) having been paid when it started.
    double letter_end(int node) const { return log_prior[static_cast<std::size_t>(node)] + std::log(62.0); }
    double of(const StoryView& s) const {
        if (s.activity != Activity::Morse || s.tau == 0) return 0.0;
        double best = -kInf;
        if (s.key_down) {
            for (int label = 0; label < 2; ++label) {
                const Kind kind = label == 1 ? Kind::Dash : Kind::Dot;
                if (s.speed < 0) {
                    if (tree.child(tree.root(), label == 1) < 0) continue;
                    for (int v = 0; v < config.speed_count; ++v) best = std::max(best, fist.log_survival(kind, v, s.tau));
                } else if (tree.child(s.node, label == 1) >= 0) {
                    best = std::max(best, fist.log_survival(kind, s.speed, s.tau));
                }
            }
            if (s.speed < 0) best += -std::log(static_cast<double>(config.speed_count));
            return best;
        }
        const double letter_cost = -std::log1p(-config.word_end_p);
        const double word_cost = -std::log(config.word_end_p * (1.0 - config.over_end_q));
        if (tree.has_children(s.node)) best = fist.log_survival(Kind::ElementGap, s.speed, s.tau);
        if (!tree.symbol(s.node).empty()) {
            best = std::max(best, fist.log_survival(Kind::LetterGap, s.speed, s.tau) - letter_cost + letter_end(s.node));
            best = std::max(best, fist.log_survival(Kind::WordGap, s.speed, s.tau) - word_cost + letter_end(s.node));
        }
        return best;
    }
};

// The score of sample k for a story whose view after sample k - 1 was `before` (not in the newborn integral),
// recomputed from the library's parts (search.hpp, "One sample k searched"): its edges advanced (an edge claimed at its
// element's first sample, tau = 1), its template and edge term, the amplitude predicted to k with the story's Omega_bar
// and the variance added at lag 1 multiplied by its level's m, V_k from that prediction, the shared reference and the constant w, the quadrature started from the previous sample's
// mode. 0 where s = 0.
double reference_sample_score(const StoryView& before, bool claims, std::int64_t k, double e) {
    const ViterbiConfig config{};
    EdgeHistory edges = before.edges;
    advance(edges, claims);
    const TemplateValue tv = template_value(edges, shared()->template_tables());
    if (tv.s == 0.0) return 0.0;
    const double sigma2 = kN0 * shared()->output_noise().sigma2_per_n0_hz;
    static const FadingTables fading(config.fading_spread_hz, config.sample_interval_s);
    const Prediction predicted =
        predict_amplitude_at_level(before.amplitude, k, before.omega_bar_fs2, sigma2, config.added_variance_multiplier_max,
                                   config.added_variance_level_knee, fading);
    const double variance = systematic_variance_fs2(sigma2, predicted.mu_fs * predicted.mu_fs + predicted.p_fs2, tv.s,
                                                    tv.v, config.systematic_floor);
    const double w = shared()->output_noise().w_strong;
    const SampleEvidence plain = sample_evidence(e, tv.s, predicted.mu_fs, predicted.p_fs2, variance, w,
                                                 config.quadrature_order, before.mode_a_fs);
    return plain.score_nats + w * (e * e / sigma2 - e * e / variance - std::log(variance / sigma2));
}

// Whether the search's full-size tests run: the environment variable KZ4AP_SEARCH_FULL_SWEEP=1, which the full-sweep
// presets set through the test Search.FullSweep (engine/CMakeLists.txt).
bool full_sweep() {
    const char* value = std::getenv("KZ4AP_SEARCH_FULL_SWEEP");
    return value != nullptr && std::string(value) == "1";
}

}  // namespace

TEST(Search, DecodesACleanStationAtThreeSpeeds) {
    // "PARIS PARIS" at 12, 25 and 40 WPM, machine keying (exact PARIS timing) with D17's 2.5 ms edges starting at
    // key-down and key-up, +20 dB S500 on DEV2's noise (sigma = 0.02 FS at 48 kHz): the exact text (plan 1b, §5, item 2).
    for (const double wpm : {12.0, 25.0, 40.0}) {
        SCOPED_TRACE(std::to_string(wpm) + " WPM");
        const auto keying = signals::keying("PARIS PARIS", wpm, 0.5);
        EXPECT_EQ(decode(ViterbiConfig{}, station(keying, 20.0, keying.back().second + 1.5, 11)), "PARIS PARIS");
    }
}

TEST(Search, FollowsASpeedStep) {
    // A step from 20 to 30 WPM in mid-text, at +20 dB S500: a CER of at most 0.05 over the text (plan 1b, §5, item 2).
    const auto keying = signals::keying_segments({{"PARIS PARIS PARIS", 20.0}, {"PARIS PARIS PARIS", 30.0}}, 0.5);
    const std::string decoded = decode(ViterbiConfig{}, station(keying, 20.0, keying.back().second + 1.5, 12));
    EXPECT_LE(cer("PARIS PARIS PARIS PARIS PARIS PARIS", decoded), 0.05) << "decoded \"" << decoded << "\"";
}

TEST(Search, FollowsASpeedStepAtEverySnr) {
    // The full sweep, labeled full-sweep (the presets windows-full-sweep and linux-full-sweep run it, through the test
    // Search.FullSweep of engine/CMakeLists.txt, which sets KZ4AP_SEARCH_FULL_SWEEP=1; skipped otherwise): the step
    // from 20 to 30 WPM at +10, +11, ... +28 dB S500 on seeds 12, 1 and 2, a CER of at most 0.05 at every point. The
    // test noise is std::normal_distribution's, whose algorithm is the library's, so under another library the draws
    // differ.
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
}

TEST(Search, FarnsworthTextIsRight) {
    // Characters at 18 WPM sent at 5 WPM overall (F = 7.84), at +20 dB S500: the plan's criterion is the exact text. A
    // strict expected failure since the owner's decision D14 of 2026-10-21: the decoder has no Farnsworth factor
    // (standard spacing only; docs/backlog.md, "Farnsworth spacing in the new decoder"). The test fails if the text is
    // exact, so that the marker is removed once the fix comes, and is skipped otherwise, with the decoded text.
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
    // exact text, with no space before it (plan 1b, §5, item 2).
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
    const std::vector<Ranked> reversed = {{5, -1.0, 9}, {5, -1.0, 2}};
    EXPECT_EQ(detail::merge(reversed), (std::vector<std::size_t>{1}));

    // In the search (Ruling 64: merging on the packed state and the last 12 samples' edges): after every sample searched,
    // no two successors created at it share a state and those edges, unless one is a rescue, and there is at most one
    // quiet no-signal story. Stories of one state with different edges do live side by side. At each sample where a
    // newborn is born (a mark at the root, speed open, tau = 0), the newborn from silence and a rescue newborn each
    // have their own key: the newborn from silence stands at the continuing no-signal story's score plus ln(1 - P), ln P
    // and ln(1/62) (at least: a birth comes from the best no-signal story, which may still be in a tail), and a rescue
    // lives beside it at samples where both survive.
    ViterbiConfig config;
    config.stats = true;
    const double over_start = config.over_rate_per_s * config.sample_interval_s;
    const double log_letter = -std::log(62.0);
    const auto keying = signals::keying("PARIS", 25.0, 0.6);
    const auto samples = station(keying, 5.0, keying.back().second + 1.0, 15);
    Search search(config, shared());
    std::optional<double> no_signal_before;  // the quiet no-signal story's own score after the previous sample, nats
    int rescues_kept = 0;
    int births_kept = 0;
    int both_kept = 0;
    int same_state_other_edges = 0;
    for (std::size_t given = 0; given < samples.size(); ++given) {
        search.step(samples[given], sample_time(given), kN0);
        const std::size_t k = given;  // searched at once (D23)
        const double offset = search.normalization_nats();
        const std::vector<StoryView> stories = search.live_stories();
        int no_signal = 0;
        std::optional<double> no_signal_now;
        std::set<std::tuple<std::tuple<Activity, bool, int, int, int>, int, bool>> created;  // state, edges, rescue
        std::set<std::tuple<Activity, bool, int, int, int>> created_states;
        int rescues_now = 0;
        int births_now = 0;
        for (std::size_t i = 0; i < stories.size(); ++i) {
            const StoryView& s = stories[i];
            const bool just_created = s.activity == Activity::Morse ? s.tau == 0 : s.tau == 1;
            if (just_created) {
                const bool rescue = search.stats().lines.at(static_cast<std::size_t>(s.line)).kind == LineKind::Rescue &&
                                    s.speed < 0 && s.activity == Activity::Morse;
                ASSERT_TRUE(created.emplace(state_of(s), s.edges.recent & 0x0FFF, rescue).second) << "sample " << k;
                if (!created_states.insert(state_of(s)).second && !rescue) ++same_state_other_edges;
            }
            if (s.activity == Activity::NoSignal && s.tau == 0) {
                ++no_signal;
                // Only a no-signal story whose over's tail has ended (no edges, so it scores nothing).
                if (s.edges.recent == 0 && !s.edges.key_down_before) no_signal_now = s.score_nats + offset;
            }
            if (s.activity == Activity::Morse && s.key_down && s.tau == 0 && s.speed < 0 && s.node == 0 &&
                no_signal_before && no_signal_now) {
                const double from_silence =
                    *no_signal_before + std::log1p(-over_start) + std::log(over_start) + log_letter;
                const double kept = s.score_nats + offset;
                const LineKind kind = search.stats().lines.at(static_cast<std::size_t>(s.line)).kind;
                if (kind == LineKind::Rescue) {
                    ++rescues_now;
                } else {
                    EXPECT_EQ(kind, LineKind::Silence) << "sample " << k;
                    EXPECT_GE(kept, from_silence - 1e-6) << "sample " << k;
                    ++births_now;
                }
            }
        }
        ASSERT_LE(no_signal, 1) << "sample " << k;
        ASSERT_LE(rescues_now, 1) << "sample " << k;
        ASSERT_LE(births_now, 1) << "sample " << k;
        rescues_kept += rescues_now;
        births_kept += births_now;
        if (rescues_now > 0 && births_now > 0) ++both_kept;
        no_signal_before = no_signal_now;
    }
    EXPECT_GT(rescues_kept, 0);
    EXPECT_GT(births_kept, 100);
    EXPECT_GT(both_kept, 0);
    EXPECT_GT(same_state_other_edges, 100);
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

    // In the search, at a ceiling of 10 stories on a station at +5 dB S500 after a second of noise: after every sample
    // searched at most 10 stories live, every one within the beam of the best, the best at 0; the ceiling decided at
    // some samples.
    ViterbiConfig config;
    config.ceiling = 10;
    config.stats = true;
    const auto keying = signals::keying("PARIS", 25.0, 1.0);
    const auto samples = station(keying, 5.0, keying.back().second + 1.0, 16);
    Search search(config, shared());
    for (std::size_t given = 0; given < samples.size(); ++given) {
        search.step(samples[given], sample_time(given), kN0);
        const std::size_t k = given;  // searched at once (D23)
        ASSERT_LE(search.live(), 10u) << "sample " << k;
        double best = -kInf;
        for (const StoryView& story : search.live_stories()) {
            ASSERT_GE(story.score_nats, -config.beam_nats) << "sample " << k;
            best = std::max(best, story.score_nats);
        }
        ASSERT_EQ(best, 0.0) << "sample " << k;
    }
    EXPECT_GT(search.stats().ceiling_samples, 0);
}

TEST(Search, BooksEveryPointAtEverySample) {
    // Every story's score, at every sample, holds exactly what its story has earned so far (design notes, topic 5,
    // item 9): from one sample to the next, a story that continues its element (a mark or a gap at tau >= 1, at a known
    // speed, out of the newborn integral) gains exactly its sample's score, recomputed here from the library's parts
    // (reference_sample_score), plus the change of its element's timing, within 1e-9 nats. "TT" at 25 WPM, +20 dB S500.
    const Timing timing;
    const auto keying = signals::keying("TT", 25.0, 0.5);
    const auto samples = station(keying, 20.0, keying.back().second + 0.6, 17);
    int plateau = 0;  // checks of marks past their template's rise (tau > 16)
    int tail = 0;     // checks of gaps within 16 samples of their key-up whose sample scored
    int quiet = 0;    // checks of gaps beyond their tail, where a sample adds exactly 0
    Search search(ViterbiConfig{}, shared());
    std::map<std::uint64_t, StoryView> before;  // a story's creation count, the same while it continues -> view
    for (std::size_t k = 0; k < samples.size(); ++k) {
        search.step(samples[k], sample_time(k), kN0);
        std::map<std::uint64_t, StoryView> now;
        const double offset = search.normalization_nats();
        for (StoryView s : search.live_stories()) {
            s.score_nats += offset;  // its own score, against "noise only"
            if (s.activity == Activity::Morse) now.emplace(s.creation, s);
            if (s.activity != Activity::Morse || s.tau == 0 || s.speed < 0 || s.in_newborn_integral) continue;
            const auto at = before.find(s.creation);
            if (at == before.end() || at->second.in_newborn_integral) continue;
            const StoryView& b = at->second;
            ASSERT_EQ(s.tau, b.tau + 1) << "sample " << k;
            const double sample = reference_sample_score(b, b.tau == 0, static_cast<std::int64_t>(k), samples[k]);
            const double expected = sample + timing.of(s) - timing.of(b);
            ASSERT_NEAR(s.score_nats - b.score_nats, expected, 1e-9)
                << "sample " << k << ", key down " << s.key_down << ", tau " << s.tau << ", speed " << s.speed
                << ", node " << s.node;
            if (s.key_down && s.tau > 16) ++plateau;
            if (!s.key_down && s.tau <= 16 && sample != 0.0) ++tail;
            if (!s.key_down && s.tau > 16) {
                ++quiet;
                EXPECT_EQ(sample, 0.0);
            }
        }
        before = std::move(now);
    }
    EXPECT_GT(plateau, 100);
    EXPECT_GT(tail, 100);
    EXPECT_GT(quiet, 100);
}

TEST(Search, TheTailIsScoredByTheStoryThatKeepsTheMark) {
    // After a key-up the gap story scores the fall's tail (part 5): at j = 0 ... 15 samples after its claimed key-up
    // (tau = 1 ... 16) its amplitude is updated (t_m = k) at the samples whose |s| is at least
    // amplitude_update_min_template and left alone at the others, whose |s| carries nothing on it, and at j = 16 (tau =
    // 17) and beyond a sample adds exactly 0, the amplitude and the long-run sums unchanged. A no-signal story after an
    // over's end does the same, and once its tail has ended its level is restarted (no amplitude, no sums). "E" at 25
    // WPM, +20 dB S500, then silence.
    const auto keying = signals::keying("E", 25.0, 0.5);
    const auto samples = station(keying, 20.0, keying.back().second + 0.8, 18);
    const double min_s = ViterbiConfig{}.amplitude_update_min_template;
    Search search(ViterbiConfig{}, shared());
    std::map<std::uint64_t, StoryView> before;  // a story's creation count, the same while it continues -> view
    int tail_updates = 0;
    int tail_holds = 0;
    int quiet_samples = 0;
    int no_signal_tail = 0;
    int no_signal_restarted = 0;
    for (std::size_t given = 0; given < samples.size(); ++given) {
        search.step(samples[given], sample_time(given), kN0);
        const auto k = static_cast<std::int64_t>(given);
        std::map<std::uint64_t, StoryView> now;
        for (const StoryView& s : search.live_stories()) {
            const int phase = s.activity == Activity::NoSignal ? 0 : (s.key_down ? 1 : 2);
            now.emplace(s.creation, s);
            if (s.in_newborn_integral) continue;
            const double template_now = template_value(s.edges, shared()->template_tables()).s;
            if (phase == 2 && s.tau >= 1) {
                const auto at = before.find(s.creation);
                if (at == before.end() || at->second.in_newborn_integral) continue;
                if (s.tau <= 16) {
                    if (std::fabs(template_now) >= min_s) {
                        EXPECT_EQ(s.amplitude.t_m_sample, k) << "tau " << s.tau;
                        ++tail_updates;
                    } else {
                        EXPECT_EQ(s.amplitude.t_m_sample, at->second.amplitude.t_m_sample) << "tau " << s.tau;
                        EXPECT_EQ(s.amplitude.mu_fs, at->second.amplitude.mu_fs) << "tau " << s.tau;
                        ++tail_holds;
                    }
                } else {
                    EXPECT_EQ(s.amplitude.t_m_sample, at->second.amplitude.t_m_sample);
                    EXPECT_EQ(s.amplitude.mu_fs, at->second.amplitude.mu_fs);
                    EXPECT_EQ(s.amplitude.p_fs2, at->second.amplitude.p_fs2);
                    EXPECT_EQ(s.nu, at->second.nu);
                    EXPECT_EQ(s.omega_hat_fs2, at->second.omega_hat_fs2);
                    ++quiet_samples;
                }
            }
            if (phase == 0) {
                const bool in_tail = s.edges.recent != 0 || s.edges.key_down_before;
                if (in_tail && s.amplitude.mu_fs > 0.0) {
                    if (std::fabs(template_now) >= min_s) EXPECT_EQ(s.amplitude.t_m_sample, k);
                    ++no_signal_tail;
                } else if (!in_tail) {
                    EXPECT_EQ(s.amplitude.mu_fs, 0.0);
                    EXPECT_EQ(s.nu, 1.0);
                    EXPECT_EQ(s.omega_hat_fs2, 0.0);
                    if (no_signal_tail > 0) ++no_signal_restarted;
                }
            }
        }
        before = std::move(now);
    }
    EXPECT_GT(tail_updates, 100);
    EXPECT_GT(tail_holds, 100);
    EXPECT_GT(quiet_samples, 100);
    EXPECT_GE(no_signal_tail, 16);
    EXPECT_GT(no_signal_restarted, 0);
    // A lone mark is E at its speed or T at a third of its dot: the two readings tie.
    EXPECT_TRUE(text_of(search.best_text()) == "E" || text_of(search.best_text()) == "T");
}

TEST(Search, SumsRestartAtAnOversEndAndNotAtARescue) {
    // Part 7: a newborn from silence starts with a new newborn integral and empty long-run sums (nu = 1, Omega_hat = 0, no
    // amplitude) at the sample it is created. A rescue opens the speed only: it stands 6.908 nats behind the best
    // letter-starting successor of its sample and keeps that story's edges, amplitude and sums (decision log, 2026-10-30).
    // After an over's end the no-signal story keeps the over's level through its tail and has none after it
    // (TheTailIsScoredByTheStoryThatKeepsTheMark). "PARIS PARIS" at 25 WPM, +15 dB S500.
    ViterbiConfig config;
    config.stats = true;
    const double rescue_offset = std::log(config.rescue_epsilon) - std::log1p(-config.rescue_epsilon);
    const auto keying = signals::keying("PARIS PARIS", 25.0, 0.5);
    const auto samples = station(keying, 15.0, keying.back().second + 1.0, 19);
    Search search(config, shared());
    int births = 0;
    int rescues = 0;
    int rescues_with_level = 0;
    for (std::size_t given = 0; given < samples.size(); ++given) {
        search.step(samples[given], sample_time(given), kN0);
        const std::vector<StoryView> stories = search.live_stories();
        std::optional<StoryView> best_letter_start;
        for (const StoryView& s : stories)
            if (s.activity == Activity::Morse && s.key_down && s.tau == 0 && s.speed >= 0 && s.node == 0 &&
                (!best_letter_start || s.score_nats > best_letter_start->score_nats))
                best_letter_start = s;
        for (const StoryView& s : stories) {
            if (!(s.activity == Activity::Morse && s.key_down && s.tau == 0 && s.speed < 0)) continue;
            const LineKind kind = search.stats().lines.at(static_cast<std::size_t>(s.line)).kind;
            if (kind == LineKind::Silence) {
                ++births;
                EXPECT_TRUE(s.in_newborn_integral);
                EXPECT_EQ(s.nu, 1.0);
                EXPECT_EQ(s.omega_hat_fs2, 0.0);
                EXPECT_EQ(s.amplitude.mu_fs, 0.0);
                EXPECT_EQ(s.omega_bar_fs2, 0.0);
            }
            if (kind != LineKind::Rescue) continue;
            ++rescues;
            if (best_letter_start && std::fabs(s.score_nats - (best_letter_start->score_nats + rescue_offset)) < 1e-9) {
                EXPECT_EQ(s.edges.recent, best_letter_start->edges.recent);
                EXPECT_EQ(s.edges.key_down_before, best_letter_start->edges.key_down_before);
                EXPECT_EQ(s.amplitude.mu_fs, best_letter_start->amplitude.mu_fs);
                EXPECT_EQ(s.nu, best_letter_start->nu);
                if (s.amplitude.mu_fs > 0.0) ++rescues_with_level;
            }
        }
    }
    EXPECT_GT(births, 50);
    EXPECT_GT(rescues, 0);
    EXPECT_GT(rescues_with_level, 0);
}

TEST(Search, NewbornGridsCopyOnBranch) {
    // A newborn's integral is shared by the stories descended from it until one of them scores a sample, which takes
    // its own copy (copy on write; plan 1b, Task 9's design moved here): after every sample, the live stories that
    // scored it each hold an integral of their own (the successors created at the sample share their parent's); a
    // newborn's first mark keeps exactly its own samples in its grid, whatever its successors did (its score less its
    // birth score and timing is the ln Z of a NewbornIntegral fed the same samples, within 1e-9 nats); and the copies
    // made at a sample are exactly one fewer than the stories in each group that shared a grid after the sample before
    // (a successor pruned before it scored was never copied). "PARIS" at 25 WPM, +20 dB S500.
    ViterbiConfig config;
    config.stats = true;  // to tell a newborn from silence from a rescue that keeps its parent's integral
    const Timing timing;
    const auto keying = signals::keying("PARIS", 25.0, 0.5);
    const auto samples = station(keying, 20.0, keying.back().second + 0.5, 20);
    const double sigma2 = kN0 * shared()->output_noise().sigma2_per_n0_hz;
    std::int64_t copies = 0;
    detail::SearchOptions options;
    options.newborn_copies = &copies;
    Search search(config, shared(), options);
    // First marks by their birth sample: the reference integral, the birth score and the view before.
    struct FirstMark {
        NewbornIntegral reference;
        double birth_score;
        StoryView before;
    };
    std::map<std::int64_t, FirstMark> first_marks;
    const FadingTables fading(config.fading_spread_hz, config.sample_interval_s);
    const NewbornFade fade{8, 0.5 * (1.0 - fading.rho(1) * fading.rho(1))};
    std::map<std::uintptr_t, int> groups_before;  // shared grids after the previous sample: grid -> stories
    int compared = 0;
    int shared_groups = 0;
    for (std::size_t given = 0; given < samples.size(); ++given) {
        const std::int64_t copies_before = copies;
        search.step(samples[given], sample_time(given), kN0);
        const auto k = static_cast<std::int64_t>(given);
        const double offset = search.normalization_nats();
        const std::vector<StoryView> stories = search.live_stories();
        // The copies at this sample: one per sharing story but the last to score, in each group.
        int expected_copies = 0;
        for (const auto& [grid, count] : groups_before) expected_copies += count - 1;
        EXPECT_EQ(copies - copies_before, expected_copies) << "sample " << k;
        std::map<std::uintptr_t, int> holders;     // every story holding a grid
        std::map<std::uintptr_t, int> scored_now;  // holders that scored this sample: in an element, at tau >= 1
        for (const StoryView& s : stories) {
            if (!s.newborn_grid) continue;
            ++holders[s.newborn_grid];
            if (s.activity == Activity::Morse && s.tau >= 1) ++scored_now[s.newborn_grid];
        }
        for (const auto& [grid, count] : scored_now) EXPECT_EQ(count, 1) << "sample " << k << ": a grid scored twice";
        groups_before.clear();
        for (const auto& [grid, count] : holders)
            if (count > 1) {
                groups_before[grid] = count;
                ++shared_groups;
            }
        // The first marks: born now (tau 0), or continued.
        std::map<std::int64_t, FirstMark> next;
        for (const StoryView& s : stories) {
            if (!(s.activity == Activity::Morse && s.key_down && s.speed < 0 && s.in_newborn_integral)) continue;
            if (search.stats().lines.at(static_cast<std::size_t>(s.line)).kind != LineKind::Silence) continue;
            const double own = s.score_nats + offset;
            const std::int64_t birth = k - s.tau;
            if (s.tau == 0) {
                const double ln_noise = std::log(kN0 * 500.0);
                const double u_lo = 0.5 * (config.level_min_db / 10.0 * std::numbers::ln10 + ln_noise);
                const double u_hi = 0.5 * (config.level_max_db / 10.0 * std::numbers::ln10 + ln_noise);
                next.emplace(birth, FirstMark{NewbornIntegral(shared()->newborn_tables(), u_lo, u_hi,
                                                              config.newborn_grid_points,
                                                              config.newborn_live_range_nats, fade),
                                              own, s});
                continue;
            }
            const auto at = first_marks.find(birth);
            if (at == first_marks.end()) continue;
            FirstMark mark = std::move(at->second);
            EdgeHistory edges = mark.before.edges;
            advance(edges, mark.before.tau == 0);
            const TemplateValue tv = template_value(edges, shared()->template_tables());
            if (tv.s != 0.0)
                mark.reference.add(samples[static_cast<std::size_t>(k)], tv.s, tv.v, sigma2, config.systematic_floor);
            ASSERT_NEAR(own - mark.birth_score - timing.of(s), mark.reference.ln_z(), 1e-9)
                << "sample " << k << ", a first mark born at " << birth;
            ++compared;
            mark.before = s;
            next.emplace(birth, std::move(mark));
        }
        first_marks = std::move(next);
    }
    EXPECT_GT(compared, 1000);
    EXPECT_GT(shared_groups, 100);
    EXPECT_GT(copies, 100);
}

TEST(Search, NoiseKeepsScoresFinite) {
    // 60 s of DEV2's noise alone at the defaults, where the ceiling binds (design notes, topic 5, item 5): after every
    // step every live score is finite and the best is 0; never more stories than the ceiling. The statistics count
    // every sample, once finish has searched those the look-ahead held.
    ViterbiConfig config;
    config.stats = true;
    // The channelizer passes whole blocks of 32 channel samples: 60.1 s of input holds the first 60 s of samples.
    auto samples = noise(60.1, 18);
    ASSERT_GE(samples.size(), 22500u);
    samples.resize(22500);
    Search search(config, shared());
    for (std::size_t k = 0; k < samples.size(); ++k) {
        search.step(samples[k], sample_time(k), kN0);
        ASSERT_LE(search.live(), static_cast<std::size_t>(config.ceiling)) << "sample " << k;
        double best = -kInf;
        for (const StoryView& story : search.live_stories()) {
            ASSERT_TRUE(std::isfinite(story.score_nats)) << "sample " << k;
            best = std::max(best, story.score_nats);
        }
        ASSERT_EQ(best, 0.0) << "sample " << k;
    }
    EXPECT_EQ(search.stats().samples, 22500);
    search.finish();
    EXPECT_TRUE(std::isfinite(search.normalization_nats()));
    const SearchStats& stats = search.stats();
    EXPECT_EQ(stats.samples, 22500);
    std::int64_t counted = 0;
    for (const std::int64_t in_bin : stats.live_histogram) counted += in_bin;
    EXPECT_EQ(counted, 22500);
    // Windows of round(0.1 s / 2.667 ms) = 38 samples: 592 of them, the last one partial.
    EXPECT_EQ(stats.live_mean_100ms.size(), 593u);
    EXPECT_EQ(stats.live_max_100ms.size(), 593u);
    EXPECT_GT(stats.ceiling_samples, 0);
}

TEST(Search, IsDeterministic) {
    // Two runs over the same samples give the same text, with the same times, and the same statistics, to the bit: a
    // weak station (+3 dB S500) after a second of noise, so that the population is full.
    ViterbiConfig config;
    config.stats = true;
    const auto keying = signals::keying("PARIS", 25.0, 1.0);
    const auto samples = station(keying, 3.0, keying.back().second + 1.0, 19);
    Search first(config, shared());
    Search second(config, shared());
    search_all(first, samples);
    search_all(second, samples);
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
    // A successor more than beam_nats behind a score known at its sample is not created (plan §2.7, item 5): with and
    // without that skip, the same text, live stories, scores and statistics, on noise alone (the ceiling binding) and
    // on a station at +10 dB S500. The skipping run skipped many times (detail::SearchOptions' counter), the other
    // never.
    ViterbiConfig config;
    config.stats = true;
    const auto keying = signals::keying("PARIS", 25.0, 0.8);
    struct Case {
        std::string name;
        std::vector<double> samples;
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
        Search skipping(config, shared(), skip);
        Search creating(config, shared(), create);
        std::size_t most = 0;
        for (std::size_t k = 0; k < c.samples.size(); ++k) {
            skipping.step(c.samples[k], sample_time(k), kN0);
            creating.step(c.samples[k], sample_time(k), kN0);
            ASSERT_EQ(skipping.live(), creating.live()) << "sample " << k;
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
        EXPECT_GT(most, 100u);
        EXPECT_GT(skipping.stats().lines.size(), c.samples.size() / 2);
        ::testing::Test::RecordProperty(c.name + "_skipped_successors", std::to_string(skipped));
        EXPECT_GT(skipped, 1000);
        EXPECT_EQ(skipped_without, 0);
    }
}

TEST(Search, OpenTimingCapChangesNothing) {
    // The open speed's tables (search.hpp, "Work saved") capped at 16 taus, every later maximum of a newborn's first
    // mark computed on demand, against the default cap (7 352 taus at 32 speeds): on 10 s of noise (the ceiling
    // binding, newborns' first marks of every length) and on a station at +10 dB S500, the same live count after every
    // sample and the same stories, scores, lines, statistics and text at the end, with newborns' first marks longer
    // than the small cap among the live stories. The two runs of a case are independent: each runs on its own thread.
    ViterbiConfig config;
    config.stats = true;
    const auto keying = signals::keying("PARIS", 25.0, 0.8);
    struct Case {
        std::string name;
        std::vector<double> samples;
        std::string text;
    };
    std::vector<double> long_noise = noise(10.1, 18);
    long_noise.resize(3750);
    const std::vector<Case> cases = {{"noise", std::move(long_noise), ""},
                                     {"station", station(keying, 10.0, keying.back().second + 1.0, 21), "PARIS"}};
    shared();  // built before the threads start
    struct Run {
        std::vector<std::size_t> live;
        int longest_first_mark = 0;  // samples
        std::vector<StoryView> stories;
        SearchStats stats;
        double normalization_nats = 0.0;
        std::string text;
    };
    const auto run = [&config](const std::vector<double>& samples, detail::SearchOptions options) {
        Run r;
        Search search(config, shared(), options);
        for (std::size_t k = 0; k < samples.size(); ++k) {
            search.step(samples[k], sample_time(k), kN0);
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
        small_tables.open_timing_cap_samples = 16;
        auto default_run = std::async(std::launch::async, run, std::cref(c.samples), detail::SearchOptions{});
        auto capped_run = std::async(std::launch::async, run, std::cref(c.samples), small_tables);
        const Run a = default_run.get();
        const Run b = capped_run.get();
        EXPECT_EQ(a.text, c.text);
        ASSERT_EQ(a.live.size(), b.live.size());
        for (std::size_t k = 0; k < a.live.size(); ++k) ASSERT_EQ(a.live[k], b.live[k]) << "sample " << k;
        ASSERT_EQ(a.stories.size(), b.stories.size());
        for (std::size_t i = 0; i < a.stories.size(); ++i) {
            EXPECT_EQ(state_of(a.stories[i]), state_of(b.stories[i]));
            EXPECT_EQ(a.stories[i].score_nats, b.stories[i].score_nats);
            EXPECT_EQ(a.stories[i].line, b.stories[i].line);
        }
        EXPECT_TRUE(a.stats == b.stats);
        EXPECT_EQ(a.normalization_nats, b.normalization_nats);
        ::testing::Test::RecordProperty(c.name + "_longest_first_mark", std::to_string(b.longest_first_mark));
        EXPECT_EQ(a.longest_first_mark, b.longest_first_mark);
        EXPECT_GT(b.longest_first_mark, 16);
    }
}

TEST(Search, RefusesWhatItCannotUse) {
    const ViterbiConfig defaults;
    EXPECT_NO_THROW(Search(defaults, shared()));
    EXPECT_THROW(Search(defaults, nullptr), std::invalid_argument);
    const auto refuses = [&](auto change) {
        ViterbiConfig config;
        change(config);
        EXPECT_THROW(Search(config, shared()), std::invalid_argument);
    };
    refuses([](ViterbiConfig& config) { config.beam_nats = 0.0; });
    refuses([](ViterbiConfig& config) { config.beam_nats = kNaN; });
    refuses([](ViterbiConfig& config) { config.ceiling = 0; });
    refuses([](ViterbiConfig& config) { config.speed_count = 256; });
    refuses([](ViterbiConfig& config) { config.over_rate_per_s = 0.0; });
    refuses([](ViterbiConfig& config) { config.over_rate_per_s = 375.0; });  // P = 1
    refuses([](ViterbiConfig& config) { config.rescue_epsilon = 0.0; });
    refuses([](ViterbiConfig& config) { config.rescue_epsilon = 1.0; });
    refuses([](ViterbiConfig& config) { config.word_end_p = 0.0; });
    refuses([](ViterbiConfig& config) { config.over_end_q = 1.0; });
    refuses([](ViterbiConfig& config) { config.quadrature_order = 1; });
    refuses([](ViterbiConfig& config) { config.quadrature_order = 21; });
    refuses([](ViterbiConfig& config) { config.fading_spread_hz = -1.0; });   // FadingTables refuses it
    refuses([](ViterbiConfig& config) { config.dot_min_ms = 0.0; });          // Fist refuses it
    refuses([](ViterbiConfig& config) { config.systematic_floor = -0.01; });  // a relative amplitude in [0, 1]
    refuses([](ViterbiConfig& config) { config.systematic_floor = 1.01; });
    refuses([](ViterbiConfig& config) { config.systematic_floor = kNaN; });
    refuses([](ViterbiConfig& config) { config.newborn_switch_sigma_u = 0.0; });
    refuses([](ViterbiConfig& config) { config.newborn_live_range_nats = 0.0; });
    refuses([](ViterbiConfig& config) { config.added_variance_multiplier_max = 0.5; });
    refuses([](ViterbiConfig& config) { config.added_variance_multiplier_max = kInf; });
    refuses([](ViterbiConfig& config) { config.added_variance_level_knee = 0.0; });
    refuses([](ViterbiConfig& config) { config.amplitude_update_min_template = -0.1; });
    refuses([](ViterbiConfig& config) { config.amplitude_update_min_template = 1.5; });
    refuses([](ViterbiConfig& config) { config.merge_edge_history_samples = -1; });
    refuses([](ViterbiConfig& config) { config.merge_edge_history_samples = 17; });
    refuses([](ViterbiConfig& config) { config.symbol_prior_floor = 0.0; });
    refuses([](ViterbiConfig& config) { config.symbol_prior_floor = 1.0 / 62.0; });
    // The shared parts must be this configuration's.
    refuses([](ViterbiConfig& config) { config.sample_interval_s = 0.002; });
    refuses([](ViterbiConfig& config) { config.level_max_db = 60.0; });
    refuses([](ViterbiConfig& config) { config.newborn_grid_points = 64; });
    // The durations must be whole numbers of samples, nu's memory a whole number of bins; each refusal names its field.
    const auto names = [&](auto change, const std::string& field) {
        ViterbiConfig config;
        change(config);
        try {
            Search search(config, shared());
            ADD_FAILURE() << field << ": not refused";
        } catch (const std::invalid_argument& e) {
            EXPECT_NE(std::string(e.what()).find(field), std::string::npos) << e.what();
        }
    };
    names([](ViterbiConfig& config) { config.newborn_fade_interval_s = 0.027; }, "newborn_fade_interval_s");
    names([](ViterbiConfig& config) { config.nu_bin_s = 0.04; }, "nu_bin_s");
    names([](ViterbiConfig& config) { config.nu_memory_s = 0.42; }, "nu_memory_s");
    names([](ViterbiConfig& config) { config.nu_memory_s = 0.04; }, "nu_memory_s");  // 15 samples: not whole bins
    EXPECT_NO_THROW([&] {
        ViterbiConfig ends = defaults;
        ends.systematic_floor = 0.0;
        Search none(ends, shared());
        ends.systematic_floor = 1.0;
        Search whole(ends, shared());
        ends.newborn_fade_interval_s = 4 * defaults.sample_interval_s;
        Search other(ends, shared());
    }());

    // A refused sample changes nothing. A time not at a later sample than the last given is refused.
    ViterbiConfig with_stats = defaults;
    with_stats.stats = true;
    Search search(with_stats, shared());
    for (std::size_t k = 0; k < 11; ++k) search.step(0.01, sample_time(k), kN0);
    ASSERT_EQ(search.stats().samples, 11);
    const double offset = search.normalization_nats();
    const std::size_t live = search.live();
    for (const double n0 : {0.0, -kN0, kNaN, kInf})
        EXPECT_THROW(search.step(0.01, sample_time(11), n0), std::invalid_argument) << n0;
    EXPECT_THROW(search.step(0.01, sample_time(10), kN0), std::invalid_argument);  // not later than the last
    EXPECT_THROW(search.step(0.01, sample_time(10) + 0.3 * kDelta, kN0), std::invalid_argument);  // the same sample
    EXPECT_THROW(search.step(0.01, kNaN, kN0), std::invalid_argument);
    EXPECT_THROW(search.step(kNaN, sample_time(11), kN0), std::invalid_argument);
    EXPECT_THROW(search.step(-0.01, sample_time(11), kN0), std::invalid_argument);
    EXPECT_THROW(search.step(kInf, sample_time(11), kN0), std::invalid_argument);
    EXPECT_EQ(search.normalization_nats(), offset);
    EXPECT_EQ(search.live(), live);
    EXPECT_EQ(search.stats().samples, 11);
    EXPECT_NO_THROW(search.step(0.01, sample_time(11), kN0));
    EXPECT_EQ(search.stats().samples, 12);
    search.finish();
    EXPECT_EQ(search.stats().samples, 12);
    // After finish the stream has ended.
    EXPECT_THROW(search.step(0.01, sample_time(12), kN0), std::logic_error);
}

TEST(Search, SearchesEachSampleAsItArrives) {
    // D23: no hold and no look-ahead. After each step that sample has been searched (the statistics count it), and
    // finish searches nothing more. ("E" at 25 WPM and +20 dB S500, then 100 ms of noise.)
    ViterbiConfig config;
    config.stats = true;
    const auto keying = signals::keying("E", 25.0, 0.5);
    auto samples = station(keying, 20.0, keying.back().second + 0.2, 25);
    ASSERT_GE(samples.size(), 243u);
    samples.resize(243);
    Search search(config, shared());
    for (std::size_t k = 0; k < samples.size(); ++k) {
        search.step(samples[k], sample_time(k), kN0);
        ASSERT_EQ(search.stats().samples, static_cast<std::int64_t>(k + 1)) << "sample " << k;
    }
    const double offset = search.normalization_nats();
    search.finish();
    search.finish();
    EXPECT_EQ(search.stats().samples, 243);
    EXPECT_EQ(search.normalization_nats(), offset);
    EXPECT_EQ(text_of(search.best_text()), "E");
}

TEST(Search, ClaimsTheTrueEdgesOfACleanDash) {
    // The known case behind D23: one dash, noise-free, amplitude known (P = 0), D17's 2.5 ms edges, 25 and 40 WPM at +20
    // and +40 dB S500, over claimed key-downs -3 ... +2 and key-ups -2 ... +3 samples. With the key-down mid-sample (u =
    // Delta / 2, the template's center) the true claim scores best and leads its best one-sample neighbor by at least 1
    // nat (heuristic; the ratio form put claims 2 samples off 89 to 1 092 nats ahead). With the key-down on a sample (u = 0,
    // half a sample from the center either way) the best claim is (0, 0) or (+1, 0), within 1 nat of (0, 0): the tie.
    // (The design notes' Part 15 and the study of 2026-10-28 label these two the other way round: their "shift 0", a
    // start of 0.5 s, is 187.5 samples, mid-sample.)
    const auto& tables = shared()->template_tables();
    const double sigma2 = kN0 * shared()->output_noise().sigma2_per_n0_hz;
    const double w = shared()->output_noise().w_strong;
    const double floor = ViterbiConfig{}.systematic_floor;
    for (const double wpm : {25.0, 40.0}) {
        for (const double level : {20.0, 40.0}) {
            for (const int eighths : {0, 4}) {  // the key-down, eighths of a sample before sample ceil(0.5 s / Delta)
                const double start = std::ceil(0.5 / kDelta) * kDelta - eighths * kDelta / 8.0;
                const auto keying = signals::keying("T", wpm, start);
                const double a = amplitude_at(level);
                const auto e = samples_of(signals::keyed_signal_d17(keying, kInputRate, keying.back().second + 0.5,
                                                                    station_hz(), a, 0.0, 11));
                const auto kd = static_cast<std::int64_t>(std::ceil(keying[0].first / kDelta - 1e-9));
                const auto ku = static_cast<std::int64_t>(std::ceil(keying[0].second / kDelta - 1e-9));
                const auto score = [&](int dd, int du) {
                    EdgeHistory h;
                    double total = 0.0;
                    for (std::int64_t k = kd - 10; k < ku + 30; ++k) {
                        advance(h, k == kd + dd || k == ku + du);
                        const TemplateValue tv = template_value(h, tables);
                        if (tv.s == 0.0) continue;
                        const double v = systematic_variance_fs2(sigma2, a * a, tv.s, tv.v, floor);
                        total += story_evidence(e[static_cast<std::size_t>(k)], tv.s, a, 0.0, v, sigma2, w, 5).score_nats;
                    }
                    return total;
                };
                double best = -kInf;
                std::pair<int, int> at{0, 0};
                double neighbor = -kInf;
                for (int dd = -3; dd <= 2; ++dd)
                    for (int du = -2; du <= 3; ++du) {
                        const double x = score(dd, du);
                        if (x > best) best = x, at = {dd, du};
                        if (std::abs(dd) + std::abs(du) == 1) neighbor = std::max(neighbor, x);
                    }
                const double aligned = score(0, 0);
                std::cout << "measured: " << wpm << " WPM, +" << level << " dB S500, shift " << eighths
                          << "/8: best claim (" << at.first << ", " << at.second << "), aligned leads its best "
                          << "one-sample neighbor by " << aligned - neighbor << " nats\n";
                if (eighths == 4) {
                    EXPECT_EQ(at, (std::pair<int, int>{0, 0})) << wpm << " WPM, +" << level << " dB S500";
                    EXPECT_GE(aligned - neighbor, 1.0) << wpm << " WPM, +" << level << " dB S500";
                } else {
                    EXPECT_TRUE(at == (std::pair<int, int>{0, 0}) || at == (std::pair<int, int>{1, 0}))
                        << wpm << " WPM, +" << level << " dB S500: (" << at.first << ", " << at.second << ")";
                    EXPECT_LE(best - aligned, 1.0) << wpm << " WPM, +" << level << " dB S500";
                }
            }
        }
    }
}

TEST(Search, AShortLetterGapBetweenCommonLettersKeepsThemApart) {
    // A flat 1/62 makes two letters cost 4.524 nats more than one merged symbol, so a gap reads as a letter gap only above
    // 2.51 dots (derived; sigma 0.3 in ln(length)). The on-air prior takes "DA" (-.. .-, counts 84 and 127) over <BT> (-...-,
    // count 15) down to about 2 dots. "CQ DE DA K" at 25 WPM, +20 dB S500, the gap inside "DA" 2.3 dots long: read as
    // <BT> with the flat prior and as DA with the on-air one.
    const auto keying = signals::keying("CQ DE DA K", 25.0, 0.5);
    std::vector<std::pair<double, double>> squeezed = keying;
    constexpr double kDot = 1.2 / 25.0;
    constexpr std::size_t kFirstOfA = 4 + 4 + 3 + 1 + 3;  // marks of C, Q, D, E and the D before it
    ASSERT_NEAR(keying[kFirstOfA].first - keying[kFirstOfA - 1].second, 3.0 * kDot, 1e-9);
    for (std::size_t i = kFirstOfA; i < squeezed.size(); ++i) {
        squeezed[i].first -= 0.7 * kDot;
        squeezed[i].second -= 0.7 * kDot;
    }
    const auto samples = station(squeezed, 20.0, squeezed.back().second + 1.0, 44);
    ViterbiConfig flat;
    flat.symbol_prior_on_air = false;
    EXPECT_EQ(decode(flat, samples), "CQ DE <BT> K");
    EXPECT_EQ(decode(ViterbiConfig{}, samples), "CQ DE DA K");
}

TEST(Search, MarkTimesAreKeyedTimes) {
    // Part 15, item 1 (part 11): a story's claimed edges are the first samples at or after the keyed edges, so a
    // letter's start_s, its first mark's claimed key-down, lies 0 to 2.667 ms after its keyed start, or one sample later
    // at a mid-sample edge position's tie: -0.1 to +5.43 ms per letter, and 0.8 to 2.0 ms in the mean (a measured
    // tolerance; the convention expects 1.33 ms). "PARIS PARIS" at 25 WPM, +30 dB S500, at 8 edge positions.
    const std::array<int, 5> marks_per_letter = {{4, 2, 3, 2, 3}};  // P, A, R, I, S
    int letters = 0;
    double offsets = 0.0;
    for (int position = 0; position < 8; ++position) {
        SCOPED_TRACE("edges moved by " + std::to_string(position) + "/8 of a sample");
        const double start_s = 0.5 + position * kDelta / 8.0;
        const auto keying = signals::keying("PARIS PARIS", 25.0, start_s);
        Search search(ViterbiConfig{}, shared());
        search_all(search, station(keying, 30.0, keying.back().second + 1.0, 30 + static_cast<unsigned>(position)));
        const std::vector<kz4ap::DecodedSymbol> text = search.best_text();
        ASSERT_EQ(text_of(text), "PARIS PARIS");
        std::size_t mark = 0;
        std::size_t letter = 0;
        for (const kz4ap::DecodedSymbol& symbol : text) {
            if (symbol.text == " ") continue;
            const double keyed = keying[mark].first;
            EXPECT_GE(symbol.start_s - keyed, -1e-4) << symbol.text << " at " << symbol.start_s;
            EXPECT_LE(symbol.start_s - keyed, 5.43e-3) << symbol.text << " at " << symbol.start_s;
            offsets += symbol.start_s - keyed;
            mark += static_cast<std::size_t>(marks_per_letter[letter % 5]);
            ++letter;
            ++letters;
        }
    }
    EXPECT_EQ(letters, 80);
    std::cout << "measured: mean start_s - keyed start " << 1e3 * offsets / letters << " ms\n";
    EXPECT_GE(offsets / letters, 0.8e-3);
    EXPECT_LE(offsets / letters, 2.0e-3);
}
