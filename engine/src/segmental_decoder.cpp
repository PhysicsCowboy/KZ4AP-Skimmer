#include "kz4ap/segmental_decoder.hpp"

#include "kz4ap/morse.hpp"
#include "kz4ap/segmental/fast_math.hpp"
#include "kz4ap/viterbi/observation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace kz4ap {

ChannelizerConfig segmental_tables_channel(ChannelizerConfig channel) {
    const int k = channel.sample_rate / 48000;
    if (k > 1 && channel.sample_rate % 48000 == 0 && channel.fft_size % (k * channel.channel_bins) == 0) {
        channel.sample_rate /= k;
        channel.fft_size /= k;
    }
    return channel;
}

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

viterbi::OutputNoise noise_of(const segmental::SegmentalConfig& config, const ChannelizerConfig& channel_in,
                              const std::vector<double>& front) {
    const ChannelizerConfig channel = segmental_tables_channel(channel_in);
    const double rate = channel_rate(channel);
    return viterbi::output_noise(channel_filter_taps(channel), channel.sample_rate, channel.fft_size / channel.channel_bins,
                                 front, spacing_of(config, rate));
}

// |H(f)|^2 of real taps at a sample rate.
double power_response(const std::vector<double>& taps, double rate_hz, double f_hz) {
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < taps.size(); ++i) {
        const double phase = 2.0 * std::numbers::pi * f_hz * static_cast<double>(i) / rate_hz;
        re += taps[i] * std::cos(phase);
        im += taps[i] * std::sin(phase);
    }
    return re * re + im * im;
}

viterbi::TemplateTables template_of(const segmental::SegmentalConfig& config, const ChannelizerConfig& channel_in,
                                    const std::vector<double>& front) {
    const ChannelizerConfig channel = segmental_tables_channel(channel_in);
    const double rate = channel_rate(channel);
    // Zeros before the taps delay the response, what template_delay_s asks.
    std::vector<double> taps = channel_filter_taps(channel);
    taps.insert(taps.begin(), static_cast<std::size_t>(std::lround(config.template_delay_s * channel.sample_rate)), 0.0);
    return viterbi::make_template_tables(taps, channel.sample_rate,
                                         channel.fft_size / channel.channel_bins, front, spacing_of(config, rate),
                                         config.model.edge_rise_s, config.model.edge_rise_min_s, config.model.edge_rise_max_s);
}

// The most a channel's own carrier, keyed on once and off once with hard edges (no rise: the widest spread) anywhere inside the
// analyzer's periodic Hann window of n samples, puts in the bin k from the channel's center bin, relative to the largest of its three
// center bins when it is key-down over the whole window, over tones within half a bin of the center (11 offsets); k = 0 ... last, 0
// below first. Derived, by computation: the window's partial sums give every on-interval, edges every n / 256 samples.
std::vector<double> own_splatter_bound(int n, std::size_t first, std::size_t last) {
    std::vector<double> bound(last + 1, 0.0);
    constexpr int kPositions = 256;
    constexpr int kOffsets = 11;
    const int step = std::max(1, n / kPositions);
    std::vector<double> window(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / n);
    using Complex = std::array<double, 2>;
    const auto dft_partial = [&](double cycles_per_n, std::vector<Complex>& sums) {
        // sums[p] = sum over i < p x step of w[i] e^(-2 pi j cycles i / n)
        sums.assign(static_cast<std::size_t>(n / step + 1), Complex{0.0, 0.0});
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i) {
            if (i % step == 0) sums[static_cast<std::size_t>(i / step)] = {re, im};
            const double phase = -2.0 * std::numbers::pi * cycles_per_n * i / n;
            re += window[static_cast<std::size_t>(i)] * std::cos(phase);
            im += window[static_cast<std::size_t>(i)] * std::sin(phase);
        }
        sums.back() = {re, im};
    };
    std::vector<Complex> sums;
    for (int o = 0; o < kOffsets; ++o) {
        const double delta = -0.5 + static_cast<double>(o) / (kOffsets - 1);  // bins: the tone's offset from the center bin
        double center = 0.0;
        for (int j = -1; j <= 1; ++j) {
            dft_partial(j - delta, sums);
            center = std::max(center, sums.back()[0] * sums.back()[0] + sums.back()[1] * sums.back()[1]);
        }
        for (std::size_t k = first; k <= last; ++k) {
            for (const int sign : {-1, 1}) {
                dft_partial(sign * static_cast<double>(k) - delta, sums);
                double most = 0.0;
                for (std::size_t a = 0; a < sums.size(); ++a)
                    for (std::size_t b = a + 1; b < sums.size(); ++b) {
                        const double re = sums[b][0] - sums[a][0], im = sums[b][1] - sums[a][1];
                        most = std::max(most, re * re + im * im);
                    }
                bound[k] = std::max(bound[k], most / center);
            }
        }
    }
    return bound;
}

}  // namespace

