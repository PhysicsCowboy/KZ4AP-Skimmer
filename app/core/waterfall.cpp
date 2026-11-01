#include "waterfall.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kz4ap::app {

double DbScale::level(double dbfs) const {
    return std::clamp((dbfs - floor_dbfs) / std::max(range_db, kMinRangeDb), 0.0, 1.0);
}

void reduce_to_columns(std::span<const float> in, std::span<float> out) {
    if (in.empty() || out.empty()) return;
    const std::size_t bins = in.size();
    const std::size_t columns = out.size();
    if (columns >= bins) {
        for (std::size_t c = 0; c < columns; ++c) out[c] = in[c * bins / columns];
        return;
    }
    for (std::size_t c = 0; c < columns; ++c) {
        const std::size_t first = c * bins / columns;
        const std::size_t last = (c + 1) * bins / columns;
        out[c] = *std::max_element(in.begin() + static_cast<std::ptrdiff_t>(first),
                                   in.begin() + static_cast<std::ptrdiff_t>(last));
    }
}

const std::array<std::uint32_t, 256>& waterfall_palette() {
    static const std::array<std::uint32_t, 256> palette = [] {
        struct Stop {
            double at, r, g, b;
        };
        static constexpr std::array<Stop, 5> stops = {
            Stop{0.00, 0, 0, 30}, Stop{0.25, 0, 50, 170}, Stop{0.50, 0, 180, 170}, Stop{0.75, 250, 220, 0},
            Stop{1.00, 255, 255, 255}};
        std::array<std::uint32_t, 256> out{};
        for (std::size_t i = 0; i < out.size(); ++i) {
            const double t = static_cast<double>(i) / 255.0;
            std::size_t k = 0;
            while (k + 2 < stops.size() && t > stops[k + 1].at) ++k;
            const Stop& a = stops[k];
            const Stop& b = stops[k + 1];
            const double f = (t - a.at) / (b.at - a.at);
            const auto channel = [f](double x, double y) {
                return static_cast<std::uint32_t>(std::lround(x + f * (y - x)));
            };
            out[i] = 0xFF000000u | (channel(a.r, b.r) << 16) | (channel(a.g, b.g) << 8) | channel(a.b, b.b);
        }
        return out;
    }();
    return palette;
}

std::uint32_t level_to_rgb(double level) {
    const auto index = static_cast<std::size_t>(std::lround(std::clamp(level, 0.0, 1.0) * 255.0));
    return waterfall_palette()[index];
}

void render_row(std::span<const float> dbfs, const DbScale& scale, std::vector<float>& scratch,
                std::span<std::uint32_t> pixels) {
    scratch.resize(pixels.size());
    reduce_to_columns(dbfs, scratch);
    for (std::size_t x = 0; x < pixels.size(); ++x)
        pixels[x] = dbfs.empty() ? waterfall_palette()[0] : level_to_rgb(scale.level(scratch[x]));
}

float median_dbfs(std::span<const float> row) {
    if (row.empty()) return 0.0f;
    std::vector<float> copy(row.begin(), row.end());
    const auto middle = copy.begin() + static_cast<std::ptrdiff_t>(copy.size() / 2);
    std::nth_element(copy.begin(), middle, copy.end());
    return *middle;
}

void NoiseFloorTracker::update(std::span<const float> row) {
    if (row.empty()) return;
    const double m = kz4ap::app::median_dbfs(row);
    median_ = median_ ? *median_ + kAlphaPerRow * (m - *median_) : m;
}

std::optional<double> NoiseFloorTracker::display_floor_dbfs() const {
    if (!median_) return std::nullopt;
    return *median_ - kFloorBelowMedianDb;
}

double FrequencyAxis::x_for_hz(double hz, double width_px) const {
    const double bin_hz = sample_rate_hz / fft_bins;
    return (hz / bin_hz + fft_bins / 2.0 + 0.5) * width_px / fft_bins;
}

double FrequencyAxis::hz_for_x(double x_px, double width_px) const {
    const double bin_hz = sample_rate_hz / fft_bins;
    return (x_px * fft_bins / width_px - fft_bins / 2.0 - 0.5) * bin_hz;
}

double FrequencyAxis::lowest_hz() const { return (-fft_bins / 2.0 - 0.5) * sample_rate_hz / fft_bins; }

double FrequencyAxis::highest_hz() const { return (fft_bins / 2.0 - 0.5) * sample_rate_hz / fft_bins; }

std::vector<double> round_ticks(double lo, double hi, double span_px, double min_spacing_px) {
    std::vector<double> ticks;
    if (!(hi > lo) || !(span_px > 0) || !(min_spacing_px > 0)) return ticks;
    const double smallest = (hi - lo) * min_spacing_px / span_px;  // the step that just keeps the spacing
    const double decade = std::pow(10.0, std::floor(std::log10(smallest)));
    double step = 10.0 * decade;
    for (const double mantissa : {1.0, 2.0, 5.0}) {
        if (mantissa * decade >= smallest) {
            step = mantissa * decade;
            break;
        }
    }
    for (double k = std::ceil(lo / step); k * step <= hi; k += 1.0) ticks.push_back(k * step);
    return ticks;
}

RowBuilder::RowBuilder(int frames_per_row, int max_columns) : frames_per_row_(frames_per_row), max_columns_(max_columns) {
    if (frames_per_row < 1) throw std::invalid_argument("a waterfall row needs at least one frame");
    if (max_columns < 1) throw std::invalid_argument("a waterfall row needs at least one column");
}

std::optional<WaterfallRow> RowBuilder::push(const SpectrumFrame& frame) {
    const std::size_t bins = frame.power_db.size();
    if (bins == 0) return std::nullopt;
    if (power_.size() != bins) {
        power_.assign(bins, 0.0f);
        frames_ = 0;
    }
    if (frame.power_fs2.size() == bins) {
        for (std::size_t i = 0; i < bins; ++i) power_[i] += frame.power_fs2[i];
    } else {
        for (std::size_t i = 0; i < bins; ++i) power_[i] += std::pow(10.0f, 0.1f * frame.power_db[i]);
    }
    if (++frames_ < frames_per_row_) return std::nullopt;

    // The whole number of bins per column that brings the row within max_columns.
    const std::size_t factor = (bins + static_cast<std::size_t>(max_columns_) - 1) / static_cast<std::size_t>(max_columns_);
    const std::size_t columns = bins / factor + (bins % factor != 0 ? 1 : 0);
    WaterfallRow row;
    row.time_s = frame.time_s;
    row.dbfs.resize(columns);
    const float scale = 1.0f / static_cast<float>(frames_);
    for (std::size_t c = 0; c < columns; ++c) {
        float most = 0.0f;
        const std::size_t end = std::min(bins, (c + 1) * factor);
        for (std::size_t i = c * factor; i < end; ++i) most = std::max(most, power_[i]);
        // -300 dBFS for a column of exact zeros, far below any display floor.
        row.dbfs[c] = most > 0.0f ? 10.0f * std::log10(most * scale) : -300.0f;
    }
    std::fill(power_.begin(), power_.end(), 0.0f);
    frames_ = 0;
    return row;
}

int frames_per_row_for(double row_period_s, double hop_s) {
    return std::max(1, static_cast<int>(std::lround(row_period_s / hop_s)));
}

}  // namespace kz4ap::app
