// The new decoder's search (plan §2.7; design notes, topic 5): a time-synchronous Viterbi beam search over stories, one
// piece at a time (piece_s, 2 ms: the sum of samples_per_piece = 3 channel samples at 1 500 samples/s). A story is one
// continuous account of what has been sent on the channel since its start: no signal, or Morse in a mark (key down) or
// a gap (key up) that has lasted tau pieces, at a speed and a Farnsworth factor (each "open" until the story takes it),
// at a node of the Morse tree (morse_tree.hpp), with its own level (level.hpp) and its own text, a chain of letters and
// word spaces shared between stories.
//
// The score. A story's score, nats, is the log-likelihood of its story and the signal so far against the story "noise
// only, throughout", so a gap piece adds 0. Scores are held relative to the best story's (normalization_nats() is the
// sum of the best scores subtracted, so a story's own score is its relative score plus that sum). Every point is booked
// at the piece it is earned (design notes, topic 5, item 9):
//   - Evidence. A mark is scored in blocks: a block is the coherent sum S of a mark's pieces, at most
//     B = floor(block_length_s(f_D, block_loss_db) / piece_s) pieces (195 at f_D = 1 Hz and 0.5 dB of signal energy:
//     390.37 ms; with f_D = 0, no limit), f_D being genie_fading_spread_hz if set, else fading_spread_hz. A block of n
//     pieces has m = samples_per_piece x n samples and the noise V = N0 c(m), FS^2, with N0 the hiss passed with the
//     current piece (FS^2/Hz) for the whole running block, and c beyond its end extended by its last increment,
//     c(m) = c(M) + (m - M)(c(M) - c(M - 1)), exact once m exceeds the filter's correlation length (33 lags). While a
//     mark lasts, its current block's evidence so far is part of the score at every piece; a finished block's evidence
//     enters the score for good and its posterior becomes the story's level. Every story in a mark makes exactly one
//     scoring call per piece, and its continuation and all its successors at that piece use that call's evidence and
//     level: a learned story calls score_block with its level predicted to the piece's time t_k (the update stamps a
//     level with its block's end, so the prior of the block in hand is predicted end to end; a predicted level is used
//     once and never stored), starting Newton's method from the previous piece's mode in the same block; a newborn's
//     first block, of at most min(B, the flat prior's max_pieces) pieces, calls score_first_block, and the level is
//     learned when it finishes.
//   - Durations, in the hazard form (fist.hpp): a story still in its mark or gap at tau pieces has paid ln S(tau), one
//     that ends there ln P(tau) instead. A mark keeps the better of ln S_dot(tau) and ln S_dash(tau), each plus its
//     label's text cost at the node (0 where that child exists, -infinity where not), and ends into a dot and a dash,
//     each with its own ln P. A gap keeps the best of the kinds allowed at its node, each with its own cost and
//     survival, and ends as each kind. A newborn's first mark keeps the best over the speeds and both labels of
//     ln(1/speed_count) + ln S_label(s, tau) (3.466 nats for 32 speeds) and ends into every speed and label with a
//     finite ln P, each paying ln(1/speed_count) and its own ln P. A story's factor is open until its first letter or
//     word gap: the gap keeps, for those two kinds, the best over the factors of ln(1/factor_count) +
//     ln S_kind(s, f, tau) (3.135 nats for 23 factors; the element gap does not depend on the factor and pays no factor
//     prior), and ending as a letter or word gap it branches into the factors, each paying ln(1/factor_count) and its
//     own ln P; ending as an element gap leaves the factor open.
//   - Text, by look-ahead, with every symbol equally likely (plan §2.6): a letter costs ln(the tree's symbols) =
//     ln 62 = 4.127 nats when it starts. After a mark at a node, the gap is between elements if the node has children
//     (cost 0); if it holds a symbol, between letters (ln(1/(1 - p)) = 0.396 nats) or between words
//     (ln(1/(p (1 - q))) = 1.161 nats), and the over may end, ln(1/(p q)) = 4.296 nats, taken at the mark's end as a
//     step to the no-signal state; p = word_end_p = 1/3.06 and q = over_end_q = 1/24 (counted constants).
//
// One piece, in this order (plan §2.7):
//   1. At the channel's first piece there is one story: no signal, score 0, empty text.
//   2. Every story continues and ends. In a mark: the piece joins the block's sum; a full block is finished and the
//      next opened; the story continues; it also ends here as a dot and as a dash, each a successor in a gap at the
//      child node with the block finished and its posterior as the level, and, where the child holds a symbol, a
//      successor in the no-signal state that ends the over after that letter. In a gap: the story continues; it also
//      ends here as each kind allowed, each a successor that starts a mark at the next piece: an element gap keeps the
//      node; a letter or word gap pays the letter at the root and, for a word gap, adds a word space; each story that
//      starts a letter also pays ln(1/(1 - epsilon)) for not restarting. No signal: the story continues, paying
//      ln(1 - P) = -1.0e-4 nats per piece, where P = over_rate_per_s x piece_s = 1e-4 is the probability that an over
//      starts at a given piece of silence (a birth from silence pays ln P = -9.210 nats, step 3). Every successor
//      starts its next element at the next piece.
//   3. Births. From silence: while the no-signal story lives, a newborn starts a mark at the next piece, with the
//      continuing no-signal story's score (the one that was silent at this piece) plus ln P plus ln(1/62), its text
//      with a word space after it if it is not empty, its speed, factor and level open. Rescue: where at least one
//      story starts a letter, one newborn starts that mark, from the best such letter-starting successor (whether or
//      not that successor is created): its score less ln(1 - epsilon) plus ln epsilon (6.908 nats behind it, epsilon =
//      rescue_epsilon = 1e-3), its text, its speed, factor and level open.
//   4. Merging: successors created at the same piece in the same state are merged, keeping the better score (of equal
//      scores, the earlier created), its text and its level. The state is: activity; key up or down; pieces in the
//      current element; speed or open; factor or open; tree node. Continuing stories never collide, since a story's
//      element began at the piece its state was created, except in the no-signal state, which has no clock: there is at
//      most one no-signal story, and the continuing one merges with the over's ends created at the piece.
//   5. Pruning: every story more than beam_nats behind the best is dropped; if more than `ceiling` remain, the
//      `ceiling` best are kept, ties broken by the state, then by creation order (a counter), so that a run depends on
//      no container's order and no address. Every successor with a finite score is offered; one already more than
//      beam_nats behind a score known at the piece is not created, since the beam would drop it (which changes no
//      output: a successor the beam drops survives nothing, and merging keeps the better of two, so the stories after
//      pruning, their order and the statistics are the same; detail::SearchOptions turns the skip off for the tests
//      that show it).
//   6. Scores are held relative to the best.
//   7. best_text() gives the best story's text, each symbol with its start and end times and the probability 1; if the
//      best story stands at a node holding a symbol (its current gap's node, or the node its current mark started
//      from), that symbol is its last letter.
// Times: a letter's start_s is the start of its first mark (the end time of the piece before it), its end_s the end of
// its last mark; a word space spans its gap, from the end of the mark before it to the start of the mark after it; an
// over's opening space has start_s = end_s = the start of the over's first mark.
//
// Four points the plan leaves open, decided here:
//   1. A birth from silence comes from the continuing no-signal story, the one that was silent at the piece, before it
//      merges with the over's ends created at that piece (births, step 3, come before merging, step 4); so a new over
//      follows the end of the last one by at least one silent piece.
//   2. A successor's score at the piece it is created is its parent's score without the current element's timing (for a
//      mark, with the block finished), plus ln P of the element that ended and the costs paid at that end. A gap's kind
//      cost is paid from the gap's first piece on, inside the merged gap's maximum: at tau = 1, where every S is 1, the
//      cheapest allowed kind's cost.
//   3. A rescue newborn's parent is the best of all the letter-starting successors of the piece, including those the
//      skip of step 5 does not create (their scores are computed before the skip), so the skip changes no birth.
//   4. The Doppler spread f_D that sets B and the level's prediction is genie_fading_spread_hz when it is set, since
//      the configuration defines it as used in place of fading_spread_hz.
//
// Known limitation (measured; the search as designed, the edge question with the owner): the search does not follow a
// step from 20 to 30 WPM at +20 dB S500 (plan §5, item 2; search_test.cpp, FollowsASpeedStep, a strict expected
// failure). At each mark's start the channel filter's step response (about 10 ms, with the test's 5 ms raised-cosine
// keying edges) leaves a piece at partial amplitude: 0.0267 FS against 0.0615 FS for a full piece. Under a level about
// 20% uncertain (relative standard deviation), as predicted across a 124 ms gap at f_D = 1 Hz, a story that starts its
// mark on that piece reads it as a partial fade and leads by about 25 nats at that piece; one piece later the story
// that starts the mark one piece later leads by 27 to 34 nats, since a coherent block of the weak and the full piece
// falls short of the learned amplitude. A line behind the leader keeps both starts only while within about 8 nats of it
// at a letter start (40 nats of beam, less the 25-nat swing and about 7 nats of letter-start costs: ln 62, the gap kind
// and ln P), so a rescue newborn, born about 15.5 nats behind and gaining about 0.5 nats per element after the step, is
// dropped by the beam before it can lead. The swing grows in proportion to the carrier's power (S500 as a power ratio),
// so the failing band moves with the beam: at 40 nats the step fails at +20 to +24 dB S500 (and decodes at +10 to +19
// and +25 to +28 dB S500), at 80 nats at +23 to +25 dB S500; a ceiling of 17 000 stories changes nothing; stations at a
// constant speed decode exactly at +19 to +24 dB S500. The design places a mark's edges on piece boundaries with no
// filter delay (design notes, topic 4, item 2).
//
// Work saved without changing any output. The fist's maxima over open speeds and factors are computed once per tau as
// the search first needs them (each the plain maximum of Fist's values). Where a gap with an open factor ends between
// letters or words, its successors are evaluated outward from the factor with the largest ln P wherever the table found
// ln P unimodal in the factor (each entry is checked when it is built; an entry that is not is evaluated whole): on
// each side ln P only falls, so the last value computed bounds the rest; a factor whose successor could not beat the
// score its slot already keeps would lose the merge, and is passed over without computing its ln P, and a side ends at
// the first factor the beam would drop, or once the bound itself is beyond the beam, since every factor further out
// would be dropped too. An offer that loses its merge costs a compare. The statistics (SearchStats) are kept only when
// config.stats is true and never change the decoding. A Search is deterministic: the same pieces give the same text and
// statistics.
#pragma once