SegmentalParts::SegmentalParts(const segmental::SegmentalConfig& c, const ChannelizerConfig& channel)
    : config(c),
      channel_rate_hz(channel_rate(channel)),
      channel_samples_per_sample(spacing_of(c, channel_rate_hz)),
      front_taps(viterbi::front_filter_taps_any_width(channel_rate_hz, c.model.front_dot_s, c.front_filter_rise_s > 0.0 ? c.front_filter_rise_s : c.model.edge_rise_s)),
      output_noise(noise_of(c, channel, front_taps)),
      template_tables(template_of(c, channel, front_taps)),
      level_tables(c, output_noise, template_tables),
      model_tables(c) {
    (void)segmental::ChannelFloor(c);  // refuses its settings here, before any channel opens
    (void)segmental::EchoEstimator(c, template_tables);  // and the tail's and the Doppler walk's (text-fast-1)
    (void)segmental::DopplerEstimator(c, output_noise.rho);
    require(std::isfinite(c.leak_hold_s) && c.leak_hold_s >= 0.0, "segmental: leak_hold_s must be finite and not negative (s)");
    require(std::isfinite(c.leak_floor_db), "segmental: leak_floor_db must be finite (dB re the leak)");
    require(c.leak_min_offset_hz >= 0.0 && c.leak_max_offset_hz > c.leak_min_offset_hz && std::isfinite(c.leak_max_offset_hz),
            "segmental: the leak's offsets must rise from 0 Hz or above");
    if (c.leak_hold_s > 0.0) {
        // The same responses at any rate: the channel filter's length in s and the bin's width do not depend on it (segmental_tables_channel).
        const ChannelizerConfig tables = segmental_tables_channel(channel);
        const std::vector<double> cut = channel_filter_taps(tables);
        const double bin_hz = static_cast<double>(channel.sample_rate) / channel.fft_size;
        leak_first_bin = static_cast<std::size_t>(std::ceil(c.leak_min_offset_hz / bin_hz));
        const auto last = static_cast<std::size_t>(std::ceil(c.leak_max_offset_hz / bin_hz));
        leak_response.resize(last + 1);
        for (std::size_t k = 0; k <= last; ++k) {
            const double f_hz = static_cast<double>(k) * bin_hz;
            leak_response[k] = power_response(cut, tables.sample_rate, f_hz) * power_response(front_taps, channel_rate_hz, f_hz);
        }
        // A block is the engine's hop, half an FFT.
        const double block_s = 0.5 * channel.fft_size / channel.sample_rate;
        leak_hold_blocks = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(c.leak_hold_s / block_s)));
        require(c.leak_own == 0.0 || c.leak_own == 1.0, "segmental: leak_own must be 0 or 1");
        require(std::isfinite(c.leak_own_hold_s) && c.leak_own_hold_s >= 0.0, "segmental: leak_own_hold_s must be finite and not negative (s)");
        if (c.leak_own == 1.0) {
            // The window's length in s does not depend on the rate: the tables' FFT is as good as the full rate's.
            leak_own_bound = own_splatter_bound(tables.fft_size, leak_first_bin, last);
            leak_own_hold_blocks = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(c.leak_own_hold_s / block_s)));
        }
    }
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
      echo(std::make_unique<segmental::EchoEstimator>(p.config, p.template_tables)),
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
    choice_started_ = false;
    doppler_ = std::make_unique<segmental::DopplerEstimator>(shared_->config, shared_->output_noise.rho);
    walk_fast_ = false;
    walk_short_ = true;
    walk_now_ = 0.0;
}

