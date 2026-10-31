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
    // Zeros before the taps delay the response, what template_delay_s asks.
    std::vector<double> taps = channel_filter_taps(channel);
    taps.insert(taps.begin(), static_cast<std::size_t>(std::lround(config.template_delay_s * channel.sample_rate)), 0.0);
    return viterbi::make_template_tables(taps, channel.sample_rate,
                                         channel.fft_size / channel.channel_bins, front, spacing_of(config, rate),
                                         config.model.edge_rise_s, config.model.edge_rise_min_s, config.model.edge_rise_max_s);
}

}  // namespace

SegmentalParts::SegmentalParts(const segmental::SegmentalConfig& c, const ChannelizerConfig& channel)
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

namespace {

const segmental::SegmentalConfig& checked_scales(const segmental::SegmentalConfig& config) {
    require(!config.filter_scales.empty() && config.filter_scales.front() == 1,
            "segmental: filter_scales must start with 1, the widest filter");
    for (std::size_t i = 1; i < config.filter_scales.size(); ++i)
        require(config.filter_scales[i] > config.filter_scales[i - 1], "segmental: filter_scales must rise");
    return config;
}

}  // namespace

SegmentalShared::SegmentalShared(const segmental::SegmentalConfig& c, const ChannelizerConfig& channel)
    : SegmentalParts(checked_scales(c), channel) {
    narrower.reserve(c.filter_scales.size());
    for (std::size_t i = 1; i < c.filter_scales.size(); ++i)
        narrower.emplace_back(segmental::at_filter_scale(c, c.filter_scales[i]), channel);
}

SegmentalDecoder::Stream::Stream(const SegmentalParts& p, int m)
    : parts(&p),
      scale(m),
      floor(std::make_unique<segmental::ChannelFloor>(p.config)),
      sums(std::make_unique<segmental::RunningSums>()),
      evidence(std::make_unique<segmental::LevelEvidence>(p.config, p.level_tables, *sums)),
      search(std::make_unique<segmental::Search>(p.config, p.model_tables, *sums, &p.level_tables)),
      sampler(p.front_taps, p.channel_samples_per_sample) {
    free_key.change = kFreeKeySwitchPerS * p.config.model.sample_interval_s;
    free_key.stay = 1.0 - free_key.change;
}

SegmentalDecoder::SegmentalDecoder(double rate_hz, std::shared_ptr<const SegmentalShared> shared)
    : rate_(rate_hz), shared_(std::move(shared)) {
    require(shared_ != nullptr, "SegmentalDecoder: no shared parts");
    require(std::fabs(shared_->channel_rate_hz - rate_hz) <= 1e-9 * rate_hz,
            "SegmentalDecoder: the shared parts were built for another channel rate");
    start();
}

void SegmentalDecoder::start() {
    streams_.clear();
    streams_.reserve(1 + shared_->narrower.size());
    streams_.emplace_back(*shared_, 1);
    for (std::size_t i = 0; i < shared_->narrower.size(); ++i)
        streams_.emplace_back(shared_->narrower[i], shared_->config.filter_scales[i + 1]);
    leader_ = 0;
}

int SegmentalDecoder::leading_scale() const { return streams_[leader_].scale; }

std::size_t SegmentalDecoder::streams_running() const { return streams_.size(); }

bool SegmentalDecoder::choose_leader(double now_s) {
    const segmental::SegmentalConfig& config = shared_->config;
    const std::size_t before = leader_;
    double top = streams_[leader_].morse_over_free();
    for (std::size_t i = 0; i < streams_.size(); ++i) {
        const double value = streams_[i].morse_over_free();
        if (value > top + config.filter_scale_switch_nats) {
            top = value;
            leader_ = i;
        }
    }
    const bool changed = leader_ != before;
    if (streams_.size() > 1 && config.filter_scale_drop_after_s >= 0.0 && now_s >= config.filter_scale_drop_after_s) {
        // The widest filter stays, whatever it reads: it is what a new, fast sender on this channel can be read with.
        const Stream* leading = &streams_[leader_];
        const int scale = leading->scale;
        std::erase_if(streams_, [&](const Stream& s) {
            return s.scale != 1 && s.scale != scale && s.morse_over_free() < top - config.filter_scale_drop_margin_nats;
        });
        for (std::size_t i = 0; i < streams_.size(); ++i)
            if (streams_[i].scale == scale) leader_ = i;
    }
    return changed;
}

