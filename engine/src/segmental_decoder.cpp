#include "kz4ap/segmental_decoder.hpp"

#include "kz4ap/viterbi/observation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace kz4ap {

namespace {

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

double channel_rate(const ChannelizerConfig& channel) {
    (void)channel_filter_taps(channel);  // checks the channelizer's configuration
    return static_cast<double>(channel.sample_rate) / (channel.fft_size / channel.channel_bins);
}

int spacing_of(const segmental::SegmentalConfig& config, double rate_hz) {
    return viterbi::whole_count(config.model.sample_interval_s, 1.0 / rate_hz, "sample_interval_s", "channel samples");
}

viterbi::OutputNoise noise_of(const segmental::SegmentalConfig& config, const ChannelizerConfig& channel,
                              const std::vector<double>& front) {
    const double rate = channel_rate(channel);
    return viterbi::output_noise(channel_filter_taps(channel), channel.sample_rate, channel.fft_size / channel.channel_bins,
                                 front, spacing_of(config, rate));
}

viterbi::TemplateTables template_of(const segmental::SegmentalConfig& config, const ChannelizerConfig& channel,
                                    const std::vector<double>& front) {
    const double rate = channel_rate(channel);
    std::vector<double> taps = channel_filter_taps(channel);
    taps.insert(taps.begin(), static_cast<std::size_t>(std::lround(config.template_delay_s * channel.sample_rate)), 0.0);
    return viterbi::make_template_tables(taps, channel.sample_rate,
                                         channel.fft_size / channel.channel_bins, front, spacing_of(config, rate),
                                         config.model.edge_rise_s, config.model.edge_rise_min_s, config.model.edge_rise_max_s);
}

}  // namespace

SegmentalShared::SegmentalShared(const segmental::SegmentalConfig& c, const ChannelizerConfig& channel)
    : config(c),
      channel_rate_hz(channel_rate(channel)),
      channel_samples_per_sample(spacing_of(c, channel_rate_hz)),
      front_taps(viterbi::front_filter_taps(channel_rate_hz, c.model.front_dot_s, c.model.edge_rise_s)),
      output_noise(noise_of(c, channel, front_taps)),
      template_tables(template_of(c, channel, front_taps)),
      level_tables(c, output_noise, template_tables),
      model_tables(c) {
    (void)segmental::ChannelFloor(c);  // refuses its settings here, before any channel opens
}

SegmentalDecoder::SegmentalDecoder(double rate_hz, std::shared_ptr<const SegmentalShared> shared)
    : rate_(rate_hz),
      shared_(std::move(shared)),
      sampler_(shared_ ? shared_->front_taps : std::vector<double>{1.0}, shared_ ? shared_->channel_samples_per_sample : 1) {
    require(shared_ != nullptr, "SegmentalDecoder: no shared parts");
    require(std::fabs(shared_->channel_rate_hz - rate_hz) <= 1e-9 * rate_hz,
            "SegmentalDecoder: the shared parts were built for another channel rate");
    start();
}

void SegmentalDecoder::start() {
    floor_ = std::make_unique<segmental::ChannelFloor>(shared_->config);
    sums_ = std::make_unique<segmental::RunningSums>();
    evidence_ = std::make_unique<segmental::LevelEvidence>(shared_->config, shared_->level_tables, *sums_);
    search_ = std::make_unique<segmental::Search>(shared_->config, shared_->model_tables, *sums_, &shared_->level_tables);
    pushed_ = 0;
    expected_k_ = -1;
    free_next_ = 0;
    const double interval_s = shared_->config.model.sample_interval_s;
    free_key_ = FreeKey{};
    free_key_.change = kFreeKeySwitchPerS * interval_s;
    free_key_.stay = 1.0 - free_key_.change;
}

void SegmentalDecoder::FreeKey::step(double l1, double top) {
    const double new_up = (stay * up + change * down) * std::exp(-top);
    const double new_down = (stay * down + change * up) * std::exp(l1 - top);
    const double total = new_up + new_down;
    up = new_up / total;
    down = new_down / total;
    nats += top + std::log(total);
}

void SegmentalDecoder::advance_free_key() {
    const std::int64_t last = sums_->committed();
    for (; free_next_ < last; ++free_next_) {
        const double l1 = sums_->sum(free_next_ + 1) - sums_->sum(free_next_);
        const double top = std::max(0.0, l1);
        free_key_.step(l1, top);
    }
}

std::optional<KeyingEvidence> SegmentalDecoder::keying_evidence() const {
    KeyingEvidence evidence;
    evidence.morse_nats = search_->best_score();
    evidence.free_key_nats = free_key_.nats;
    return evidence;
}