void SegmentalDecoder::update_walk() {
    const segmental::SegmentalConfig& c = shared_->config;
    const double spread = doppler_->spread_hz();
    const bool loud = doppler_->level_db() >= c.doppler_min_db && doppler_->shape_ok(c.doppler_shape_lo, c.doppler_shape_hi) &&
                      doppler_->runs_hz() >= c.doppler_min_runs_ratio * spread;
    const bool fast = loud && (walk_fast_ ? spread >= c.doppler_slow_hz : spread >= c.doppler_fast_hz);
    const double walk = std::clamp(c.doppler_walk_per_hz * spread, c.level_walk_nepers_per_sqrt_s, c.doppler_walk_max);
    // A variant, off: short frames only once the tail is decided, where the runs are strong enough to learn one (review of text-fast-1:
    // a fresh track ran them for 6 to 8 s before its tail was in; config.hpp, doppler_hold_frames).
    bool decided = c.doppler_hold_frames == 0.0 || doppler_->level_db() < c.echo_min_db || c.echo_max_s == 0.0;
    for (const Stream& stream : streams_) decided = decided || stream.echo->decided();
    if (fast == walk_fast_ && (!fast || (std::fabs(walk - walk_now_) < 0.5 && decided == walk_short_))) return;
    walk_fast_ = fast;
    walk_short_ = decided;
    walk_now_ = fast ? walk : 0.0;
    for (Stream& stream : streams_) stream.evidence->set_walk(fast, walk, decided);
}

int SegmentalDecoder::leading_scale() const { return streams_[leader_].scale; }

double SegmentalDecoder::leak_floor_fs2(int scale) const {
    for (const Stream& stream : streams_)
        if (stream.scale == scale) return stream.leak_floor;
    return 0.0;
}

std::size_t SegmentalDecoder::streams_running() const { return streams_.size(); }

segmental::EchoShape SegmentalDecoder::echo_shape(int scale) const {
    for (const Stream& stream : streams_)
        if (stream.scale == scale) return stream.echo->shape();
    return {};
}

bool SegmentalDecoder::choose_leader(double stream_now_s, bool may_drop) {
    const segmental::SegmentalConfig& config = shared_->config;
    if (streams_.size() == 1) return false;
    const double now_s = stream_now_s - first_s_;  // the channel's age, a replay's seconds included
    if (!choice_started_) {
        if (now_s < config.filter_scale_start_s) return false;
        choice_started_ = true;
        for (Stream& stream : streams_) {
            stream.start_morse_over_free = stream.morse_over_free();
            stream.start_symbols = stream.symbols();
        }
    }
    const std::size_t before = leader_;
    const double wanted_symbols = config.filter_scale_min_symbols * streams_.front().symbols();
    const auto eligible = [&](std::size_t i) {
        return i == 0 || streams_[i].symbols() >= wanted_symbols || streams_[i].morse_over_free() >= config.filter_scale_enough_nats;
    };
    // The leader that no longer qualifies gives way to the widest, which always does.
    if (!eligible(leader_)) leader_ = 0;
    double top = streams_[leader_].morse_over_free();
    for (std::size_t i = 0; i < streams_.size(); ++i) {
        if (!eligible(i)) continue;
        const double value = streams_[i].morse_over_free();
        if (value > top + config.filter_scale_switch_nats) {
            top = value;
            leader_ = i;
        }
    }
    const bool changed = leader_ != before;
    if (may_drop && streams_.size() > 1 && config.filter_scale_drop_after_s >= 0.0 && now_s >= config.filter_scale_drop_after_s) {
        // The widest filter stays, whatever it reads: it is what a new, fast sender on this channel can be read with.
        const Stream* leading = &streams_[leader_];
        const int scale = leading->scale;
        std::size_t index = 0;
        std::vector<bool> may_lead(streams_.size());
        for (std::size_t i = 0; i < streams_.size(); ++i) may_lead[i] = eligible(i);
        std::erase_if(streams_, [&](const Stream& s) {
            const bool keep = may_lead[index++];
            return s.scale != 1 && s.scale != scale && (!keep || s.morse_over_free() < top - config.filter_scale_drop_margin_nats);
        });
        for (std::size_t i = 0; i < streams_.size(); ++i)
            if (streams_[i].scale == scale) leader_ = i;
    }
    return changed;
}

