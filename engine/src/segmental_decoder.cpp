#include "kz4ap/segmental_decoder.hpp"

#include "kz4ap/viterbi/observation.hpp"

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
    return viterbi::make_template_tables(channel_filter_taps(channel), channel.sample_rate,
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
      model_tables(c) {}

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
    sums_ = std::make_unique<segmental::RunningSums>();
    evidence_ = std::make_unique<segmental::LevelEvidence>(shared_->config, shared_->level_tables, *sums_);
    search_ = std::make_unique<segmental::Search>(shared_->config, shared_->model_tables, *sums_);
    pushed_ = 0;
    expected_k_ = -1;
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
        evidence_->push(sample.e_fs, sigma2);
        ++pushed_;
    }
    search_->advance();
    return {};
}

DecodeUpdate SegmentalDecoder::flush() {
    if (flushed_) return {};
    flushed_ = true;
    evidence_->finish();
    search_->advance();
    if (const char* debug = std::getenv("KZ4AP_SEGMENTAL_DEBUG"); debug != nullptr) {
        const segmental::SearchStats& s = search_->stats();
        std::fprintf(stderr, "segmental: %lld boundaries, %.1f touches per boundary, %.3f opened, largest live %lld\n",
                     static_cast<long long>(s.boundaries), static_cast<double>(s.touches) / static_cast<double>(s.boundaries + 1),
                     static_cast<double>(s.opened) / static_cast<double>(s.boundaries + 1), static_cast<long long>(s.largest_live));
    }
    DecodeUpdate update;
    update.chars = search_->best_text();
    if (std::getenv("KZ4AP_SEGMENTAL_DEBUG") != nullptr)
        for (const auto& c : update.chars) std::fprintf(stderr, "[%s %.0f %.0f] ", c.text.c_str(), c.start_s * 375, c.end_s * 375);
    return update;
}

void SegmentalDecoder::reset() {
    sampler_.reset();
    start();
    started_ = false;
    flushed_ = false;
    next_index_ = 0;
}

void SegmentalDecoder::set_noise_density_fs2_per_hz(double n0_fs2_per_hz) {
    require(std::isfinite(n0_fs2_per_hz) && n0_fs2_per_hz >= 0.0,
            "SegmentalDecoder: the noise density must be finite and not negative (FS^2/Hz)");
    n0_ = n0_fs2_per_hz;
}

}  // namespace kz4ap