#include "kz4ap/decoder.hpp"
#include "kz4ap/viterbi/config.hpp"
#include "kz4ap/viterbi/level.hpp"
#include "kz4ap/viterbi/observation.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kz4ap::viterbi {

// How a newborn line (a newborn and its descendants) began.
enum class LineKind {
    Start,    // line 0: the channel's first story, in the no-signal state
    Silence,  // a newborn from silence: an over starts
    Rescue,   // a rescue newborn: the sender begins a new story at a letter start
};

// Which step removed a line's last story, or Alive while it has one.
enum class LineEnd { Alive, Merge, Beam, Ceiling };

// What became of one newborn line.
struct NewbornLine {
    double birth_s = 0.0;  // s: the time of the piece at which it was created (line 0: the first piece's)
    LineKind kind = LineKind::Start;
    // s: the first piece after whose pruning one of its stories was the best
    std::optional<double> first_led_s;
    // nats: how far its best story was behind the best, at most, after the pruning of each piece from its birth until
    // it first led, or over its life if it never led; at its birth piece, its newborn's own deficit even where the
    // newborn did not survive that piece
    double largest_deficit_nats = 0.0;
    LineEnd end = LineEnd::Alive;
    std::optional<double> end_s;  // s: the piece at which its last story was removed
    // its last story was removed before any story of the line had left the newborn's first mark (false for line 0)
    bool ended_in_first_element = false;
    friend bool operator==(const NewbornLine&, const NewbornLine&) = default;
};

