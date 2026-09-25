#include "kz4ap/classical_decoder.hpp"

#include "kz4ap/morse.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>

namespace kz4ap {
namespace {

constexpr double kLog2 = 0.6931471805599453;
constexpr double kDahSlope = 0.08;  // width of the dit/dah decision, in log-duration units
constexpr std::size_t kSpeedWindow = 24;

float alpha_for(double tau_s, double rate) {
    return static_cast<float>(1.0 - std::exp(-1.0 / (tau_s * rate)));
}

float logistic(double x) { return static_cast<float>(1.0 / (1.0 + std::exp(-x))); }

// Validates the decoder's parameters and returns sample_rate. Runs as the
// first-declared member's (rate_'s) initializer, before the filter coefficients
// divide by sample_rate or the time constants, so an invalid parameter throws
// std::invalid_argument instead of producing NaN or infinite coefficients.
double validated_rate(double sample_rate, const ClassicalDecoderConfig& c) {
    if (!(sample_rate > 0) || !(c.min_wpm > 0) || !(c.max_wpm >= c.min_wpm) || !(c.initial_wpm >= c.min_wpm) ||
        !(c.initial_wpm <= c.max_wpm) || !(c.attack_s > 0) || !(c.decay_s > 0) || !(c.squelch_ratio > 1.0f) ||
        !(c.smoothing_dits > 0) || !(c.glitch_dits >= 0))
        throw std::invalid_argument("invalid classical decoder config");
    return sample_rate;
}

}  // namespace

ClassicalDecoder::ClassicalDecoder(double sample_rate, ClassicalDecoderConfig config)
    : rate_(validated_rate(sample_rate, config)),
      config_(config),
      attack_alpha_(alpha_for(config.attack_s, sample_rate)),
      decay_alpha_(alpha_for(config.decay_s, sample_rate)) {
    reset();
}

void ClassicalDecoder::reset() {
    dit_s_ = 1.2 / config_.initial_wpm;
    smooth_alpha_ = alpha_for(std::max(config_.smoothing_dits * dit_s_, 1.0 / rate_), rate_);
    smoothed_ = mark_ = space_ = 0;
    warmup_total_ = warmup_left_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(dit_s_ * rate_)));
    warmup_sum_ = 0;
    anchored_ = false;
    origin_s_ = 0;
    count_ = 0;
    key_ = false;
    down_t_ = prev_down_t_ = up_t_ = 0;
    char_open_ = word_open_ = false;
    elements_.clear();
    recent_marks_.clear();
    confidence_ = 0;
}

DecodeUpdate ClassicalDecoder::process(std::span<const Sample> samples, double t0_s) {
    DecodeUpdate out;
    // Sample times count from one origin, so they come out bit-identical however
    // the stream is split into chunks (t0_s + i / rate_ rounds differently for
    // different splits). A t0_s off the expected time by more than half a sample
    // means a discontinuity, and the count restarts from it.
    if (!anchored_ || std::abs(t0_s - (origin_s_ + static_cast<double>(count_) / rate_)) > 0.5 / rate_) {
        origin_s_ = t0_s;
        count_ = 0;
        anchored_ = true;
    }
    for (const auto& sample : samples) {
        step(std::abs(sample), origin_s_ + static_cast<double>(count_) / rate_, out);
        ++count_;
    }
    out.wpm = static_cast<float>(wpm());
    out.confidence = confidence_;
    return out;
}

DecodeUpdate ClassicalDecoder::flush() {
    DecodeUpdate out;
    if (char_open_) finish_char(out);
    out.wpm = static_cast<float>(wpm());
    out.confidence = confidence_;
    return out;
}

void ClassicalDecoder::step(float magnitude, double t, DecodeUpdate& out) {
    if (warmup_left_ > 0) {
        // Warm-up (one dit at the initial speed): the envelope is a running mean
        // and both levels follow it, so they start at the input's settled level.
        // Starting the smoother at zero would prime space_ near zero, from where
        // it can rise only at decay_s, and noise would key as one long mark until
        // it caught up.
        warmup_sum_ += magnitude;
        --warmup_left_;
        smoothed_ = warmup_sum_ / static_cast<float>(warmup_total_ - warmup_left_);
        mark_ = space_ = smoothed_;
        return;
    }
    smoothed_ += smooth_alpha_ * (magnitude - smoothed_);
    const float env = smoothed_;
    mark_ += (env > mark_ ? attack_alpha_ : decay_alpha_) * (env - mark_);
    space_ += (env < space_ ? attack_alpha_ : decay_alpha_) * (env - space_);

    const float span = mark_ - space_;
    const bool squelched = span <= 0 || mark_ < config_.squelch_ratio * space_;
    if (!key_ && !squelched && env > space_ + 0.6f * span) {
        key_down(t);
    } else if (key_ && (squelched || env < space_ + 0.4f * span)) {
        key_up(t);
    }

    if (!key_) {
        const double gap = t - up_t_;
        if (char_open_ && gap > 2.0 * dit_s_) finish_char(out);
        if (word_open_ && !char_open_ && gap > 5.0 * dit_s_) {
            emit(out, " ", 1.0f, up_t_, t);
            word_open_ = false;
        }
    }
}

