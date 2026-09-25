#pragma once

#include "kz4ap/decoder.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace kz4ap {

struct ClassicalDecoderConfig {
    double initial_wpm = 25.0;
    double min_wpm = 5.0;
    double max_wpm = 60.0;
    double attack_s = 0.004;       // how fast mark/space levels follow a new extreme
    double decay_s = 3.0;          // how fast they relax back
    float squelch_ratio = 3.0f;    // mark level must exceed space level by this factor to key
    double smoothing_dits = 0.25;  // envelope smoothing time constant, in dits
    double glitch_dits = 0.3;      // marks and dropouts shorter than this are ignored
};

// Baseline statistical decoder: envelope keying against adaptive levels, speed
// from the dit/dah split of recent marks, probabilities from timing margins.
class ClassicalDecoder final : public Decoder {
public:
    explicit ClassicalDecoder(double sample_rate, ClassicalDecoderConfig config = {});

    DecodeUpdate process(std::span<const Sample> samples, double t0_s) override;
    DecodeUpdate flush() override;
    void reset() override;

    double wpm() const { return 1.2 / dit_s_; }

private:
    struct Element {
        bool dah;
        float confidence;
        double start_s;
        double end_s;
    };

    void step(float magnitude, double t, DecodeUpdate& out);
    void key_down(double t);
    void key_up(double t);
    void finish_char(DecodeUpdate& out);
    void emit(DecodeUpdate& out, std::string_view text, float probability, double start_s, double end_s);
    void update_speed();

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
};

}  // namespace kz4ap
