// The bank decoder behind the engine's Decoder interface (docs/signal-processing.md section 8c, "The bank decoder
// behind the engine"): one kz4ap::bank::BankChannel per channel, fed the channel's blocks mixed down by the
// engine's frequency anchor, its published characters and corrections returned as DecodeUpdates.
#pragma once

#include "kz4ap/bank/bank_config.hpp"
#include "kz4ap/bank/channel.hpp"
#include "kz4ap/decoder.hpp"

#include <complex>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kz4ap {

class BankDecoder final : public Decoder {
public:
    // rate_hz: the channel's sample rate, samples/s. residual_hz: the station's offset from the channel's center,
    // Hz, the anchor used until set_frequency_anchor_hz is called (the engine calls it before every block).
    BankDecoder(double rate_hz, const bank::BankConfig& cfg, double residual_hz);

    // Mixes the block down by the latest anchor (phase continuous across blocks and anchor changes, as the
    // prototype's streams.anchored_baseband) and pushes it to the bank. Returns the characters published since
    // the last call and the corrections made since then (see TextCorrection). t0_s: time of samples[0], s; the
    // first call's t0_s is the channel's time origin (the bank's times are counted from its first sample). After
    // flush() the samples are ignored.
    DecodeUpdate process(std::span<const Sample> samples, double t0_s) override;
    // End of the channel: the bank's last partial block, every branch's open character, and what that publishes.
    // Further calls return nothing.
    DecodeUpdate flush() override;
    // Starts a new bank channel: a new character list (indices start again at 0) and a new time origin.
    void reset() override;
    // The detector's frequency for the station, Hz from the channel's center (oracle: the label's).
    void set_frequency_anchor_hz(double offset_hz) override { anchor_hz_ = offset_hz; }

    const bank::BankChannel& channel() const { return *channel_; }

private:
    // Test-only seam (engine/tests/bank_decoder_test.cpp): reaches the bank's published list to force a "resync".
    friend struct BankDecoderTestAccess;

    void push_mixed(std::span<const std::complex<double>> u);
    void collect_appended();
    DecodeUpdate take_update();
    DecodedSymbol symbol(const bank::Char& c) const;

    double rate_;
    bank::BankConfig cfg_;
    double anchor_hz_;
    std::unique_ptr<bank::BankChannel> channel_;
    std::optional<double> origin_s_;          // the channel's time origin, s (the first block's t0_s)
    double phase_sum_hz_ = 0;                 // the running sum of each sample's anchor, Hz (numpy's cumsum)
    std::vector<std::complex<double>> u_;     // the block, mixed down, FS
    std::size_t appended_seen_ = 0;           // characters of the bank's Output::appended() already collected
    std::size_t corrections_seen_ = 0;        // the bank's corrections already returned
    std::size_t changes_seen_ = 0;            // the bank's Output::text_changes() already looked at
    std::vector<std::string> published_;      // the consumer's character list (texts), as the updates build it
    std::vector<DecodedSymbol> pending_;      // characters appended since the last update
    bool flushed_ = false;
};

}  // namespace kz4ap
