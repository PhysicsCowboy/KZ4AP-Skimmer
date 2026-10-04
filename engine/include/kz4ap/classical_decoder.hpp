#pragma once

#include "kz4ap/decoder.hpp"
#include "kz4ap/frequency_tracker.hpp"
#include "kz4ap/matched_front_end.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace kz4ap {

enum class FrontEnd {
    Envelope,  // the baseline: |y|, smoothing, keying at 40%/60% between space and mark levels
    Matched,   // re-centering, a filter matched to the dit, keying on the posterior log-odds
    Bank,      // the bank decoder (BankDecoder, milestone 2c), not a ClassicalDecoder: the engine creates a BankDecoder
};

struct ClassicalDecoderConfig {
    double initial_wpm = 25.0;
    double min_wpm = 5.0;
    double max_wpm = 60.0;
    double attack_s = 0.004;       // how fast mark/space levels follow a new extreme
    double decay_s = 3.0;          // how fast they relax back
    float squelch_ratio = 3.0f;    // mark level must be at least this factor times the space level to key
    double smoothing_dits = 0.25;  // envelope smoothing time constant, in dits
    double glitch_dits = 0.3;      // marks and dropouts shorter than this are ignored
    FrontEnd front_end = FrontEnd::Matched;  // owner decision 2026-09-29; Envelope stays selectable
    double llr_hysteresis = 1.0;          // Matched: key down above +this, up below -this (posterior log-odds, nats)
    std::size_t follow_after_marks = 8;   // Matched: the filter follows the speed once the window holds this many marks
    double max_dit_growth = 1.25;         // Matched, while the filter follows: the dit estimate may grow at most this factor
                                          // per mark (owner's decision); a mark re-opened by a dropout merge is re-measured
                                          // from the speed state before it, so the bound applies once per physical mark
    double reacquire_after_dits = 12.0;   // Matched: re-acquire after the key has been up this many dits...
    double reacquire_min_s = 0.5;         // ...and at least this long, s
    double reacquire_window_s = 2.0;      // Matched: if nothing is keyed this long after, back to the narrow filter, s
    MatchedFrontEndConfig matched;        // Matched only
    FrequencyTrackerConfig tracker;       // Matched only
};

// The classical statistical decoder, run as one of two decoders (ClassicalDecoderConfig::front_end): Matched (the
// default) re-centers the station's frequency, filters with a dit-matched boxcar and keys on the
// posterior log-odds; Envelope (the milestone-1 path) keys the smoothed envelope against adaptive mark
// and space levels. Both then take the speed from the dit/dah split of recent marks and probabilities
// from timing margins.
class ClassicalDecoder final : public Decoder {
public:
    // initial_offset_hz: the station's offset from its channel's center as the detector
    // measured it, Hz. The Matched decoder starts re-centering there. Throws std::invalid_argument for an invalid
    // config, and for front_end == FrontEnd::Bank (see BankDecoder).
    explicit ClassicalDecoder(double sample_rate, ClassicalDecoderConfig config = {}, double initial_offset_hz = 0.0);

    DecodeUpdate process(std::span<const Sample> samples, double t0_s) override;
    DecodeUpdate flush() override;
    void reset() override;

    double wpm() const { return 1.2 / dit_s_; }
    double frequency_offset_hz() const;  // Matched: the tracker's estimate; Envelope: the initial offset, Hz
    int filter_length() const;           // Matched: the matched filter's length K, samples; Envelope: 0
    // Matched: marks counted for speed since the last re-acquisition (or reset); Envelope: counted but unused.
    std::size_t marks_since_reacquire() const { return marks_since_reacquire_; }
    // Matched: the tracker fine-tunes within +/- tracker.fine_tune_hz of this (Task 10); Envelope: ignored.
    void set_frequency_anchor_hz(double offset_hz) override;

private:
    struct Element {
        bool dah;
        float confidence;
        double start_s;
        double end_s;
    };

    void step(float magnitude, double t, DecodeUpdate& out);
    void key_down(double t, bool observed = true);  // observed: false if the key-up before it was not seen (Matched)
    void key_up(double t);
    void finish_char(DecodeUpdate& out);
    void emit(DecodeUpdate& out, std::string_view text, float probability, double start_s, double end_s);
    void update_speed();
    bool counts_for_speed(double duration) const;
    void restore_speed_state();

    double rate_;
    ClassicalDecoderConfig config_;
    float attack_alpha_;
    float decay_alpha_;
    float smooth_alpha_ = 1;
    double dit_s_ = 0;
    float smoothed_ = 0;
    float mark_ = 0;
    float space_ = 0;
    std::size_t warmup_total_ = 0;  // warm-up length, in samples
    std::size_t warmup_left_ = 0;
    float warmup_sum_ = 0;
    bool anchored_ = false;  // origin_s_ and count_ hold a stream position
    double origin_s_ = 0;    // time of the sample count_ counts from
    std::uint64_t count_ = 0;
    bool key_ = false;
    double down_t_ = 0;
    double prev_down_t_ = 0;
    double up_t_ = 0;
    bool char_open_ = false;  // elements received since the last character boundary
    bool word_open_ = false;  // characters received since the last word space
    std::vector<Element> elements_;
    std::deque<double> recent_marks_;
    float confidence_ = 0;

    void step_matched(Sample y, double t, DecodeUpdate& out);
    void check_gaps(double t, DecodeUpdate& out);

    double initial_offset_hz_ = 0;
    std::optional<FrequencyTracker> tracker_;     // Matched only
    std::optional<MatchedFrontEnd> front_end_;    // Matched only
    double last_key_t_ = 0;                       // Matched: the last time the key was down, s
    bool heard_since_reacquire_ = false;          // Matched: a key-down since the last re-acquisition
    std::size_t marks_since_reacquire_ = 0;       // Matched: marks counted for speed since then
    double reacquire_until_ = -1;                 // Matched: end of the re-acquisition window, s (-1: none)
    bool was_following_ = false;                  // Matched: the filter followed the speed before it
    std::deque<double> set_aside_marks_;          // Matched: the speed window before it, back if nothing answers
    double filter_dit_s_ = 0.02;                  // Matched: the dit the matched filter is set to, s (grows at most
                                                  // max_dit_growth per mark, from the acquisition dit)
    double set_aside_filter_dit_s_ = 0.02;        // Matched: the filter's dit before the re-acquisition, s
    // Matched: the speed state just before the last element's key-up updated it, so a dropout merge that
    // re-opens that element can undo the update (the growth bound then applies once per physical mark).
    struct SpeedState {
        bool counted = false;  // the last element counted for speed; the fields below are valid
        double dit_s = 0;
        float smooth_alpha = 1;
        double filter_dit_s = 0;
        std::size_t marks_since_reacquire = 0;
        std::deque<double> recent_marks;
    };
    SpeedState before_last_element_;
    bool key_up_seen_ = false;        // Matched: since the last sample on which keying was impossible (warm-up, squelch
                                      // closed), a keyable sample had the key up with log-odds below -llr_hysteresis
    bool mark_observed_ = true;       // the current mark's key-down came after such a sample
    bool prev_mark_observed_ = true;  // the same for the last element; a dropout merge brings it back
};

}  // namespace kz4ap
