// The new decoder's search (plan §2.7; design notes, topic 5): a time-synchronous Viterbi beam search over stories, one
// piece at a time (piece_s, 2 ms: the sum of samples_per_piece = 3 channel samples at 1 500 samples/s). A story is one
// continuous account of what has been sent on the channel since its start: no signal, or Morse in a mark (key down) or
// a gap (key up) that has lasted tau pieces, at a speed ("open" until the story takes it), at a node of the Morse tree
// (morse_tree.hpp), with its own level (level.hpp) and its own text, a chain of letters and word spaces shared between
// stories. Spacing is standard: there is no Farnsworth factor (the owner's decision D14 of 2026-10-21;
// Farnsworth-spaced signals decode poorly until a later fix, docs/backlog.md, "Farnsworth spacing in the new decoder").
//
// The score. A story's score, nats, is the log-likelihood of its story and the signal so far against the story "noise
// only, throughout", so a gap piece adds 0. Scores are held relative to the best story's (normalization_nats() is the
// sum of the best scores subtracted, so a story's own score is its relative score plus that sum). Every point is booked
// at the piece it is earned (design notes, topic 5, item 9):
//   - Evidence. A mark is scored in blocks: a block is a coherent sum S of a mark's pieces, at most
//     B = floor(block_length_s(f_D, block_loss_db) / piece_s) pieces (195 at f_D = 1 Hz and 0.5 dB of signal energy:
//     390.37 ms; with f_D = 0, no limit), f_D being genie_fading_spread_hz if set, else fading_spread_hz. The pieces
//     count as independent at the noise's long-run rate, N0 x rate per channel sample, rate being c's last increment
//     (1 500 Hz on the channelizer's filter), with N0 the hiss passed with the current piece (FS^2/Hz) for the whole
//     running block. That is exact for long blocks (c(m) / m tends to the rate) and credits a short block less than its
//     own correlated noise would (derived: a weighted first block of 6 pieces, the 100 WPM dot, 5.8% less; first blocks
//     of 1 to 4 pieces 53% to 12% less; observation.hpp). A block's own c(m) is not used, because it ignores the noise's
//     correlation with the samples before the block: one piece's three samples are nearly fully correlated
//     (c(3) = 2 104.66 Hz = 0.93 x 9 R_0), so a block's first full piece earned |S|^2 / V = 213.8 at +20 dB S500 against
//     100.0 per piece later (derived; c(3) against 4 500 Hz per piece in a long block), and starts that skipped a weak
//     first piece gained by it. A mark's first block is the matched filter to the pulse as received (rise_fractions,
//     observation.hpp): S = sum_j g_j x_j over its pieces x_j, with the rise's fractions g_j (0.193, 0.614, 0.943 and
//     1.019 at 48 kHz with a keying edge of keying_rise_s = 5 ms; g_j = 1 past the rise), the flat prior's
//     (FlatPrior::weight), so that a weak first piece adds little and a start that skips it gains nothing; its
//     m_signal = samples_per_piece x (g_1^2 + ... + g_n^2) and the noise's share of V is N0 x rate x m_signal. A later
//     block is the plain sum: m_signal = m = samples_per_piece x n and the noise's share N0 x rate x m. V adds the
//     systematic term to the noise in quadrature (below, "The systematic term"). While a mark lasts, its current
//     block's evidence so far is part of the score at every piece; a finished block's evidence enters the score for
//     good and its posterior becomes the story's level. Every story in a mark makes exactly one scoring call per piece,
//     and its continuation and all its successors at that piece use that call's evidence and level: a learned story
//     calls score_block with its level predicted to the piece's time t_k (the update stamps a level with its block's
//     end, so the prior of the block in hand is predicted end to end; a predicted level is used once and never
//     stored), starting Newton's method from the previous piece's mode in the same block; a newborn's first block, of
//     at most min(B, the flat prior's max_pieces) pieces, calls score_first_block, and the level is learned when it
//     finishes.
//   - Durations, in the hazard form (fist.hpp): a story still in its mark or gap at tau pieces has paid ln S(tau), one
//     that ends there ln P(tau) instead. A mark keeps the better of ln S_dot(tau) and ln S_dash(tau), each plus its
//     label's text cost at the node (0 where that child exists, -infinity where not), and ends into a dot and a dash,
//     each with its own ln P. A gap keeps the best of the kinds allowed at its node, each with its own cost and
//     survival, and ends as each kind; a letter gap's median is 3 dots and a word gap's 7 (fist.hpp). A newborn's first
//     mark keeps the best over the speeds and both labels of ln(1/speed_count) + ln S_label(s, tau) (3.466 nats for 32
//     speeds) and ends into every speed and label with a finite ln P, each paying ln(1/speed_count) and its own ln P.
//   - Text, by look-ahead, with every symbol equally likely (plan §2.6): a letter costs ln(the tree's symbols) =
//     ln 62 = 4.127 nats when it starts. After a mark at a node, the gap is between elements if the node has children
//     (cost 0); if it holds a symbol, between letters (ln(1/(1 - p)) = 0.396 nats) or between words
//     (ln(1/(p (1 - q))) = 1.161 nats), and the over may end, ln(1/(p q)) = 4.296 nats, taken at the mark's end as a
//     step to the no-signal state; p = word_end_p = 1/3.06 and q = over_end_q = 1/24 (counted constants).
//
// The search runs lookahead_pieces() pieces behind the pieces it is given (2 at 2 ms, 4 ms: the systematic term's
// look-ahead, below): step holds each piece until that many more have come, then searches it with the time and N0 given
// with it, and finish searches those still held. One piece searched, in this order (plan §2.7):
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
//      with a word space after it if it is not empty, its speed and level open. Rescue: where at least one story starts
//      a letter, one newborn starts that mark, from the best such letter-starting successor (whether or not that
//      successor is created): its score less ln(1 - epsilon) plus ln epsilon (6.908 nats behind it, epsilon =
//      rescue_epsilon = 1e-3), its text, its speed and level open.
//   4. Merging: successors created at the same piece in the same state are merged, keeping the better score (of equal
//      scores, the earlier created), its text and its level. The state is: activity; key up or down; pieces in the
//      current element; speed or open; tree node. Continuing stories never collide, since a story's element began at
//      the piece its state was created, except in the no-signal state, which has no clock: there is at most one
//      no-signal story, and the continuing one merges with the over's ends created at the piece.
//   5. Pruning: every story more than beam_nats behind the best is dropped; if more than `ceiling` remain, the
//      `ceiling` best are kept, ties broken by the state, then by creation order (a counter), so that a run depends on
//      no container's order and no address. Every successor with a finite score is offered; one already more than
//      beam_nats behind a score known at the piece is not created, since the beam would drop it, except an over's end:
//      the continuing no-signal story reaches pruning only through the no-signal state, where the over's ends merge
//      with it, so they are always offered there (at most two compares per mark story and piece). This changes no
//      output: a successor the beam drops survives nothing, and merging keeps the better of two, so the stories after
//      pruning, their order and the statistics are the same (detail::SearchOptions turns the skip off for the tests
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
// The mark's rise (design notes, topic 4, item 2, as amended 2026-10-20) and what remains. With the pieces at the
// long-run rate and the matched filter in a mark's first block, the start aligned with the rise wins and keeps winning,
// and a newborn whose level is free gains nothing at a strong first piece. Measured before the systematic term, on
// noise-free pieces at +20 dB S500 under a level about 20% uncertain (relative standard deviation, predicted across
// 124 ms at f_D = 1 Hz): a start one piece late trails by about 41 nats one piece after the edge and by about 59 nats
// six pieces after it (with a block's own c(m) and no rise it had led by 48.8 and about 23 nats), and a start one piece
// early scores within 0.4 nats of the aligned one. Measured on the tests' signals (search_test.cpp), without the
// Farnsworth factor, with and without the systematic term: the step from 20 to 30 WPM decodes at every level from +10
// to +28 dB S500 on three seeds at beams of 40 and 80 nats, and "PARIS PARIS" at 12, 25 and 40 WPM decodes exactly at
// every level from +10 to +28 dB S500 on three seeds (Search.FollowsASpeedStepAtEverySnr holds the beam of 40 nats).
// What remains:
//   1. The edge's position within a piece is averaged, not known: at 48 kHz a mark's first piece holds 0.048 to 0.384
//      of a full piece, depending on where the edge falls in it, and the template has their average, 0.193. The
//      systematic term's edge share, E_n, carries that spread into the error budget (below).
//   2. The keying edge's shape is assumed: the generator's 5 ms raised cosine (keying_rise_s); real transmitters vary.
//   3. The ceiling binds in the gaps of weak signals. With the Farnsworth factor, at +10 dB S500 on seed 12 the step
//      misread one letter pair ("P<AR>IS", CER 0.057), because the ceiling dropped the rescue newborns born in the
//      word gap before the step (a ceiling of 2 500 stories decoded every point; measured). Without the factor that
//      point decodes exactly at both beams, though the ceiling still drops stories at 294 of the 500 pieces from 9.0
//      to 10.0 s there (308 with the factor; measured): why it now decodes was not traced. The ceiling is the owner's
//      question.
//   4. The fall at a mark's end is not modeled, and a gap piece adds 0 whatever it holds, so the energy left in the
//      fall's tail after the mark's last piece is explained by no story that keeps the mark: a piece holding the
//      fraction f of a full piece's carrier sum carries (3 f a)^2 / (4 500 Hz x N0) = f^2 x 10^(S500 / 10) nats of it
//      (derived: a^2 = 10^(S500 / 10) x N0 x 500 Hz). A story that ends the over after the mark and starts a new over
//      of one piece in the tail can claim that energy. That over alone costs 34.3 nats (the over's end 4.30, the
//      birth's ln P 9.21, the letter 4.13, the speed's prior 3.47 and a one-piece dot at 100 WPM 13.17), and the story
//      also pays for what follows it, so the claim wins only where the piece carries at least about 34 nats (derived).
//      Without the systematic term that broke strong stations. Through the decoder ("PARIS PARIS" at 25 WPM on DEV2's
//      noise, N0 the noise's own; in each run every keying edge at one of 16 positions within its piece, steps of 6 of
//      its 96 input samples at 48 kHz), 16, 16, 13, 10, 2, 0 and 0 positions decoded exactly at +30, +36, +40, +45,
//      +50, +55 and +60 dB S500, the first failures at +38 dB S500; from +44 dB S500 the texts broke into short words,
//      mostly of E and T ("T EE EE ET E T EE E ..." at +60 dB S500), and a fading spread of 5 Hz instead of 1 Hz failed
//      alike (measured; the results record, section 1.7). With the systematic term (below) a newborn's one-piece first
//      block claims at most g_1^2 f^2 / (E_1 + floor^2 g_1^2) = 97 f^2 nats where the mark just seen sets C^2
//      (derived), and every position decodes exactly at every one of those levels (measured;
//      ViterbiDecoder.StrongStationsAtEveryEdgePosition, a full-sweep test, and ViterbiDecoder.ExtremeLevels at
//      +60 dB S500). DEV2's stations lie at or below +20 dB S500 (neighbors to +26 dB S500); real bands hold stronger
//      ones.
//
// The systematic term (the owner's decision D15 of 2026-10-21). The model is close to the signal, not exact: the
// template of a mark's first block is the rise averaged over where the edge falls in its piece, the channel filter
// rings before a mark where the template has nothing, and real signals add chirp, clicks, phase noise and fading within
// a block. At ordinary levels the noise hides the mismatch. At +60 dB S500 a full piece is worth 10^6 nats (its
// f^2 x 10^(S500 / 10) with f = 1), so a mismatch of 1% of one piece's carrier amplitude is worth about 100 nats, more
// than inventing a one-piece over costs (34.3 nats). So the model's systematic uncertainty is added in quadrature to
// the noise in the error budget of every block of a mark:
//     V = N0 x rate x m_signal + C^2 (E_n + floor^2 x W_n)   (FS^2),
//   - W_n = m_signal / samples_per_piece, the sum of the block's squared weights (n in a later block);
//   - floor = systematic_floor, 0.02 of a full piece's carrier amplitude (placeholder, heuristic): what the template
//     leaves out, independent from piece to piece;
//   - E_n, in a mark's first block only (a newborn's or a learned story's; 0 in a later block): the template's own
//     systematic error at a mark's start, FlatPrior::template_error (rise_template_errors, observation.hpp), the mean
//     square over the edge's positions of the weighted sum's miss, the pieces' errors summed before squaring since one
//     edge makes them all (derived): 0.00037, 0.0095 and 0.0224 at n = 1, 2 and 3 pieces and 0.0210 from 7 on, at
//     48 kHz with 5 ms edges (measured);
//   - C^2 = (samples_per_piece x a)^2, a full piece's squared carrier sum, taken from the data, the same for every
//     story with this block and never a story's level: the larger of the channel's carrier power, the largest
//     |piece|^2 of the last min(B, the flat prior's span) pieces (195, 390 ms, at the defaults: the longest block a
//     story can hold, heuristic) and of the next lookahead_pieces(), and the block's own estimate,
//     (samples_per_piece |S| / m_signal)^2.
// The evidence keeps its form, Lambda = -b^2 + ln I0(2 b z) with this V in b and z: the carrier and noise alone are
// judged against the same error budget, as a least-squares fit treats a systematic error. A newborn's flat prior is
// read at z = |S| / sqrt(V), so its range in b, built for the noise's V, stands for levels higher than its bounds by
// 10 log10(V / the noise's share) in dB S500 (derived): it moves up with the strongest stations.
// Derived: at large SNR Lambda is about (|S|^2 - |S - m_signal a|^2) / V, so a hypothesis that lowers its own V raises
// its evidence. With a story's own level k a in V (k the level over the true one) a mark scores about
// (2k - k^2) z0^2 / (1 + k^2 rho), z0^2 its energy over the noise and rho the systematic share of V over the noise's at
// the true level, which is largest near k = 1 / sqrt(rho), not at k = 1: a 24-piece dot at +60 dB S500 (z0^2 =
// 2.2 x 10^7, rho = 1 340) scores about 6 x 10^5 nats at k = 0.027 against 1.7 x 10^4 at k = 1. The exact form, V(a)
// inside the level's integral, is dominated by that same a and grows as z0^2 / sqrt(rho) instead of being capped. With
// C^2 at the true carrier, a first block of n pieces has z^2 at most G_n^2 / (E_n + floor^2 G_n), G_n = g_1^2 + ... +
// g_n^2: 3.6 nats for one piece and 17.7 for two (below the 34.3 nats of an invented over), and a later block's
// z^2 / n at most 1 / floor^2 = 2 500.
// Measured (scratch runs on the grid above, in a copy of the branch; the results record, section 1.7), each part of
// C^2 alone fails: the channel's carrier power alone left an over's first mark uncapped, since the channel holds no
// recent carrier there ("E PARIS PARIS" from +40 dB S500); the block's own estimate alone over- or understated the
// carrier as the template sat on the rise, and so did a story's level (0 of 16 edge positions exact at every level from
// +30 to +60 dB S500, with either). Without the look-ahead, E_n summed over independent pieces left 3 of 16 positions
// exact at +60 dB S500, against 14 with E_n as one edge's.
// The look-ahead (measured): the two pieces before a mark's first template piece carry the channel filter's ringing,
// up to 0.046 and 0.022 of a full piece's amplitude (the third below 0.002); at +60 dB S500 a newborn read them as the
// end of a weak mark, worth about 87 nats, before the mark's power reached the window, and 2 of 16 positions failed.
// So the channel's carrier power also takes the next lookahead_pieces() = ceil(4 ms / piece_s) pieces, and the search
// runs that far behind the pieces it is given (2 pieces, 4 ms, at the defaults; 3 gave the same at +50 to +60 dB S500).
// The floor (measured without the look-ahead on the same grid, and at ordinary levels: the speed step of
// Search.FollowsASpeedStepAtEverySnr at beams of 40 and 80 nats and its constant-speed stations, and "TEST" and "TU"
// sent alone at +10 to +30 dB S500 on three seeds): 0 failed the strong grid from +45 dB S500 (two marks merged
// across a gap: a budget that does not grow with the block's length makes merging across g pieces pay when
// E_n > floor^2 g, derived) and lost a speed-step point and a one-word over; 0.01 lost two one-word overs at
// +30 dB S500; 0.05 was clean but costs as much evidence as 1.3 to 1.6 dB of S500 at +20 dB S500. At 0.02 a mark's
// evidence drops as if S500 were 0.05, 0.24 and 1.12 dB lower in a 195-piece later block at +10, +20 and
// +28 dB S500, and 0.12, 0.70 and 2.88 dB lower in a 24-piece first block (measured; 10 log10(1 + floor^2 x
// 10^(S500 / 10)) = 0.02, 0.17 and 0.98 dB of S500 for a later block, derived), and the search's CPU time stays within
// 1% of its own without the term (measured).
// A neighbor: C^2 is the channel's, so a strong neighbor inside the channel raises a weak station's term. A steady
// neighbor whose pieces are p, delta Hz away, leaves on average |p|^2 / (2 sin^2(pi delta piece_s)) in the power of a
// block sum of n pieces, while the floor's share of the term is floor^2 |p|^2 n, at most 2 n floor^2 of that residual
// (0.019 for a 24-piece block, 0.16 for a 195-piece one), and the edge's share at most 2 E_n (0.045; derived): where
// the term's share reaches the noise, the neighbor's own residual in the same block is on average at least 5 times
// larger.
// Measured: a station at +6 or +10 dB S500 with a keyed neighbor 0 to 40 dB stronger 40 to 200 Hz away, and cells like
// DEV2's E2 group's, never lost an exact text to the term (the results record, section 1.7).
//
// Work saved without changing any output:
//   - The maxima over an open speed (a newborn's first mark: its survival, the best over the speeds and both labels;
//     its ends, ln P per speed and label and their maxima) are kept in tables for the first `cap` taus, as the search
//     first needs them, and computed again the same way for any tau beyond. With a first-mark row of
//     2 x speed_count + 4 values of 8 bytes per tau (544 bytes per tau at 32 speeds),
//     cap = floor(4 000 000 bytes / the bytes per tau) (7 352 pieces, 14.704 s, at 32 speeds), so that one Search's
//     tables never hold more than 4 000 000 bytes of entries (3 999 488 at 32 speeds). The 4 000 000 bytes count the
//     entries only: the three tables' vector headers and the scratch row add 584 bytes (3 x 24 + 64 x 8, at 32
//     speeds).
//   - An offer that loses its merge costs a compare.
// The statistics (SearchStats) are kept only when config.stats is true and never change the decoding. They grow with
// the channel's time: about 96 bytes per newborn line (its record and its track), up to two lines per piece, about
// 350 MB per hour; they are meant for bench recordings, not for an engine that runs for hours. A Search is
// deterministic: the same pieces give the same text and statistics.
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
    // The taus whose open speed's maxima are kept in tables, pieces: 0 for the cap the 4 000 000-byte budget sets
    // (search.hpp, "Work saved"); a smaller cap computes more of them on demand.
    int open_timing_cap_pieces = 0;
    // If set, counts the skips: each time a successor, or a newborn's first mark's whole set of gap successors, was not
    // created because the beam would drop it, so that a test can show that the skip cut something.
    std::int64_t* skipped_successors = nullptr;
};

