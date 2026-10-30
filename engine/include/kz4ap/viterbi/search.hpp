// The new decoder's search (plan §2.7; design notes, topic 5, and "Detection, amplitude, frequency and the mark's
// shape", parts 3 to 11; plan 1b, Task 10): a time-synchronous Viterbi beam search over stories, one envelope sample at
// a time (sample_interval_s, Delta = 2.667 ms: every 4th channel sample at 1 500 samples/s, e_k = |y_k| of the front
// filter's output; front_filter.hpp). A story is one continuous account of what has been sent on the channel since
// its start: no signal, or Morse in a mark (key down) or a gap (key up) that has lasted tau samples, at a speed ("open"
// until the story takes it), at a node of the Morse tree (morse_tree.hpp), with its own claimed edges, its own
// amplitude and long-run power, and its own text, a chain of letters and word spaces shared between stories. Spacing is
// standard: there is no Farnsworth factor (the owner's decision D14 of 2026-10-21; docs/backlog.md, "Farnsworth spacing
// in the new decoder").
//
// The score. A story's score, nats, is the log-likelihood of its story and the samples so far against the story "noise
// only, throughout". Scores are held relative to the best story's (normalization_nats() is the sum of the best scores
// subtracted, so a story's own score is its relative score plus that sum). Every point is booked at the sample it is
// earned (design notes, topic 5, item 9): `base` is the score at the element's start plus the scores of the samples
// since, and the score adds the current element's timing.
//
// What a story carries for the evidence (parts 5 to 8):
//   - its edge history (mark_template.hpp, EdgeHistory): the claimed edges of the last 16 samples and the key state
//     before them. A story claims an edge at the first sample of each element: a successor created at sample k (a mark
//     that ends, a gap that ends, an over's end, a birth) claims its key-down or key-up at sample k + 1, the first
//     sample of its element (j = 0; the first sample at or after the keyed edge, owner, 2026-10-25). Its template s_k
//     and edge term v_k are template_value's (the superposition of its edges' averaged responses);
//   - its amplitude N(mu, P) and the sample t_m of its last update (amplitude.hpp), the previous sample's mode (Newton's
//     start), its long-run sums (long_run.hpp, LongRun: Omega_hat and nu) and its cached Omega_bar;
//   - while it is a newborn, its newborn integral (newborn.hpp), shared between the stories descended from one newborn
//     until one of them scores a sample, when it takes its own copy (copy on write), and dropped once it switches to
//     N(mu, P).
//
// One sample k searched, every story in this order (plan 1b, Task 10; parts 6, 7 and 9):
//   1. Its template (s_k, v_k) from its edge history after this sample's claim (advance, then template_value).
//   2. If s_k != 0, with sigma_y^2 = N0 x sigma2_per_n0_hz (74.5 Hz; the front filter's output noise, O4) and the one
//      constant w = w_strong (0.1987; D23):
//      - a story in the newborn integral adds the sample to it (its score is the integral's ln Z_k - ln Z_(k-1), with
//        V(a_i) = sigma_y^2 + a_i^2 (f^2 s_k^2 + v_k) at each amplitude of its grid and, every 8 scored samples, the
//        prediction step; newborn.hpp) and, after it, switches to N(mu, P) by moment matching when sigma_u <
//        newborn_switch_sigma_u (0.1 nepers) or at the latest at the 15th sample after its first mark's claimed key-up
//        (should_switch);
//      - any other story predicts its amplitude to sample k from (mu, P, t_m) and its cached Omega_bar
//        (predict_amplitude, at the configuration's f_D; genie_fading_spread_hz in its place when set), takes V_k =
//        sigma_y^2 + (mu^2 + P)(f^2 s_k^2 + v_k) from that prediction (systematic.hpp), scores the sample against the
//        reference every story shares by story_evidence (observation.hpp; order quadrature_order, Newton started from the
//        previous sample's mode) and sets its amplitude to the posterior, t_m = k;
//      - every one adds the sample to its long-run sums (LongRun: at_sample(k), add(s_k, e_k, sigma_y^2)) and, once out
//        of the integral (from the sample at which it switches), looks Omega_bar up again from them (the default
//        table, ViterbiShared::omega_bar(): only after samples with s_k != 0, part 7).
//      If s_k = 0 the sample adds exactly 0 and nothing changes.
//   3. The sample's score is booked into its base, and its score is its base plus the current element's timing.
// Every phase scores so: a mark; a gap within 16 samples of its key-up (the fall's tail is explained by the story that
// keeps the mark, part 5); and the no-signal story within 16 samples of an over's end. Beyond them s = 0.
//
// Restarts (part 7): a newborn from silence starts with a new newborn integral (its prior's bounds in FS fixed from N0 at
// its birth, O10) and empty long-run sums, keeping the no-signal story's edge history (the tail of the over that ended,
// if any). A rescue opens the speed only: it keeps its parent's edges, amplitude and long-run sums (decision log,
// 2026-10-30: a sender who changes speed keeps his level; with a new level the rescue's first word cost more than the
// wrong speed's reading did, and a speed step was never followed). An over's end leaves the no-signal story with the
// over's amplitude and sums while it scores the over's tail, and restarts them (forgotten) once the tail has ended; so
// every over starts with no level.
//
// The rest is the first prototype's search, counted in samples:
//   - Durations, in the hazard form (fist.hpp), in samples: a story still in its mark or gap at tau samples has paid
//     ln S(tau), one that ends there ln P(tau) instead. A mark keeps the better of ln S_dot(tau) and ln S_dash(tau),
//     each plus its label's text cost at the node (0 where that child exists, -infinity where not), and ends into a
//     dot and a dash, each with its own ln P. A gap keeps the best of the kinds allowed at its node, each with its own
//     cost and survival, and ends as each kind. A newborn's first mark keeps the best over the speeds and both labels
//     of ln(1/speed_count) + ln S_label(s, tau) and ends into every speed and label with a finite ln P, each paying
//     ln(1/speed_count) and its own ln P.
//   - Text, by look-ahead, with every symbol equally likely (plan §2.6): a letter costs ln 62 = 4.127 nats when it
//     starts. After a mark at a node, the gap is between elements if the node has children (cost 0); if it holds a
//     symbol, between letters (ln(1/(1 - p)) = 0.396 nats) or between words (ln(1/(p (1 - q))) = 1.161 nats), and the
//     over may end, ln(1/(p q)) = 4.296 nats, taken at the mark's end as a step to the no-signal state; p = word_end_p
//     = 1/3.06 and q = over_end_q = 1/24.
//   - No signal: the story pays ln(1 - P) per sample, P = over_rate_per_s x sample_interval_s = 1.333e-4 (8.92 nats
//     for a birth, ln P).
//
// step searches each sample as it arrives, with the time and N0 given with it (D23: no hold, no look-ahead).
// One sample searched, in this order (plan §2.7):
//   1. At the channel's first sample there is one story: no signal, score 0, empty text, no edges.
//   2. Every story continues (it scores the sample, above) and ends. In a mark: it also ends here as a dot and as a
//      dash, each a successor in a gap at the child node, and, where the child holds a symbol, a successor in the
//      no-signal state that ends the over after that letter. In a gap: it also ends here as each kind allowed, each a
//      successor that starts a mark at the next sample: an element gap keeps the node; a letter or word gap pays the
//      letter at the root and, for a word gap, adds a word space; each story that starts a letter also pays
//      ln(1/(1 - epsilon)) for not restarting. Every successor carries its parent's state after this sample (its
//      edges, amplitude, sums and newborn integral) and claims its edge at the next sample.
//   3. Births. From silence: while the no-signal story lives, a newborn starts a mark at the next sample, with the
//      continuing no-signal story's score plus ln P plus ln(1/62), its text with a word space after it if it is not
//      empty, its speed and level open. Rescue: where at least one story starts a letter, one story starts that mark,
//      from the best such letter-starting successor (whether or not that successor is created): its score less
//      ln(1 - epsilon) plus ln epsilon (6.908 nats behind it, epsilon = rescue_epsilon = 1e-3), its text and level, its
//      speed open.
//   4. Merging on the packed state (Ruling 64; the owner's per-survivor decision): successors created at the same
//      sample in the same state are merged, keeping the better score (of equal scores, the earlier created) with its
//      own history (edges, amplitude, sums, newborn integral, text). The state is: activity; key up or down; samples in
//      the current element; speed or open; tree node. The no-signal state's clock counts the samples since the over's
//      end while the over's tail still scores, so that stories with different key-ups stay apart until their templates
//      agree (the build's Task 10: merging them lost about 10 nats on every over's end); there is at most one quiet
//      no-signal story. Continuing stories collide only there.
//   5. Pruning: every story more than beam_nats behind the best is dropped; if more than `ceiling` remain, the
//      `ceiling` best are kept, ties broken by the state, then by creation order (a counter), so that a run depends on
//      no container's order and no address. A successor already more than beam_nats behind a score known at the
//      sample is not created, except an over's end (always offered into the no-signal state, where it merges); this
//      changes no output (detail::SearchOptions turns it off for the tests that show it).
//   6. Scores are held relative to the best.
//   7. best_text() gives the best story's text, each symbol with its start and end times and the probability 1; if the
//      best story stands at a node holding a symbol, that symbol is its last letter.
// Times: a letter's start_s is the time of its first mark's claimed key-down sample, its end_s that of its last mark's
// claimed key-up sample (part 11: keyed times to within 0 to 2.667 ms, the claimed edge being the first sample at or
// after the keyed edge); a word space spans its gap, from the key-up before it to the key-down after it; an over's
// opening space has start_s = end_s = its first mark's key-down. A claimed edge's time is the time of the sample at
// which its successor was created plus Delta (the next sample's: the stream's own unless samples were skipped there,
// ViterbiDecoder::process).
//
// Four points the plan leaves open, decided in the first prototype and kept:
//   1. A birth from silence comes from the continuing no-signal story, before it merges with the over's ends created at
//      that sample; so a new over follows the end of the last one by at least one silent sample.
//   2. A successor's score at the sample it is created is its parent's base after this sample's score (without the
//      current element's timing), plus ln P of the element that ended and the costs paid at that end. A gap's kind
//      cost is paid from the gap's first sample on, inside the merged gap's maximum.
//   3. A rescue newborn's parent is the best of all the letter-starting successors of the sample, including those the
//      skip of step 5 does not create.
//   4. The Doppler spread f_D that sets the amplitude's prediction and nu is genie_fading_spread_hz when it is set.
//
// Work saved without changing any output: the maxima over an open speed (a newborn's first mark) are kept in tables
// for the first `cap` taus, cap = floor(4 000 000 bytes / (2 x speed_count + 4) / 8 bytes) (7 352 samples, 19.6 s, at
// 32 speeds), computed again the same way for any tau beyond; an offer that loses its merge costs a compare; a newborn
// integral is copied only when a story that shares it scores a sample.
// The statistics (SearchStats) are kept only when config.stats is true and never change the decoding. They grow with
// the channel's time: about 96 bytes per newborn line, up to two lines per sample; they are meant for bench recordings.
// A Search is deterministic: the same samples give the same text and statistics.
#pragma once