// The search's instrumentation, kept only when config.stats is true.
struct SearchStats {
    std::int64_t pieces = 0;          // pieces searched
    std::int64_t ceiling_pieces = 0;  // pieces at which the ceiling dropped at least one story
    // pieces by the number of live stories after pruning, in bins of 10 stories: 0-9, 10-19, ...
    std::vector<std::int64_t> live_histogram;
    // stories: the mean and the largest live count after pruning in each 0.1 s (round(0.1 s / piece_s) = 50 pieces),
    // the last entry over the pieces of its window searched so far
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
    bool key_down;     // in a mark (Morse only)
    int tau;           // pieces in the current element: 0 for one that starts at the next piece, and for no signal
    int speed;         // 0 ... speed_count - 1, or -1 while open (and for no signal)
    int factor;        // 0 ... factor_count - 1, or -1 while open (and for no signal)
    int node;          // a mark: the node it started from; a gap: the node of the mark before it; -1 for no signal
    double score_nats;  // relative to the best: 0 for the best, at most beam_nats behind
    std::int64_t line;  // its newborn line
    Level level;       // its level as stored: learned from its last finished block, not predicted
};

namespace detail {

// Settings of the search that change no output, for the tests that show it.
struct SearchOptions {
    // Do not create a successor already more than beam_nats behind a score known at its piece (plan §2.7, item 5).
    bool skip_successors_beyond_beam = true;
    // If set, counts the open factors' scans that stopped short of an end of the factor grid (a letter or word gap with
    // an open factor whose outward scan from its best factor ended at a factor not created), so that a test can show
    // that the scan cut something.
    std::int64_t* cut_factor_scans = nullptr;
};

// A candidate story at a piece, as merging and pruning see it.
struct Ranked {
    std::uint64_t state;     // the story's state, packed so that its order is one fixed order of the states
    double score_nats;
    std::uint64_t creation;  // its creation order
};

// The pruning order: the better score first, then the lower state, then the earlier creation.
bool ranks_before(const Ranked& a, const Ranked& b);

// Merging (plan §2.7, item 4): of the candidates in one state, the one with the better score is kept (of equal scores,
// the earlier created). The indices of those kept, in the order given. The search merges by the same rule.
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
    // c: sum_noise_hz's c(m), Hz, at index m = m channel samples (c(0) = 0), covering at least the longest block in
    // samples: samples_per_piece x B, or samples_per_piece x the flat prior's max_pieces when B is unlimited (f_D = 0);
    // prior: the flat prior of this configuration and channel filter, whose samples_per_piece is the piece's. Throws
    // std::invalid_argument for no prior, a c too short or not positive and finite from m = 1, and a configuration the
    // search cannot use: a beam that is not positive, a ceiling below 1, more than 255 speeds or factors, a P
    // (over_rate_per_s x piece_s), epsilon, p or q outside (0, 1), a quadrature order outside 2 ... 20, or a block
    // shorter than one piece; and what Fist and block_length_s refuse.
    Search(const ViterbiConfig& config, std::span<const double> c, std::shared_ptr<const FlatPrior> prior,
           detail::SearchOptions options = {});
    ~Search();
    Search(Search&&) noexcept;
    Search& operator=(Search&&) noexcept;
    Search(const Search&) = delete;
    Search& operator=(const Search&) = delete;

    // One piece: piece_fs, the sum of samples_per_piece channel samples (FS); t_s, its time, the end of its last sample
    // (s), computed by the caller from one expression, t_k = t_first + samples_per_piece x (k + 1) / r, never from sums
    // of increments; n0_fs2_per_hz, the hiss N0 (FS^2/Hz). Throws std::invalid_argument, before changing anything, for
    // an N0 that is not positive and finite, a t_s that is not finite or not later than the previous piece's, and a
    // piece that is not finite.
    void step(std::complex<double> piece_fs, double t_s, double n0_fs2_per_hz);

    // The best story's text (plan §2.7, item 7): " " is a word space; each symbol's probability is 1.
    std::vector<DecodedSymbol> best_text() const;

    const SearchStats& stats() const;

    // For tests: the live stories after the last piece's pruning, and each one's state and score, in the order of their
    // states (the order of detail::Ranked::state).
    std::size_t live() const;
    std::vector<StoryView> live_stories() const;
    // nats: the sum of the best scores subtracted so far; a story's score against "noise only, throughout" is its
    // relative score plus this.
    double normalization_nats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace kz4ap::viterbi
