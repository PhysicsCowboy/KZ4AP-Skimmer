#pragma once

#include "kz4ap/types.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kz4ap {

struct DecodedSymbol {
    std::string text;   // "A", "?", "<SK>"; " " marks a word space, "*" a pattern with no Morse code
    float probability;  // the decoder's belief that text is right, 0..1
    double start_s;
    double end_s;
};

// A correction of a channel's published text (the bank and segmental decoders; Envelope and Matched never correct). The channel's
// characters form one list, indexed from 0 in the order they were published; a correction keeps the first
// from_index characters and replaces everything after them by chars. A consumer applies an update's chars first
// (appended to the list) and then its corrections, in order, clipping from_index to the list's length; chars are
// the channel's characters from from_index on as they stand after the update (so they include characters the same
// update appended after the correction was made, and any kept character after from_index). from_index is never
// past the first character whose text the correction changed, so the consumer's list is always the decoder's.
struct TextCorrection {
    std::size_t from_index = 0;
    std::vector<DecodedSymbol> chars;
    double t_s = 0;      // when the decoder made the correction, s (stream time)
    // the segmental decoder's reason: "story" (its best story changed its mind), or with its margins' variant "confidence" (a margin changed, the text not). The bank's: "switch", "rekey" or "timeout"; "resync" for one the BankDecoder adds when the list's text
    // changed without a correction (measured: never on the suite, 0 of 31 205; see docs/signal-processing.md
    // appendix A.8c)
    std::string reason;
    // t_s minus the start of the first replaced character, s: at most the correction reach (20 s), except for a
    // "resync", whose first replaced character can be an older kept one that a same-text replacement moved, so its
    // reach has no bound but the channel's start
    double reach_s = 0;
};

struct DecodeUpdate {
    std::vector<DecodedSymbol> chars;  // characters newly published, appended to the channel's list
    std::vector<TextCorrection> corrections;  // applied after chars, in order (always empty for Envelope and Matched)
    float wpm = 0;
    float confidence = 0;  // running average of recent character probabilities
    std::optional<double> freq_offset_hz;  // the station's offset from its channel's center, Hz, if the decoder tracks it
};

// A decoder's running evidence, nats, summed from the channel's start, each against key-up throughout at the channel's noise
// (a sample of key-up scores 0): what the engine reads to decide whether a track holds keyed Morse (track_evidence.hpp).
struct KeyingEvidence {
    double morse_nats = 0;     // the best Morse story: its timing and text included
    double free_key_nats = 0;  // an on-off key with no timing and no text at the same level: a two-state chain, 5 changes a second
};

// Decodes one channel's baseband stream into characters.
class Decoder {
public:
    virtual ~Decoder() = default;
    // samples: channel baseband; t0_s: time of samples[0].
    virtual DecodeUpdate process(std::span<const Sample> samples, double t0_s) = 0;
    // Emits any partly received character (end of input, or the track died).
    virtual DecodeUpdate flush() = 0;
    // Forgets all state (e.g. after a gap in the input).
    virtual void reset() = 0;
    // Where the signal detector says the station is, Hz from the channel's center (option 1,
    // owner decisions 2026-09-29: the detector decides which station a channel follows). The
    // engine calls it before every block. Decoders that do not track frequency ignore it.
    virtual void set_frequency_anchor_hz(double /*offset_hz*/) {}
    // The density of the noise around the channel, N0 (FS^2/Hz), from the engine's hiss estimator (the new decoder's
    // HissEstimator, viterbi/hiss.hpp; 0 before it has a frame and on exact zeros). The engine calls it before every
    // block, as it does the frequency anchor, when the decoder is the new decoder (FrontEnd::Viterbi); the other
    // decoders estimate their noise themselves and ignore it.
    virtual void set_noise_density_fs2_per_hz(double /*n0_fs2_per_hz*/) {}
    // The band's latest spectrum (SpectrumFrame::power_fs2), and the index in it of the channel's center: what stands beside the channel. The
    // engine calls it before every block that is not a replay's, when the decoder is the segmental decoder.
    virtual void set_band_spectrum(std::span<const float> /*power_fs2*/, int /*center_index*/) {}
    // The evidence above, if this decoder has any.
    virtual std::optional<KeyingEvidence> keying_evidence() const { return std::nullopt; }
    // The text as published so far (the consumer's list above), if this decoder keeps it.
    virtual std::span<const std::string> published_symbols() const { return {}; }
    // What the last block of process() gave at the filter whose text goes out: the mean of its envelope samples' power, e^2, FS^2 (the
    // keying the decoder hears, which the engine compares between neighbors: keying_coherence.hpp). Negative if this decoder has
    // none, or the block gave no sample.
    virtual double block_envelope_fs2() const { return -1.0; }
};

}  // namespace kz4ap