#include "kz4ap/decoder.hpp"
#include "kz4ap/viterbi/amplitude.hpp"
#include "kz4ap/viterbi/config.hpp"
#include "kz4ap/viterbi/mark_template.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kz4ap {
class ViterbiShared;  // viterbi_decoder.hpp: the parts every decoder of a configuration shares
}

namespace kz4ap::viterbi {

// How a newborn line (a newborn and its descendants) began.
enum class LineKind {
    Start,    // line 0: the channel's first story, in the no-signal state
    Silence,  // a newborn from silence: an over starts
    Rescue,   // a rescue: the sender takes a new speed at a letter start
};

// Which step removed a line's last story, or Alive while it has one.
enum class LineEnd { Alive, Merge, Beam, Ceiling };

// What became of one newborn line.
struct NewbornLine {
    double birth_s = 0.0;  // s: the time of the sample at which it was created (line 0: the first sample's)
    LineKind kind = LineKind::Start;
    // s: the first sample after whose pruning one of its stories was the best
    std::optional<double> first_led_s;
    // nats: how far its best story was behind the best, at most, after the pruning of each sample from its birth until
    // it first led, or over its life if it never led; at its birth sample, its newborn's own deficit even where the
    // newborn did not survive that sample
    double largest_deficit_nats = 0.0;
    LineEnd end = LineEnd::Alive;
    std::optional<double> end_s;  // s: the sample at which its last story was removed
    // its last story was removed before any story of the line had left the newborn's first mark (false for line 0)
    bool ended_in_first_element = false;
    friend bool operator==(const NewbornLine&, const NewbornLine&) = default;
};

// The search's instrumentation, kept only when config.stats is true.
struct SearchStats {
    std::int64_t samples = 0;          // samples searched
    std::int64_t ceiling_samples = 0;  // samples at which the ceiling dropped at least one story
    // samples by the number of live stories after pruning, in bins of 10 stories: 0-9, 10-19, ...
    std::vector<std::int64_t> live_histogram;
    // stories: the mean and the largest live count after pruning in each 0.1 s (round(0.1 s / sample_interval_s) = 38
    // samples), the last entry over the samples of its window searched so far
    std::vector<float> live_mean_100ms;
    std::vector<float> live_max_100ms;
    std::vector<NewbornLine> lines;  // indexed by line id; line 0 is the channel's first story
    friend bool operator==(const SearchStats&, const SearchStats&) = default;
};

// A story's activity.
enum class Activity { NoSignal, Morse };

// One live story, for tests.
struct StoryView {
    Activity activity;
    bool key_down;      // in a mark (Morse only)
    int tau;            // samples in the current element: 0 for one that starts at the next sample; no signal: samples
                        // since an over's end while its tail scores (1 ... 17), 0 once quiet
    int speed;          // 0 ... speed_count - 1, or -1 while open (and for no signal)
    int node;           // a mark: the node it started from; a gap: the node of the mark before it; -1 for no signal
    double score_nats;  // relative to the best: 0 for the best, at most beam_nats behind
    std::int64_t line;  // its newborn line
    Amplitude amplitude;      // its amplitude as stored (mu FS, P FS^2, t_m): updated, not predicted; zeros before any
    double omega_bar_fs2;     // FS^2: its cached Omega_bar (0 while in the newborn integral, and before any sample)
    double nu;                // its long-run sums' nu (1 before any sample with s != 0)
    double omega_hat_fs2;     // FS^2: its long-run sums' Omega_hat (0 before any sample with s != 0)
    bool in_newborn_integral;  // it scores by the newborn integral (it has not switched yet)
    EdgeHistory edges;         // its claimed edges (the next sample's claim not yet made)
    double mode_a_fs;          // FS: the last scored sample's mode, Newton's start at its next
    std::uintptr_t newborn_grid;  // the identity of its newborn integral (0 for none): stories sharing one have the same
};

namespace detail {

// Settings of the search that change no output, for the tests that show it.
struct SearchOptions {
    // Do not create a successor already more than beam_nats behind a score known at its sample (plan §2.7, item 5).
    bool skip_successors_beyond_beam = true;
    // The taus whose open speed's maxima are kept in tables, samples: 0 for the cap the 4 000 000-byte budget sets
    // (search.hpp, "Work saved"); a smaller cap computes more of them on demand.
    int open_timing_cap_samples = 0;
    // If set, counts the skips: each time a successor, or a newborn's first mark's whole set of gap successors, was not
    // created because the beam would drop it, so that a test can show that the skip cut something.
    std::int64_t* skipped_successors = nullptr;
    // If set, counts the newborn integrals copied (copy on write: a story that shares its integral scores a sample).
    std::int64_t* newborn_copies = nullptr;
};

// A candidate story at a sample, as merging and pruning see it.
struct Ranked {
    std::uint64_t state;     // the story's state, packed so that its order is one fixed order of the states
    double score_nats;
    std::uint64_t creation;  // its creation order
};

// The pruning order: the better score first, then the lower state, then the earlier creation.
bool ranks_before(const Ranked& a, const Ranked& b);

// The merging rule (plan §2.7, item 4): whether a candidate offered into a state replaces the one kept there, by the
// better score and, of equal scores, the earlier created. The search's offers decide by this function.
bool replaces(double score, std::uint64_t creation, double kept_score, std::uint64_t kept_creation);

// Merging: of the candidates in one state, the one `replaces` keeps. The indices of those kept, in the order given.
std::vector<std::size_t> merge(std::span<const Ranked> candidates);

// Pruning (plan §2.7, item 5): those more than beam_nats behind the best are dropped by the beam; then, if more than
// `ceiling` remain, all but the `ceiling` first by ranks_before are dropped by the ceiling. The indices of those kept
// (in the order the search keeps them) and of those each step dropped. The search prunes by the same routine.
struct Pruning {
    std::vector<std::size_t> kept;
    std::vector<std::size_t> beam_dropped;
    std::vector<std::size_t> ceiling_dropped;
};
Pruning prune(std::span<const Ranked> candidates, double beam_nats, int ceiling);

}  // namespace detail

class Search {
public:
    // shared: the parts of this configuration on this channel filter (make_viterbi_shared): the template's tables, the
    // output noise, the Omega_bar table and the newborn's tables. Throws std::invalid_argument for no shared parts,
    // shared parts built for another sample interval or newborn grid, and a configuration the search cannot use: a beam
    // that is not positive, a ceiling below 1, more than 255 speeds, a P (over_rate_per_s x sample_interval_s),
    // epsilon, p or q outside (0, 1), a quadrature order outside 2 ... 20, a systematic_floor outside [0, 1], a
    // newborn_fade_interval_s, nu_bin_s or nu_memory_s that is not a whole number of samples (nu's
    // memory of bins; naming the field), a newborn_switch_sigma_u or newborn_live_range_nats that is not positive;
    // and what Fist, FadingTables and LongRun refuse.
    Search(const ViterbiConfig& config, std::shared_ptr<const ViterbiShared> shared,
           detail::SearchOptions options = {});
    ~Search();
    Search(Search&&) noexcept;
    Search& operator=(Search&&) noexcept;
    Search(const Search&) = delete;
    Search& operator=(const Search&) = delete;

