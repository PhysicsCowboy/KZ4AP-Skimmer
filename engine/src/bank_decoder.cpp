#include "kz4ap/bank_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

namespace kz4ap {

BankDecoder::BankDecoder(double rate_hz, const bank::BankConfig& cfg, double residual_hz)
    : rate_(rate_hz), cfg_(cfg), anchor_hz_(residual_hz), channel_(std::make_unique<bank::BankChannel>(cfg, rate_hz)) {}

DecodedSymbol BankDecoder::symbol(const bank::Char& c) const {
    // The bank gives no per-character probability: 1, as Envelope and Matched give a word space.
    const double origin = origin_s_ ? *origin_s_ : 0.0;
    return DecodedSymbol{c.text, 1.0f, c.start_s + origin, c.end_s + origin};
}

DecodeUpdate BankDecoder::process(std::span<const Sample> samples, double t0_s) {
    if (flushed_ || samples.empty()) return {};
    if (!origin_s_) origin_s_ = t0_s;
    // streams.anchored_baseband: phase[n] = 2 pi (cumsum(offset)[n] - offset[n]) / r, the phase before sample n's
    // own advance, with the cumulative sum carried across blocks (a continuous phase across anchor changes);
    // u = y exp(-j phase) = y (cos(-phase) + j sin(-phase)), the complex product written out as numpy computes it.
    const double offset = anchor_hz_;
    u_.resize(samples.size());
    for (std::size_t n = 0; n < samples.size(); ++n) {
        phase_sum_hz_ += offset;
        const double phase = 2.0 * std::numbers::pi * (phase_sum_hz_ - offset) / rate_;
        const double er = std::cos(-phase);
        const double ei = std::sin(-phase);
        const double yr = samples[n].real(), yi = samples[n].imag();
        u_[n] = {yr * er - yi * ei, yr * ei + yi * er};
    }
    push_mixed(u_);
    return take_update();
}

void BankDecoder::push_mixed(std::span<const std::complex<double>> u) {
    // One bank block at a time: a block's last operation is publishing the selected branch's new characters, so
    // after each block the characters it appended are the last ones of the list (the immediate text keeps them
    // even if a later block of the same call corrects them).
    const auto block = static_cast<std::int64_t>(channel_->block_samples());
    std::size_t i = 0;
    while (i < u.size()) {
        const std::int64_t room = block - (channel_->samples() - channel_->processed());
        const std::size_t take = std::min(u.size() - i, static_cast<std::size_t>(std::max<std::int64_t>(room, 1)));
        channel_->push(u.subspan(i, take));
        i += take;
        collect_appended();
    }
}

void BankDecoder::collect_appended() {
    const auto& out = channel_->output();
    const std::size_t fresh = out.appended() - appended_seen_;
    appended_seen_ = out.appended();
    const auto& chars = out.chars();
    for (std::size_t k = chars.size() - std::min(fresh, chars.size()); k < chars.size(); ++k)
        pending_.push_back(symbol(chars[k]));
}

DecodeUpdate BankDecoder::take_update() {
    DecodeUpdate up;
    up.chars = std::move(pending_);
    pending_.clear();
    up.freq_offset_hz = anchor_hz_;  // the bank decodes at the anchor (it does not track frequency itself)
    const auto& out = channel_->output();
    const auto& corrections = out.corrections();
    const auto& chars = out.chars();
    for (; corrections_seen_ < corrections.size(); ++corrections_seen_) {
        const auto& c = corrections[corrections_seen_];
        TextCorrection t;
        // The characters from from_index on as they stand now: applied after this update's chars, in order, the
        // corrections leave the consumer's list equal to the bank's (docs/signal-processing.md section 8c).
        t.from_index = std::min(c.from_index, chars.size());
        for (std::size_t k = t.from_index; k < chars.size(); ++k) t.chars.push_back(symbol(chars[k]));
        t.t_s = c.t_s + (origin_s_ ? *origin_s_ : 0.0);
        t.reason = c.reason;
        up.corrections.push_back(std::move(t));
    }
    return up;
}

DecodeUpdate BankDecoder::flush() {
    if (flushed_) return {};
    flushed_ = true;
    channel_->finish();
    collect_appended();
    return take_update();
}

void BankDecoder::reset() {
    channel_ = std::make_unique<bank::BankChannel>(cfg_, rate_);
    origin_s_.reset();
    phase_sum_hz_ = 0;
    appended_seen_ = 0;
    corrections_seen_ = 0;
    pending_.clear();
    flushed_ = false;
}

}  // namespace kz4ap