void SegmentalDecoder::FreeKey::step(double l1, double top) {
    const double new_up = (stay * up + change * down) * std::exp(-top);
    const double new_down = (stay * down + change * up) * std::exp(l1 - top);
    const double total = new_up + new_down;
    up = new_up / total;
    down = new_down / total;
    nats += top + std::log(total);
}

void SegmentalDecoder::Stream::advance_free_key() {
    const std::int64_t last = sums->committed();
    for (; free_next < last; ++free_next) {
        const double l1 = sums->sum(free_next + 1) - sums->sum(free_next);
        const double top = std::max(0.0, l1);
        free_key.step(l1, top);
    }
}

std::optional<KeyingEvidence> SegmentalDecoder::keying_evidence() const {
    // The widest filter's, whichever stream leads: a track's judge reads one number across the track's life.
    const Stream& widest = streams_.front();
    KeyingEvidence evidence;
    evidence.morse_nats = widest.search->best_score();
    evidence.free_key_nats = widest.free_key.nats;
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
    for (Stream& stream : streams_) {
        stream.envelope.clear();
        stream.sampler.push(samples, next_index_, stream.envelope);
        const double spacing = static_cast<double>(stream.parts->channel_samples_per_sample);
        const double sigma2 = n0_ * stream.parts->output_noise.sigma2_per_n0_hz;
        for (const viterbi::EnvelopeSample& sample : stream.envelope) {
            if (!(n0_ > 0.0)) continue;
            // Samples not searched (N0 = 0) leave the stream spliced; the time of the samples after them is noted.
            if (sample.k != stream.expected_k)
                stream.search->note_sample_time(stream.pushed, spacing * static_cast<double>(sample.k) / rate_);
            stream.expected_k = sample.k + 1;
            stream.evidence->push(sample.e_fs, stream.floor->sigma2(sample.e_fs, sigma2));
            stream.search->advance();  // at once: the short elements' context reaches back only so far
            stream.advance_free_key();
            ++stream.pushed;
        }
    }
    next_index_ += samples.size();
    const double now_s = static_cast<double>(next_index_) / rate_;
    if (now_s < next_publish_s_) return {};
    next_publish_s_ = now_s + kPublishEveryS;
    const bool changed = choose_leader(now_s);
    const segmental::Search& search = *streams_[leader_].search;
    segmental::BestStory story = search.best_story(kPublishTailSymbols);
    // A letter still open goes out when it closes: until then it may yet grow (E, I, S, H), and a display would show each.
    if (story.open_letter && !story.tail.empty()) {
        story.tail.pop_back();
        --story.length;
    }
    // A tail that begins past what has been published (more text than the tail in one period), or another stream's: read it all.
    if (changed || story.length - story.tail.size() > published_.size()) {
        const bool open_letter = story.open_letter;
        story.tail = search.best_text();
        if (open_letter && !story.tail.empty()) story.tail.pop_back();
        story.length = story.tail.size();
    }
    return bring_up_to_date(story, now_s);
}

DecodeUpdate SegmentalDecoder::bring_up_to_date(const segmental::BestStory& story, double now_s) {
    DecodeUpdate update;
    if (story.speed) {
        // PARIS: 1.2 s / the dot, the dot from the speed grid (config.hpp).
        const viterbi::ViterbiConfig& model = streams_[leader_].parts->config.model;
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
    for (Stream& stream : streams_) {
        stream.evidence->finish();
        stream.search->advance();
        stream.advance_free_key();
    }
    (void)choose_leader(-1.0);
    const segmental::Search& search = *streams_[leader_].search;
    // The whole text, not the tail: the channel's last word on everything it has published.
    segmental::BestStory story = search.best_story(0);
    story.tail = search.best_text();
    story.length = story.tail.size();
    DecodeUpdate last = bring_up_to_date(story, static_cast<double>(next_index_) / rate_);
    if (const char* path = std::getenv("KZ4AP_TRACE")) {  // experiment
        if (std::FILE* out = std::fopen(path, "a")) {
            std::fprintf(out, "%.3f %.3f %.2f %zu %d", search.best_score(), streams_[leader_].free_key.nats, wpm_, published_.size(), leading_scale());
            for (const DecodedSymbol& symbol : story.tail)
                std::fprintf(out, " %s,%.3f,%.3f", symbol.text == " " ? "_" : symbol.text.c_str(), symbol.start_s, symbol.end_s);
            std::fprintf(out, "\n");
            std::fclose(out);
        }
    }
    return last;
}

void SegmentalDecoder::reset() {
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