void SegmentalDecoder::FreeKey::step(double l1, double top) {
    const double new_up = (stay * up + change * down) * segmental::fast::exp_neg(-top);
    const double new_down = (stay * down + change * up) * segmental::fast::exp_neg(l1 - top);
    const double total = new_up + new_down;
    up = new_up / total;
    down = new_down / total;
    nats += top + segmental::fast::log(total);
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
        first_s_ = t0_s;
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
        const double hiss = n0_ * stream.parts->output_noise.sigma2_per_n0_hz;
        for (const viterbi::EnvelopeSample& sample : stream.envelope) {
            if (!(n0_ > 0.0)) continue;
            // Samples not searched (N0 = 0) leave the stream spliced; the time of the samples after them is noted.
            if (sample.k != stream.expected_k)
                stream.search->note_sample_time(stream.pushed, spacing * static_cast<double>(sample.k) / rate_);
            stream.expected_k = sample.k + 1;
            // The channel's floor, max(hiss, the channel floor), is what the level's grid is relative to; a neighbor's leak above it
            // is noise only (segmental/config.hpp, level_on_floor).
            const double floor = stream.floor->sigma2(sample.e_fs, hiss);
            const double sigma2 = std::max(floor, stream.leak_floor);
            if (stream.scale == 1 && doppler_->push(sample.y_fs, floor)) update_walk();
            if (stream.echo->push(sample.e_fs * sample.e_fs, floor)) {
                stream.evidence->set_echo(stream.echo->shape());
            }
            stream.evidence->push(sample.e_fs, sigma2, floor);
            stream.search->advance();  // at once: the short elements' context reaches back only so far
            stream.advance_free_key();
            ++stream.pushed;
        }
    }
    next_index_ += samples.size();
    {
        // The keying this channel hears, at the filter whose text goes out (Decoder::block_envelope_fs2).
        const std::vector<viterbi::EnvelopeSample>& heard = streams_[leader_].envelope;
        double sum = 0.0;
        for (const viterbi::EnvelopeSample& sample : heard) sum += sample.e_fs * sample.e_fs;
        block_envelope_fs2_ = heard.empty() ? -1.0 : sum / static_cast<double>(heard.size());
        block_noise_fs2_ = n0_ > 0.0 ? n0_ * streams_[leader_].parts->output_noise.sigma2_per_n0_hz : -1.0;
    }
    const double now_s = static_cast<double>(next_index_) / rate_;
    if (now_s < next_publish_s_) return {};
    next_publish_s_ = now_s + kPublishEveryS;
    const bool changed = choose_leader(now_s, true);
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
    const std::size_t base = story.length - story.tail.size();
    std::size_t k = base;
    while (k < published_.size() && k < story.length && published_[k] == story.tail[k - base].text) ++k;
    // The first symbol out whose margin has fallen a level since (the search's rivals reach a symbol after it went out).
    std::size_t c = base;
    while (c < k && published_levels_[c] == confidence_level(story.tail[c - base].probability)) ++c;
    const bool text_changed = k < published_.size();
    if (text_changed || c < k) {
        // The best story changed its mind about text already out, or about how sure it is of it: a correction from the first
        // symbol that differs.
        const std::size_t from = std::min(k, c);
        TextCorrection correction;
        correction.from_index = from;
        correction.chars.assign(story.tail.begin() + static_cast<std::ptrdiff_t>(from - base), story.tail.end());
        correction.t_s = now_s;
        correction.reason = text_changed ? "story" : "confidence";
        correction.reach_s = from < story.length ? now_s - story.tail[from - base].start_s : 0.0;
        update.corrections.push_back(std::move(correction));
        k = from;
    } else {
        update.chars.assign(story.tail.begin() + static_cast<std::ptrdiff_t>(k - base), story.tail.end());
    }
    published_.resize(k);
    published_levels_.resize(k);
    published_wpm_.resize(k);
    for (std::size_t i = k; i < story.length; ++i) {
        published_.push_back(story.tail[i - base].text);
        published_levels_.push_back(confidence_level(story.tail[i - base].probability));
        published_wpm_.push_back(letter_wpm(story.tail[i - base]));
    }
    // The text's speed, not the best story's at this instant: that one is the speed of whatever the story holds now, in a silence
    // a slow junk story's (backlog, "A long row's speed reads low at its end"). It changes only with the text, so between
    // transmissions it holds.
    update.wpm = text_wpm();
    return update;
}

float SegmentalDecoder::letter_wpm(const DecodedSymbol& symbol) {
    const std::string_view code = morse::encode(symbol.text);
    const double duration_s = symbol.end_s - symbol.start_s;
    if (code.empty() || !(duration_s > 0.0)) return 0.0f;
    double units = static_cast<double>(code.size()) - 1.0;  // the element gaps
    for (const char element : code) units += element == '-' ? 3.0 : 1.0;
    return static_cast<float>(1.2 * units / duration_s);  // PARIS: a dot lasts 1.2 s / WPM
}

float SegmentalDecoder::text_wpm() const {
    std::array<float, kSpeedLetters> recent{};
    std::size_t n = 0;
    for (std::size_t i = published_wpm_.size(); i-- > 0 && n < kSpeedLetters;) {
        if (published_wpm_[i] > 0.0f) recent[n++] = published_wpm_[i];
    }
    if (n == 0) return 0.0f;
    std::sort(recent.begin(), recent.begin() + static_cast<std::ptrdiff_t>(n));
    return n % 2 == 1 ? recent[n / 2] : 0.5f * (recent[n / 2 - 1] + recent[n / 2]);
}

