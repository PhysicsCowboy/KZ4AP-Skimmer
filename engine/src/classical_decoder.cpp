#include "kz4ap/classical_decoder.hpp"

#include "kz4ap/morse.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace kz4ap {
namespace {

constexpr double kLog2 = 0.6931471805599453;
constexpr double kDahSlope = 0.08;  // width of the dit/dah decision, in log-duration units
constexpr std::size_t kSpeedWindow = 24;
// Marks longer than a dah of this many dits at min_wpm are not Morse elements
// (a tune-up carrier, say) and are kept out of the speed estimate.
constexpr double kLongestElementDits = 4.0;
// Keying edges shorten every mark by about the same time, which raises the
// dah/dit ratio above 3. Measured with 5 ms raised-cosine edges: 2.91-3.22 at
// 5-25 wpm, up to 3.52 at 45 wpm and 3.75 at 60 wpm. Heavy hand keying with
// four-dit dahs measures 3.95-4.32. Above this bound the ratio is taken as the
// sender's weighting, not edge shortening.
constexpr double kMaxShortenedRatio = 3.85;

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
        !(c.smoothing_dits > 0) || !(c.glitch_dits >= 0) || !(c.llr_hysteresis >= 0) || c.follow_after_marks < 2 ||
        !(c.max_dit_growth >= 1) || !(c.reacquire_after_dits > 0) || !(c.reacquire_min_s >= 0) ||
        !(c.reacquire_window_s >= 0))
        throw std::invalid_argument("invalid classical decoder config");
    return sample_rate;
}

}  // namespace

ClassicalDecoder::ClassicalDecoder(double sample_rate, ClassicalDecoderConfig config, double initial_offset_hz)
    : rate_(validated_rate(sample_rate, config)),
      config_(config),
      attack_alpha_(alpha_for(config.attack_s, sample_rate)),
      decay_alpha_(alpha_for(config.decay_s, sample_rate)),
      initial_offset_hz_(initial_offset_hz) {
    if (config_.front_end == FrontEnd::Matched) {
        tracker_.emplace(sample_rate, initial_offset_hz, config_.tracker);
        front_end_.emplace(sample_rate, config_.matched);
    }
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
    if (tracker_) tracker_->reset(initial_offset_hz_);
    if (front_end_) front_end_->reset();
    last_key_t_ = 0;
    heard_since_reacquire_ = false;
    marks_since_reacquire_ = 0;
    reacquire_until_ = -1;
    was_following_ = false;
    set_aside_marks_.clear();
    filter_dit_s_ = 1.2 / config_.matched.initial_wpm;  // the acquisition dit, 20 ms at 60 WPM
    set_aside_filter_dit_s_ = filter_dit_s_;
    before_last_element_ = {};
    key_up_seen_ = false;
    mark_observed_ = true;
    prev_mark_observed_ = true;
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
        const double t = origin_s_ + static_cast<double>(count_) / rate_;
        if (front_end_) step_matched(sample, t, out);
        else step(std::abs(sample), t, out);
        ++count_;
    }
    out.wpm = static_cast<float>(wpm());
    out.confidence = confidence_;
    if (tracker_) out.freq_offset_hz = tracker_->offset_hz();
    return out;
}

DecodeUpdate ClassicalDecoder::flush() {
    DecodeUpdate out;
    if (char_open_) finish_char(out);
    out.wpm = static_cast<float>(wpm());
    out.confidence = confidence_;
    if (tracker_) out.freq_offset_hz = tracker_->offset_hz();
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
    check_gaps(t, out);
}

void ClassicalDecoder::check_gaps(double t, DecodeUpdate& out) {
    if (key_) return;
    const double gap = t - up_t_;
    if (char_open_ && gap > 2.0 * dit_s_) finish_char(out);
    if (word_open_ && !char_open_ && gap > 5.0 * dit_s_) {
        emit(out, " ", 1.0f, up_t_, t);
        word_open_ = false;
    }
}

