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

// A correction of a channel's published text (the bank decoder; Envelope and Matched never correct). The channel's
// characters form one list, indexed from 0 in the order they were published; a correction keeps the first
// from_index characters and replaces everything after them by chars. A consumer applies an update's chars first
// (appended to the list) and then its corrections, in order, clipping from_index to the list's length; chars are
// the channel's characters from from_index on as they stand after the update (so they include characters the same
// update appended after the correction was made).
struct TextCorrection {
    std::size_t from_index = 0;
    std::vector<DecodedSymbol> chars;
    double t_s = 0;      // when the decoder made the correction, s (stream time)
    std::string reason;  // the bank's reason: "switch", "rekey" or "timeout"
};

struct DecodeUpdate {
    std::vector<DecodedSymbol> chars;  // characters newly published, appended to the channel's list
    std::vector<TextCorrection> corrections;  // applied after chars, in order (always empty for Envelope and Matched)
    float wpm = 0;
    float confidence = 0;  // running average of recent character probabilities
    std::optional<double> freq_offset_hz;  // the station's offset from its channel's center, Hz, if the decoder tracks it
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
};

}  // namespace kz4ap