std::uint8_t SegmentalDecoder::confidence_level(float probability) {
    // Half-nat steps of the margin, ln(p / (1 - p)), from 0 to 63.5 nats; 255 for 1 (no margins kept, or a letter open at the end).
    // Heuristic: finer than any use of it.
    if (!(probability < 1.0f)) return 255;
    const double margin = std::log(static_cast<double>(probability) / (1.0 - static_cast<double>(probability)));
    return static_cast<std::uint8_t>(std::clamp(std::floor(2.0 * margin), 0.0, 127.0));
}

DecodeUpdate SegmentalDecoder::flush() {
    if (flushed_) return {};
    flushed_ = true;
    for (Stream& stream : streams_) {
        stream.evidence->finish();
        stream.search->advance();
        stream.advance_free_key();
    }
    (void)choose_leader(static_cast<double>(next_index_) / rate_, false);
    const segmental::Search& search = *streams_[leader_].search;
    // The whole text, not the tail: the channel's last word on everything it has published.
    segmental::BestStory story = search.best_story(0);
    story.tail = search.best_text();
    story.length = story.tail.size();
    return bring_up_to_date(story, static_cast<double>(next_index_) / rate_);
}

void SegmentalDecoder::reset() {
    start();
    started_ = false;
    flushed_ = false;
    next_index_ = 0;
    first_s_ = 0.0;
    next_publish_s_ = 0.0;
    published_.clear();
    published_levels_.clear();
    published_wpm_.clear();
}

void SegmentalDecoder::set_band_spectrum(std::span<const float> power_fs2, int center_index) {
    const auto size = static_cast<std::ptrdiff_t>(power_fs2.size());
    for (Stream& stream : streams_) {
        const SegmentalParts& parts = *stream.parts;
        if (parts.leak_response.empty()) continue;
        // L = sum over the bins beside the channel of (P / 1.5) |H_channel|^2 |H_front|^2. A tone of amplitude A reads A^2 in its bin
        // under the analyzer's Hann window and 1.5 A^2 over all its bins (derived), so a neighbor's carrier counts once.
        // The channel's own key-down power, the largest of its three center bins held over leak_own_hold_s; a bin counts only above
        // what that carrier, keyed, can put there (leak_own_bound).
        double own = 0.0;
        if (!parts.leak_own_bound.empty()) {
            double now = 0.0;
            for (std::ptrdiff_t j = -1; j <= 1; ++j)
                if (center_index + j >= 0 && center_index + j < size) now = std::max(now, static_cast<double>(power_fs2[static_cast<std::size_t>(center_index + j)]));
            if (stream.own_held.empty()) stream.own_held.assign(parts.leak_own_hold_blocks, 0.0);
            stream.own_held[stream.own_next++ % stream.own_held.size()] = now;
            own = *std::max_element(stream.own_held.begin(), stream.own_held.end());
        }
        const auto counted = [&](std::size_t k, std::ptrdiff_t bin) {
            const double p = power_fs2[static_cast<std::size_t>(bin)];
            return parts.leak_own_bound.empty() ? p : std::max(0.0, p - parts.leak_own_bound[k] * own);
        };
        double leak = 0.0;
        for (std::size_t k = parts.leak_first_bin; k < parts.leak_response.size(); ++k) {
            const auto offset = static_cast<std::ptrdiff_t>(k);
            const std::ptrdiff_t below = center_index - offset, above = center_index + offset;
            if (below >= 0 && below < size) leak += parts.leak_response[k] * counted(k, below);
            if (above >= 0 && above < size) leak += parts.leak_response[k] * counted(k, above);
        }
        leak /= 1.5;
        if (stream.leak_held.empty()) stream.leak_held.assign(parts.leak_hold_blocks, 0.0);
        stream.leak_held[stream.leak_next++ % stream.leak_held.size()] = leak;
        stream.leak_floor = std::pow(10.0, parts.config.leak_floor_db / 10.0) * *std::max_element(stream.leak_held.begin(), stream.leak_held.end());
    }
}

void SegmentalDecoder::set_noise_density_fs2_per_hz(double n0_fs2_per_hz) {
    require(std::isfinite(n0_fs2_per_hz) && n0_fs2_per_hz >= 0.0,
            "SegmentalDecoder: the noise density must be finite and not negative (FS^2/Hz)");
    n0_ = n0_fs2_per_hz;
}

}  // namespace kz4ap