void ClassicalDecoder::step_matched(Sample y, double t, DecodeUpdate& out) {
    const FrontEndSample f = front_end_->step(tracker_->mix(y));
    if (!f.ready) return;
    tracker_->observe(f.filtered, f.p_key_down);
    const double h = config_.llr_hysteresis;
    // A key-down is observed only if, since the last sample on which keying was impossible (the warm-up
    // returns above; here, the squelch closed), the decoder saw the key up with log-odds below -h:
    // evidence that the carrier was off before the mark began. Otherwise the mark may have begun while
    // keying was impossible or while the log-odds sat between -h and +h, and its duration may be a
    // fragment's: it is decoded but not counted for speed. A re-acquisition needs no code of its own:
    // it restarts s-hat at 0, which closes the squelch on the next sample.
    if (!f.signal) key_up_seen_ = false;
    if (!key_ && f.signal && f.log_odds > h) {
        key_down(t, key_up_seen_);
        heard_since_reacquire_ = true;
    } else if (key_ && (!f.signal || f.log_odds < -h)) {
        key_up(t);
    }
    if (!key_ && f.signal && f.log_odds < -h) key_up_seen_ = true;
    if (key_) last_key_t_ = t;
    // A long silence may be a turnover to another station (at another level, or at another
    // frequency, which the detector will report through the anchor): widen the filter, restart the
    // amplitude and frequency averages, and start a new speed window, so the next station's marks
    // are not mixed with this one's (heuristic).
    if (!key_ && heard_since_reacquire_ &&
        t - last_key_t_ > std::max(config_.reacquire_min_s, config_.reacquire_after_dits * dit_s_)) {
        was_following_ = marks_since_reacquire_ >= config_.follow_after_marks;
        front_end_->reacquire();
        tracker_->reacquire();
        heard_since_reacquire_ = false;
        marks_since_reacquire_ = 0;
        reacquire_until_ = t + config_.reacquire_window_s;
        set_aside_marks_ = std::move(recent_marks_);
        recent_marks_.clear();
        set_aside_filter_dit_s_ = filter_dit_s_;
        filter_dit_s_ = 1.2 / config_.matched.initial_wpm;  // the filter is back at the acquisition width
    }
    // Nothing keyed within the window: the same station is probably pausing (or none is there).
    // Bring back its speed window, and the narrow filter it had, which is more sensitive than the
    // acquisition width.
    if (reacquire_until_ >= 0 && t > reacquire_until_) {
        reacquire_until_ = -1;
        if (!heard_since_reacquire_) {
            recent_marks_ = std::move(set_aside_marks_);
            if (was_following_) {
                filter_dit_s_ = set_aside_filter_dit_s_;  // the width it had (a return, not a follow step)
                front_end_->set_dit(filter_dit_s_);
                marks_since_reacquire_ = config_.follow_after_marks;
            }
        }
        set_aside_marks_.clear();
    }
    check_gaps(t, out);
}

double ClassicalDecoder::frequency_offset_hz() const { return tracker_ ? tracker_->offset_hz() : initial_offset_hz_; }

int ClassicalDecoder::filter_length() const { return front_end_ ? front_end_->length() : 0; }

void ClassicalDecoder::set_frequency_anchor_hz(double offset_hz) {
    if (tracker_) tracker_->set_anchor(offset_hz);  // Envelope: nothing changes (bit-identical)
}

void ClassicalDecoder::key_down(double t, bool observed) {
    key_ = true;
    if (char_open_ && !elements_.empty() && t - up_t_ < config_.glitch_dits * dit_s_) {
        // The key-up was a dropout inside one element: merge it back into that element.
        const Element merged = elements_.back();
        elements_.pop_back();
        char_open_ = !elements_.empty();  // flush() must not finish a character with no elements
        if (front_end_) {
            // Matched: undo the merged element's speed update, so the mark gets one bounded update when
            // it is re-measured (owner decision 2026-09-29: at most max_dit_growth per mark).
            if (before_last_element_.counted) restore_speed_state();
        } else if (counts_for_speed(merged.end_s - merged.start_s) && !recent_marks_.empty()) {
            recent_marks_.pop_back();  // Envelope: milestone 1's merge, unchanged (bit-identical)
        }
        mark_observed_ = prev_mark_observed_;  // the mark continues, with its own start
        down_t_ = prev_down_t_;
        return;
    }
    mark_observed_ = observed;  // Envelope: always true
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
    prev_mark_observed_ = mark_observed_;
    if (front_end_) before_last_element_ = {};  // Matched: nothing to undo unless this element counts
    // A mark whose key-down was not observed (Matched) is decoded but kept out of the speed estimate.
    if (!counts_for_speed(duration) || !mark_observed_) return;
    if (front_end_) {
        // Matched: the state this key-up's speed update starts from, restored if a dropout merge
        // re-opens this element.
        before_last_element_ = {true, dit_s_, smooth_alpha_, filter_dit_s_, marks_since_reacquire_, recent_marks_};
    }
    recent_marks_.push_back(duration);
    ++marks_since_reacquire_;
    if (recent_marks_.size() > kSpeedWindow) recent_marks_.pop_front();
    update_speed();
}

