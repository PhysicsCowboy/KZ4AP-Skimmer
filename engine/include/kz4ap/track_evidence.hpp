// Whether a track holds keyed Morse: the tests behind a track's retirement and behind showing its text. Two kinds of evidence,
// both in nats, both ratios of likelihoods (docs/decision-log.md, 2026-10-30, has the measurements):
//
// 1. The signal: the decoder's best Morse story against an on-off key with no timing and no text at the same level
//    (KeyingEvidence, decoder.hpp), over the last kWindowS:
//        R = (dS - max(0, dF)) / W, nats/s.
//    Keyed Morse earns its timing and R grows with the level; FT8, a carrier and speech do not.
//
// 2. The text: a sequential test on the symbols of the best story (Wald's). Each symbol adds ln(q(s) / p(s)), p the on-air prior
//    (viterbi/symbol_prior.hpp) and q an alternative; the sum is floored at -kTextFloorNats, so that a station that later fades
//    can be seen to read junk. Two alternatives, each with its own sum:
//      - tiny pieces: what a wide signal or noise reads as, one burst a letter, E, I, T ...: q is the share measured on the
//        junk of a real recording (a heuristic fit);
//      - garbage: every symbol alike, 1/62 (the tree's symbols): what a fast irregular signal reads as.
//    Under the prior, a sum reaches +x with probability at most e^-x; under the alternative, it reaches -x with probability
//    at most e^-x (derived: a likelihood ratio is a martingale). The thresholds below are chosen from those bounds, and are
//    heuristics: the bounds hold for symbols drawn independently from the models, and real text and real junk are neither. On the
//    air, far more than e^-6 of the junk is shown (measured, on real 40 m: 17% and 9% of the rows above +40 kHz of the center).
#pragma once

#include "kz4ap/decoder.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace kz4ap {

struct TextEvidence {
    double tiny_nats = 0;     // the sum against tiny pieces, as it stands; positive for junk
    double garbage_nats = 0;  // the sum against garbage, as it stands; positive for junk
    double lowest_tiny_nats = 0;     // the lowest the first has been
    double highest_garbage_nats = 0;  // the highest the second has been
    std::size_t symbols = 0;  // not counting word spaces
    // Text that a list may show: it has left the tiny pieces behind, at an error of e^-kPlausibleNats, and never read as garbage.
    bool plausible() const;
};

inline constexpr double kTextFloorNats = 12.0;  // Heuristic: 10 to 20 tiny pieces from the floor to a verdict
// Heuristics, by the bound above, which holds only for the models: e^-6 = 0.25% of model junk shown, not of real junk (see above).
inline constexpr double kPlausibleNats = 6.0;
inline constexpr double kTinyJunkNats = 8.0;     // Heuristic likewise: e^-8 = 0.03% of text drawn from the prior; real text never reached +5.5 nats
inline constexpr double kGarbageJunkNats = 6.0;  // Heuristic likewise: e^-6 = 0.25% of text drawn from the prior

// The evidence of a text, word spaces skipped. A symbol the prior lacks counts at the prior's floor.
TextEvidence text_evidence(std::span<const std::string> symbols);

// The same evidence as text_evidence, kept up as a text grows: only new symbols are added, so that reading a long track's text each
// second does not grow with its length. A correction goes back to the nearest saved state before it, kCheckpointEvery symbols
// apart at most: a correction reaches a few symbols back, and the sum depends on the order only through its floor.
class RunningTextEvidence {
public:
    // The symbols from this index on have changed (a decoder's correction).
    void changed_from(std::size_t index);
    // The evidence of the text as it now stands; what lies before the changed index must be as it was.
    const TextEvidence& update(std::span<const std::string> symbols);

private:
    static constexpr std::size_t kCheckpointEvery = 256;  // symbols; each saved state is about 50 bytes
    void back_to(std::size_t index);
    TextEvidence sum_;
    std::size_t read_ = 0;                  // symbols read so far, word spaces included
    std::vector<TextEvidence> checkpoints_;  // the state after 0, kCheckpointEvery, 2 kCheckpointEvery ... symbols read
};

