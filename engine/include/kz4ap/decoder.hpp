#pragma once

#include "kz4ap/types.hpp"

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

struct DecodeUpdate {
    std::vector<DecodedSymbol> chars;
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