bool ClassicalDecoder::counts_for_speed(double duration) const {
    return duration <= kLongestElementDits * 1.2 / config_.min_wpm;
}

void ClassicalDecoder::restore_speed_state() {
    SpeedState& s = before_last_element_;
    dit_s_ = s.dit_s;
    smooth_alpha_ = s.smooth_alpha;
    marks_since_reacquire_ = s.marks_since_reacquire;
    recent_marks_ = std::move(s.recent_marks);
    if (filter_dit_s_ != s.filter_dit_s) {
        filter_dit_s_ = s.filter_dit_s;
        front_end_->set_dit(filter_dit_s_);  // back to the length before the update (rescales sigma^2 back)
    }
    s = {};
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
        // Two clusters: dits below the split, dahs (three dits each) above it.
        const auto mid = d.begin() + static_cast<std::ptrdiff_t>(split);
        const auto n_dits = static_cast<double>(split);
        const auto n_dahs = static_cast<double>(d.size() - split);
        const double mean_dit = std::accumulate(d.begin(), mid, 0.0) / n_dits;
        const double mean_dah = std::accumulate(mid, d.end(), 0.0) / n_dahs;
        const double ratio = mean_dah / mean_dit;
        if (ratio >= 3.0 && ratio <= kMaxShortenedRatio) {
            // Keying edges shorten every mark by about the same time (about 5 ms
            // for 5 ms raised-cosine edges), but a dah still outlasts a dit by
            // two dits, so the difference of the cluster means is unbiased. The
            // two estimates agree at a ratio of exactly 3.
            dit = (mean_dah - mean_dit) / 2.0;
        } else {
            // A ratio outside the band is the sender's weighting (or a stray long
            // mark), which the difference would amplify: average the clusters.
            dit = (mean_dit * n_dits + mean_dah / 3.0 * n_dahs) / static_cast<double>(d.size());
        }
    } else {
        // All recent marks look alike: they are dits or dahs by the current estimate.
        const double mean = std::accumulate(d.begin(), d.end(), 0.0) / static_cast<double>(d.size());
        dit = mean > 2.0 * dit_s_ ? mean / 3.0 : mean;
    }
    // Matched: while the filter follows the speed, the dit estimate may grow by at most
    // max_dit_growth per mark (owner decision 2026-09-29). The bound is applied here, at each key-up
    // counted for speed; a dropout merge in key_down() restores the state before the merged mark's
    // update, so each physical mark is bounded once. A jump of x2 in one update, while the window held
    // two speeds, made the filter outgrow the element spaces, merge marks and run away (final check
    // F-2); a real slowdown takes ln(ratio) / ln(max_dit_growth) marks to follow.
    const bool following = front_end_ && recent_marks_.size() >= config_.follow_after_marks &&
                           marks_since_reacquire_ >= config_.follow_after_marks;
    if (following) dit = std::min(dit, config_.max_dit_growth * dit_s_);
    dit_s_ = std::clamp(dit, 1.2 / config_.max_wpm, 1.2 / config_.min_wpm);
    smooth_alpha_ = alpha_for(std::max(config_.smoothing_dits * dit_s_, 1.0 / rate_), rate_);
    // The matched filter follows the speed once the estimate rests on enough marks, and
    // after a re-acquisition only once enough of them are new. Its own dit may grow at most
    // max_dit_growth per mark, from its first follow step on (owner decision 3 of option 1,
    // 2026-09-29): the estimate may rest on up to 7 unbounded marks, and a jump from the 20 ms
    // acquisition dit straight to a wrong 110 ms estimate made the filter outgrow the element
    // spaces and run away in simulation. Decreases are not bounded.
    if (front_end_ && recent_marks_.size() >= config_.follow_after_marks &&
        marks_since_reacquire_ >= config_.follow_after_marks) {
        filter_dit_s_ = std::min(dit_s_, config_.max_dit_growth * filter_dit_s_);
        front_end_->set_dit(filter_dit_s_);
    }
}

}  // namespace kz4ap