// How a track ended, for whoever keeps its text.
enum class TrackEnd {
    Faded,      // the detector saw no signal for its death time, or the track was pushed out by the cap
    Merged,     // another, older track holds the same peak
    NotMorse,   // retired: its evidence says no keyed Morse
    Quiet,      // retired: it has earned nothing for a long time
    Shed,       // the pipeline fell behind and this track had the least evidence
};

// One track's judge. The engine gives it the decoder's evidence once a second; it answers keep or retire.
class TrackJudge {
public:
    struct Verdict {
        bool retire = false;
        TrackEnd end = TrackEnd::Quiet;
        double hold_s = 0.0;  // how long the place stays closed to a new track, s, before repeats (SignalDetector::retire)
    };

    // s: the signal's window, and how long a track that has shown nothing may stay. All heuristics chosen by measurement.
    static constexpr double kWindowS = 12.0;
    // s: no verdict on the text before this: the rate above needs a few seconds to mean anything.
    static constexpr double kFirstVerdictS = 4.0;
    // nats/s: R above this is keyed Morse whatever the text reads (a generated contest exchange can be all E, T, I, M, U).
    static constexpr double kKeyedRate = 3.0;
    // nats/s: R below this, with held_to_it, is not Morse: FT8 and carriers lie below it. It is not a margin below the real
    // stations, some of which fall below it over a full window (measured on a fresh 30 min of real 40 m: 9 of 57 tracks at real
    // stations' places, as low as -430 nats/s). They stay because they have read as plausible and earn too much for the shortfall
    // to count (kStrongRate, kShortfallShare); the text test does the work for most of them.
    static constexpr double kNotKeyedRate = -8.0;
    // nats/s: a shown track is held to that rate only if it earns at least this: at a low level a marginal over every key path
    // beats the one best story whatever the signal (generated, +6.5 dB S500 at 42 WPM: 230 nats/s and -14 nats/s), while at this
    // level the real stations earn their timing.
    static constexpr double kStrongRate = 1000.0;
    // 1: and its shortfall must be this much of its earnings: a strong station's story slips by tens of nats/s, out of hundreds
    // of thousands, for a moment (generated: 390 of 728 000).
    static constexpr double kShortfallShare = 1e-3;
    static constexpr double kQuietNewS = 8.0;       // s: a track with nothing plausible yet must earn kQuietNewNatsPerS in this
    static constexpr double kQuietNewNatsPerS = 5.0;  // nats/s: noise's reading of itself lies below it
    static constexpr double kUnprovenS = 14.0;      // s: the longest a track may read nothing plausible
    static constexpr double kQuietOldS = 30.0;      // s: a shown track that earns nothing this long has gone (or its station listens)
    static constexpr double kQuietOldNatsPerS = 1.0;  // nats/s
    static constexpr double kHoldS = 8.0;           // s: how long a place stays closed after its first retirement

    // Once a second of stream, with the evidence as it stands and the text as published.
    Verdict update(const KeyingEvidence& evidence, std::span<const std::string> symbols);
    // The published text has changed from this index on (a correction), before the next update.
    void text_changed_from(std::size_t index) { text_.changed_from(index); }
    bool proven() const { return proven_; }
    // nats/s: what the best story has earned over the last window: which tracks a pipeline that falls behind gives up first.
    double earning() const { return earning_; }

private:
    static constexpr std::size_t kHistory = 32;  // s: at least the longest window, at one entry a second
    std::array<KeyingEvidence, kHistory> history_{};
    std::size_t count_ = 0;
    RunningTextEvidence text_;
    bool proven_ = false;
    double earning_ = 0.0;
};

}  // namespace kz4ap