    // One envelope sample: e_fs, |y_k| (FS); t_s, its stream time, t_k = k Delta (s), sample k being the one whose
    // index k = round(t_s / Delta) (the channel sample of absolute index 4 k); n0_fs2_per_hz, the hiss N0 (FS^2/Hz).
    // Throws std::invalid_argument, before changing anything, for an N0 that is not positive and finite, a t_s that is
    // not finite or whose k is not later than the previous sample's, and an e that is negative or not finite; and
    // std::logic_error after finish(). The sample is searched at once (D23).
    void step(double e_fs, double t_s, double n0_fs2_per_hz);

    // Ends the stream: nothing is queued, so it searches nothing; later samples are refused. A second call does nothing.
    void finish();

    // The best story's text (plan §2.7, item 7): " " is a word space; each symbol's probability is 1.
    std::vector<DecodedSymbol> best_text() const;

    const SearchStats& stats() const;

    // For tests: the live stories after the last searched sample's pruning, and each one's state and score, in the
    // order of their states (the order of detail::Ranked::state).
    std::size_t live() const;
    std::vector<StoryView> live_stories() const;
    // nats: the sum of the best scores subtracted so far; a story's score against "noise only, throughout" is its
    // relative score plus this.
    double normalization_nats() const;
    // For tests: the entries of the shared text in use, the bytes the open timing's tables hold in entries ("Work
    // saved": at most 4 000 000), and the newborn integrals the live stories hold (each counted once) with their bytes.
    std::size_t text_entries() const;
    std::size_t open_timing_bytes() const;
    std::size_t newborn_integrals() const;
    std::size_t newborn_integral_bytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace kz4ap::viterbi
