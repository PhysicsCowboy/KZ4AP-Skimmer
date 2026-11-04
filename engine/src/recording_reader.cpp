#include "kz4ap/recording_reader.hpp"

#include <algorithm>

namespace kz4ap {

int decoder_rate_for(int file_rate) {
    int best = 0;
    for (int rate = 48000; rate <= 768000; rate *= 2) {
        if (rate == file_rate) return file_rate;
        if (rate < file_rate) best = rate;
    }
    return best == 0 ? file_rate : best;
}

RecordingReader::RecordingReader(const std::filesystem::path& path, RecordingOptions options)
    : reader_(path), options_(options), rate_(reader_.sample_rate()) {
    const int target = options_.resample ? decoder_rate_for(reader_.sample_rate()) : reader_.sample_rate();
    if (target != reader_.sample_rate()) {
        resampler_ = std::make_unique<RationalResampler>(reader_.sample_rate(), target);
        rate_ = target;
        total_ = RationalResampler::outputs_for(reader_.total_samples(), resampler_->up(), resampler_->down());
    } else {
        total_ = reader_.total_samples();
    }
}

std::size_t RecordingReader::read(std::span<Sample> out) {
    const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(out.size(), total_ - position_));
    if (want == 0) return 0;
    std::size_t got = 0;
    if (!resampler_) {
        got = reader_.read(out.first(want));
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
        const std::size_t n = reader_.read(input_);
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

void RecordingReader::seek(std::uint64_t sample) {
    position_ = std::min(sample, total_);
    if (!resampler_) {
        reader_.seek(position_);
        return;
    }
    resampler_->reset(position_);
    reader_.seek(resampler_->first_input());
    ready_.clear();
    ready_at_ = 0;
}

}  // namespace kz4ap