// A candidate story at a piece, as merging and pruning see it.
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
    // c: sum_noise_hz's c(m), Hz, at index m = m channel samples (c(0) = 0), covering at least the longest block in
    // samples: samples_per_piece x B, or samples_per_piece x the flat prior's max_pieces when B is unlimited (f_D = 0);
    // it is read only for its last increment, the noise's long-run rate; prior: the flat prior of this configuration
    // and channel filter, whose samples_per_piece is the piece's and whose rise weights a mark's first block. Throws
    // std::invalid_argument for no prior, a c too short or not positive and finite from m = 1, a long-run rate that
    // differs from the prior's (by more than 1e-9 of it), and a configuration the search cannot use: a beam that is not
    // positive, a ceiling below 1, more than 255 speeds, a P (over_rate_per_s x piece_s), epsilon, p or q
    // outside (0, 1), a quadrature order outside 2 ... 20, a block shorter than one piece, a systematic_floor outside
    // [0, 1], or a piece_s so short that the look-ahead would hold more than a million pieces; and what Fist and
    // block_length_s refuse.
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
    // piece that is not finite. The piece is held for the look-ahead and searched, with its own t_s and N0, once
    // lookahead_pieces() more have been given (or at finish), so after a step the stories, the text and the statistics
    // are those of the pieces searched: all but the last lookahead_pieces() given.
    void step(std::complex<double> piece_fs, double t_s, double n0_fs2_per_hz);

    // Searches the pieces held for the look-ahead, oldest first, each with the look-ahead of those held after it, so
    // that every piece given has been searched; pieces given later are held again. Call it at the channel's end,
    // before best_text() (ViterbiDecoder::flush does).
    void finish();

    // pieces: how many pieces the search holds back for the look-ahead, ceil(4 ms / piece_s), 2 at 2 ms.
    int lookahead_pieces() const;

    // The best story's text (plan §2.7, item 7): " " is a word space; each symbol's probability is 1.
    std::vector<DecodedSymbol> best_text() const;

    const SearchStats& stats() const;

    // For tests: the live stories after the last searched piece's pruning, and each one's state and score, in the order
    // of their states (the order of detail::Ranked::state).
    std::size_t live() const;
    std::vector<StoryView> live_stories() const;
    // nats: the sum of the best scores subtracted so far; a story's score against "noise only, throughout" is its
    // relative score plus this.
    double normalization_nats() const;
    // For tests: the entries of the shared text in use (letters and word spaces that a live story's text holds), and the
    // bytes the open timing's tables hold in entries, their vectors' capacities ("Work saved": at most 4 000 000).
    std::size_t text_entries() const;
    std::size_t open_timing_bytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace kz4ap::viterbi
