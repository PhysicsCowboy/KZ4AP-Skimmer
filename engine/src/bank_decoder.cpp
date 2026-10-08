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
    for (const auto& c : up.chars) published_.push_back(c.text);
    const auto& out = channel_->output();
    const auto& corrections = out.corrections();
    const auto& chars = out.chars();
    const double origin = origin_s_ ? *origin_s_ : 0.0;
    // The consumer's list (published_) mirrors the bank's; apply a correction to it as the consumer will.
    auto emit = [&](std::size_t from, double t_s, const std::string& reason, double reach_s) {
        TextCorrection t;
        t.from_index = std::min(from, chars.size());
        for (std::size_t k = t.from_index; k < chars.size(); ++k) t.chars.push_back(symbol(chars[k]));
        t.t_s = t_s + origin;
        t.reason = reason;
        t.reach_s = reach_s;
        published_.resize(std::min(t.from_index, published_.size()));
        for (const auto& s : t.chars) published_.push_back(s.text);
        up.corrections.push_back(std::move(t));
    };
    for (; corrections_seen_ < corrections.size(); ++corrections_seen_) {
        const auto& c = corrections[corrections_seen_];
        // From the first character whose text the correction changed, or earlier: the prototype's from_index (the
        // number of characters kept) is that index unless the kept characters are not a prefix of the list
        // (characters that overlap in time), when it is past it. The characters from there on as they stand now:
        // applied after this update's chars, in order, the corrections leave the consumer's list equal to the bank's
        // (docs/signal-processing.md appendix A.8c, "The consumer's rule").
        emit(std::min(c.from_index, c.first_changed_index), c.t_s, c.reason, c.reach_s);
    }
    // Every change of the list's text (with a correction or without one: a same-text replacement that reorders
    // overlapping characters) is at or after the lowest of the new text_changes(); from there, check the consumer's
    // list against the bank's and, if they differ, re-send the tail ("resync"; never seen on the full suite).
    std::size_t low = chars.size();
    bool changed = false;
    for (; changes_seen_ < out.text_changes().size(); ++changes_seen_) {
        low = std::min(low, out.text_changes()[changes_seen_]);
        changed = true;
    }
    if (changed) {
        std::size_t k = std::min(low, published_.size());
        while (k < chars.size() && k < published_.size() && published_[k] == chars[k].text) ++k;
        if (k < chars.size() || k < published_.size()) {
            const double now = static_cast<double>(channel_->processed()) / rate_;
            emit(k, now, "resync", k < chars.size() ? now - chars[k].start_s : 0.0);
        }
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
    changes_seen_ = 0;
    published_.clear();
    pending_.clear();
    flushed_ = false;
}

}  // namespace kz4ap