DecodeUpdate SegmentalDecoder::process(std::span<const Sample> samples, double t0_s) {
    if (flushed_ || samples.empty()) return {};
    require(std::isfinite(t0_s), "SegmentalDecoder::process: t0_s must be finite (s)");
    if (!started_) {
        const double position = t0_s * rate_;
        const double index = std::round(position);
        require(index >= 0.0 && index < 9e15 && std::fabs(position - index) <= 1e-3,
                "SegmentalDecoder::process: the first block's t0_s must be a channel sample's stream time");
        next_index_ = static_cast<std::uint64_t>(index);
        started_ = true;
    } else {
        const double expected_s = static_cast<double>(next_index_) / rate_;
        require(std::fabs(t0_s - expected_s) <= 0.5 / rate_,
                "SegmentalDecoder::process: a block must start where the previous one ended (to within half a sample)");
    }
    envelope_.clear();
    sampler_.push(samples, next_index_, envelope_);
    next_index_ += samples.size();
    const double spacing = static_cast<double>(shared_->channel_samples_per_sample);
    const double sigma2 = n0_ * shared_->output_noise.sigma2_per_n0_hz;
    for (const viterbi::EnvelopeSample& sample : envelope_) {
        if (!(n0_ > 0.0)) continue;
        // Samples not searched (N0 = 0) leave the stream spliced; the time of the samples after them is noted.
        if (sample.k != expected_k_) search_->note_sample_time(pushed_, spacing * static_cast<double>(sample.k) / rate_);
        expected_k_ = sample.k + 1;
        evidence_->push(sample.e_fs, floor_->sigma2(sample.e_fs, sigma2));
        search_->advance();  // at once: the short elements' context reaches back only so far
        advance_free_key();
        ++pushed_;
    }
    const double now_s = static_cast<double>(next_index_) / rate_;
    if (now_s < next_publish_s_) return {};
    next_publish_s_ = now_s + kPublishEveryS;
    segmental::BestStory story = search_->best_story(kPublishTailSymbols);
    // A letter still open goes out when it closes: until then it may yet grow (E, I, S, H), and a display would show each.
    if (story.open_letter && !story.tail.empty()) {
        story.tail.pop_back();
        --story.length;
    }
    // A tail that begins past what has been published: more text than the tail in one period; read it all.
    if (story.length - story.tail.size() > published_.size()) {
        story.tail = search_->best_text();
        story.length = story.tail.size();
    }
    return bring_up_to_date(story, now_s);
}

DecodeUpdate SegmentalDecoder::bring_up_to_date(const segmental::BestStory& story, double now_s) {
    DecodeUpdate update;
    if (story.speed) {
        // PARIS: 1.2 s / the dot, the dot from the speed grid (config.hpp).
        const viterbi::ViterbiConfig& model = shared_->config.model;
        wpm_ = static_cast<float>(1200.0 / (model.dot_min_ms * std::pow(model.speed_ratio, *story.speed)));
    }
    update.wpm = wpm_;
    const std::size_t base = story.length - story.tail.size();
    std::size_t k = base;
    while (k < published_.size() && k < story.length && published_[k] == story.tail[k - base].text) ++k;
    if (k < published_.size()) {
        // The best story changed its mind about text already out: a correction from the first symbol that differs.
        TextCorrection correction;
        correction.from_index = k;
        correction.chars.assign(story.tail.begin() + static_cast<std::ptrdiff_t>(k - base), story.tail.end());
        correction.t_s = now_s;
        correction.reason = "story";
        correction.reach_s = k < story.length ? now_s - story.tail[k - base].start_s : 0.0;
        update.corrections.push_back(std::move(correction));
    } else {
        update.chars.assign(story.tail.begin() + static_cast<std::ptrdiff_t>(k - base), story.tail.end());
    }
    published_.resize(k);
    for (std::size_t i = k; i < story.length; ++i) published_.push_back(story.tail[i - base].text);
    return update;
}

DecodeUpdate SegmentalDecoder::flush() {
    if (flushed_) return {};
    flushed_ = true;
    evidence_->finish();
    search_->advance();
    // The whole text, not the tail: the channel's last word on everything it has published.
    segmental::BestStory story = search_->best_story(0);
    story.tail = search_->best_text();
    story.length = story.tail.size();
    DecodeUpdate last = bring_up_to_date(story, static_cast<double>(next_index_) / rate_);
    if (const char* path = std::getenv("KZ4AP_TRACE")) {  // experiment
        if (std::FILE* out = std::fopen(path, "a")) {
            std::fprintf(out, "%.3f %.3f %.2f %zu", search_->best_score(), free_key_.nats, wpm_, published_.size());
            for (const DecodedSymbol& symbol : story.tail)
                std::fprintf(out, " %s,%.3f,%.3f", symbol.text == " " ? "_" : symbol.text.c_str(), symbol.start_s, symbol.end_s);
            std::fprintf(out, "\n");
            std::fclose(out);
        }
    }
    return last;
}

void SegmentalDecoder::reset() {
    sampler_.reset();
    start();
    started_ = false;
    flushed_ = false;
    next_index_ = 0;
    next_publish_s_ = 0.0;
    wpm_ = 0.0f;
    published_.clear();
}

void SegmentalDecoder::set_noise_density_fs2_per_hz(double n0_fs2_per_hz) {
    require(std::isfinite(n0_fs2_per_hz) && n0_fs2_per_hz >= 0.0,
            "SegmentalDecoder: the noise density must be finite and not negative (FS^2/Hz)");
    n0_ = n0_fs2_per_hz;
}

}  // namespace kz4ap
