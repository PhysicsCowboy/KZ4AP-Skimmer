#include "kz4ap/recording_reader.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace kz4ap {

int decoder_rate_for(int file_rate) {
    int best = 0;
    for (int rate = 48000; rate <= 768000; rate *= 2) {
        if (rate == file_rate) return file_rate;
        if (rate < file_rate) best = rate;
    }
    return best == 0 ? file_rate : best;
}

namespace {

// Audio samples read from the file at a time.
constexpr std::size_t kAudioBlock = 4096;

bool holds_audio(const std::filesystem::path& path, RecordingInput input) {
    if (input == RecordingInput::Audio) return true;
    if (input == RecordingInput::Iq) return false;
    return wav_channels(path) == 1;
}

}  // namespace

RecordingReader::RecordingReader(const std::filesystem::path& path, RecordingOptions options) : options_(options) {
    if (holds_audio(path, options_.input)) {
        audio_.emplace(path, options_.audio_channel);
        rate_ = kAudioEngineRate;
        total_ = AudioAnalytic::outputs_for(audio_->total_samples(), audio_->sample_rate());
        start_audio_at(0);
        return;
    }
    iq_.emplace(path);
    rate_ = iq_->sample_rate();
    const int target = options_.resample ? decoder_rate_for(iq_->sample_rate()) : iq_->sample_rate();
    if (target != iq_->sample_rate()) {
        resampler_ = std::make_unique<RationalResampler>(iq_->sample_rate(), target);
        rate_ = target;
        total_ = RationalResampler::outputs_for(iq_->total_samples(), resampler_->up(), resampler_->down());
    } else {
        total_ = iq_->total_samples();
    }
}

RecordingReader::~RecordingReader() = default;

int RecordingReader::file_rate() const { return audio_ ? audio_->sample_rate() : iq_->sample_rate(); }

WavSampleFormat RecordingReader::sample_format() const { return audio_ ? audio_->sample_format() : iq_->sample_format(); }

const WavRecordingInfo& RecordingReader::info() const { return audio_ ? audio_->info() : iq_->info(); }

std::size_t RecordingReader::read(std::span<Sample> out) {
    if (audio_) return read_audio(out);
    const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(out.size(), total_ - position_));
    if (want == 0) return 0;
    std::size_t got = 0;
    if (!resampler_) {
        got = iq_->read(out.first(want));
        if (options_.mirror)
            for (std::size_t i = 0; i < got; ++i) out[i] = std::conj(out[i]);
        position_ += got;
        return got;
    }
    while (got < want) {
        if (ready_at_ < ready_.size()) {
            const std::size_t take = std::min(want - got, ready_.size() - ready_at_);
            std::copy_n(ready_.begin() + static_cast<std::ptrdiff_t>(ready_at_), take, out.begin() + static_cast<std::ptrdiff_t>(got));
            ready_at_ += take;
            got += take;
            continue;
        }
        ready_.clear();
        ready_at_ = 0;
        // Enough input for about the outputs still wanted, at least one block.
        const auto in_block = static_cast<std::size_t>(std::max<std::uint64_t>(
            4096, (want - got) * static_cast<std::uint64_t>(resampler_->down()) / static_cast<std::uint64_t>(resampler_->up()) + 1));
        input_.resize(in_block);
        const std::size_t n = iq_->read(input_);
        if (n == 0) {
            // The file's end: the outputs still owed stand on zeros past it, as before its start.
            input_.assign(static_cast<std::size_t>(resampler_->taps_per_phase()), Sample(0.0f, 0.0f));
            resampler_->push(input_, ready_);
            if (ready_.empty()) break;
            continue;
        }
        if (options_.mirror)
            for (std::size_t i = 0; i < n; ++i) input_[i] = std::conj(input_[i]);
        resampler_->push(std::span<const Sample>(input_).first(n), ready_);
    }
    position_ += got;
    return got;
}

void RecordingReader::start_audio_at(std::uint64_t sample) {
    // A fresh analytic step, fed from audio sample i0: a whole number of the reduced ratio's input period, so that its output
    // index i0 x up / down is whole, and far enough before `sample` that every filter has forgotten its start (the zeros it
    // takes for the audio before i0) by then.
    analytic_ = std::make_unique<AudioAnalytic>(audio_->sample_rate());
    const auto g = static_cast<std::uint64_t>(std::gcd(audio_->sample_rate(), kAudioEngineRate));
    const std::uint64_t up = kAudioEngineRate / g, down = static_cast<std::uint64_t>(audio_->sample_rate()) / g;
    std::uint64_t memory = analytic_->removed_samples() + static_cast<std::uint64_t>(analytic_->analytic_taps()) + 64;
    if (const RationalResampler* r = analytic_->resampler())
        memory += (static_cast<std::uint64_t>(r->taps_per_phase()) + 2) * up / down + 2;
    std::uint64_t first_input = 0;
    if (sample > memory) first_input = (sample - memory) * down / up / down * down;
    audio_->seek(first_input);
    ready_.clear();
    ready_at_ = 0;
    ready_index_ = first_input * up / down;
    position_ = sample;
}

std::size_t RecordingReader::read_audio(std::span<Sample> out) {
    const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(out.size(), total_ - position_));
    std::size_t got = 0;
    while (got < want) {
        // ready_ holds the outputs ready_index_ on; position_ is never below ready_index_.
        const std::uint64_t at = position_ - ready_index_;
        if (at < ready_.size()) {
            const std::size_t take = std::min<std::size_t>(want - got, ready_.size() - static_cast<std::size_t>(at));
            std::copy_n(ready_.begin() + static_cast<std::ptrdiff_t>(at), take, out.begin() + static_cast<std::ptrdiff_t>(got));
            if (options_.mirror)  // receiver audio from a lower sideband: the tone at f Hz to -f Hz
                for (std::size_t i = got; i < got + take; ++i) out[i] = std::conj(out[i]);
            got += take;
            position_ += take;
            continue;
        }
        ready_index_ += ready_.size();
        ready_.clear();
        audio_in_.resize(kAudioBlock);
        const std::size_t n = audio_->read(audio_in_);
        // Past the file's end the outputs still owed stand on zeros, as before its start.
        if (n == 0) std::fill(audio_in_.begin(), audio_in_.end(), 0.0f);
        analytic_->push(std::span<const float>(audio_in_).first(n == 0 ? kAudioBlock : n), ready_);
    }
    return got;
}

void RecordingReader::seek(std::uint64_t sample) {
    if (audio_) {
        start_audio_at(std::min(sample, total_));
        return;
    }
    position_ = std::min(sample, total_);
    if (!resampler_) {
        iq_->seek(position_);
        return;
    }
    resampler_->reset(position_);
    iq_->seek(resampler_->first_input());
    ready_.clear();
    ready_at_ = 0;
}

}  // namespace kz4ap