void ClassicalDecoder::key_down(double t) {
    key_ = true;
    if (char_open_ && !elements_.empty() && t - up_t_ < config_.glitch_dits * dit_s_) {
        // The key-up was a dropout inside one element: merge it back into that element.
        elements_.pop_back();
        char_open_ = !elements_.empty();  // flush() must not finish a character with no elements
        if (!recent_marks_.empty()) recent_marks_.pop_back();
        down_t_ = prev_down_t_;
        return;
    }
    down_t_ = t;
}

void ClassicalDecoder::key_up(double t) {
    key_ = false;
    const double duration = t - down_t_;
    if (duration < config_.glitch_dits * dit_s_) return;  // too short to be an element
    const float p_dah = logistic((std::log(duration / dit_s_) - kLog2) / kDahSlope);
    const bool dah = p_dah >= 0.5f;
    elements_.push_back({dah, dah ? p_dah : 1.0f - p_dah, down_t_, t});
    prev_down_t_ = down_t_;
    up_t_ = t;
    char_open_ = true;
    recent_marks_.push_back(duration);
    if (recent_marks_.size() > kSpeedWindow) recent_marks_.pop_front();
    update_speed();
}

void ClassicalDecoder::finish_char(DecodeUpdate& out) {
    std::string pattern;
    float probability = 1.0f;
    for (const auto& e : elements_) {
        pattern += e.dah ? '-' : '.';
        probability *= e.confidence;
    }
    const auto symbol = morse::decode(pattern);
    emit(out, symbol.empty() ? "*" : symbol, symbol.empty() ? 0.0f : probability, elements_.front().start_s,
         elements_.back().end_s);
    elements_.clear();
    char_open_ = false;
    word_open_ = true;
}

void ClassicalDecoder::emit(DecodeUpdate& out, std::string_view text, float probability, double start_s, double end_s) {
    out.chars.push_back({std::string(text), probability, start_s, end_s});
    if (text != " ") confidence_ += 0.2f * (probability - confidence_);
}

void ClassicalDecoder::update_speed() {
    std::vector<double> d(recent_marks_.begin(), recent_marks_.end());
    if (d.size() < 2) return;
    std::sort(d.begin(), d.end());
    std::size_t split = 0;
    double best = 0;
    for (std::size_t i = 0; i + 1 < d.size(); ++i) {
        const double ratio = d[i + 1] / d[i];
        if (ratio > best) {
            best = ratio;
            split = i + 1;
        }
    }
    double dit = dit_s_;
    if (best >= 1.8) {
        // Two clusters: dits below the split, dahs (three dits each) above it. A
        // dah outlasts a dit by two dits however much the keying edges shorten
        // every mark (about 5 ms for 5 ms raised-cosine edges), so the speed comes
        // from the difference of the cluster means, not from their levels.
        const auto mid = d.begin() + static_cast<std::ptrdiff_t>(split);
        const double mean_dit = std::accumulate(d.begin(), mid, 0.0) / static_cast<double>(split);
        const double mean_dah = std::accumulate(mid, d.end(), 0.0) / static_cast<double>(d.size() - split);
        dit = (mean_dah - mean_dit) / 2.0;
    } else {
        // All recent marks look alike: they are dits or dahs by the current estimate.
        const double mean = std::accumulate(d.begin(), d.end(), 0.0) / static_cast<double>(d.size());
        dit = mean > 2.0 * dit_s_ ? mean / 3.0 : mean;
    }
    dit_s_ = std::clamp(dit, 1.2 / config_.max_wpm, 1.2 / config_.min_wpm);
    smooth_alpha_ = alpha_for(std::max(config_.smoothing_dits * dit_s_, 1.0 / rate_), rate_);
}

}  // namespace kz4ap
