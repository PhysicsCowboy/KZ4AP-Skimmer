// The waterfall's arithmetic, free of Qt so it can be tested without a display: spectrum frames to rows, bins to pixel
// columns, dBFS to color, frequency to position. Every value here is a display choice, not signal processing: none
// reaches the detector or a decoder. docs/decision-log.md has the reasons for the heuristic ones.
#pragma once

#include "kz4ap/spectrum.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace kz4ap::app {

// One line of the waterfall.
struct WaterfallRow {
    double time_s = 0;          // stream time just after the row's last input sample, s
    std::vector<float> dbfs;    // columns, lowest frequency first: dBFS in one FFT bin (a complex tone of amplitude
                                // 1 FS reads 0 dBFS), the largest of the bins a column covers
};

// dBFS to a display level: floor_dbfs maps to 0, floor_dbfs + range_db to 1, linear in dB between, clamped outside.
struct DbScale {
    double floor_dbfs = -80.0;
    double range_db = 50.0;  // dB above the floor; at least kMinRangeDb is used

    static constexpr double kMinRangeDb = 1.0;

    double level(double dbfs) const;
};

// The largest value of each column's bins: a narrow carrier survives any reduction, where an average would sink it into
// the noise by 10 log10(bins per column) dB. out.size() columns over in.size() bins; with more columns than bins each
// column takes its nearest bin. Empty in or out: nothing is written.
void reduce_to_columns(std::span<const float> in, std::span<float> out);

// 256 colors, 0xFFRRGGBB (QImage::Format_RGB32's layout), dark to bright: navy, blue, teal, yellow, white. A
// perceptually ordered sequence chosen by eye (heuristic).
const std::array<std::uint32_t, 256>& waterfall_palette();
// The palette's color for a level in [0, 1] (clamped).
std::uint32_t level_to_rgb(double level);

// One row as pixels: reduces the row to pixels.size() columns (scratch keeps the allocation) and colors them.
void render_row(std::span<const float> dbfs, const DbScale& scale, std::vector<float>& scratch,
                std::span<std::uint32_t> pixels);

// The noise floor of a row, dBFS: the median over its columns, which holds while signals occupy fewer than half of them.
float median_dbfs(std::span<const float> row);

// The automatic floor: the rows' median, smoothed so the picture does not breathe. Exponential, kAlphaPerRow per row
// (about 50 rows, 2 s at 25 rows/s; heuristic).
class NoiseFloorTracker {
public:
    static constexpr double kAlphaPerRow = 0.02;
    // The display floor sits this far below the median, dB, so the noise shows as texture rather than black. Heuristic.
    static constexpr double kFloorBelowMedianDb = 5.0;

    void update(std::span<const float> row);
    std::optional<double> median_dbfs() const { return median_; }
    // The floor to display, dBFS; none before the first row.
    std::optional<double> display_floor_dbfs() const;
    void reset() { median_.reset(); }

private:
    std::optional<double> median_;
};

// Frequency against horizontal position, for a spectrum of fft_bins bins at sample_rate_hz drawn over width_px: bin i
// is at (i - fft_bins/2) x sample_rate_hz / fft_bins Hz (SpectrumFrame), and occupies [i, i + 1) x width_px / fft_bins,
// so its frequency falls at the center of that stretch.
struct FrequencyAxis {
    double sample_rate_hz = 48000;
    int fft_bins = 2048;

    double x_for_hz(double hz, double width_px) const;
    double hz_for_x(double x_px, double width_px) const;
    double lowest_hz() const;   // the left edge's frequency, Hz
    double highest_hz() const;  // the right edge's, Hz
};

// Round tick values in [lo, hi]: multiples of the smallest step of 1, 2 or 5 x 10^n that leaves at least min_spacing_px
// between ticks over span_px. Empty if the range or the span is not positive.
std::vector<double> round_ticks(double lo, double hi, double span_px, double min_spacing_px);

// Spectrum frames to rows, on the pipeline's thread: frames_per_row frames averaged in power (the physically meaningful
// average; it narrows the noise's spread, which is what makes a weak trace visible), and columns reduced to at most
// max_columns by an integer factor of bins (reduce_to_columns), so rows from a wide span stay a bounded size.
class RowBuilder {
public:
    // Throws std::invalid_argument if frames_per_row or max_columns is below 1.
    RowBuilder(int frames_per_row, int max_columns);
    // The finished row, once per frames_per_row frames; none otherwise. A frame of another size than the last starts
    // the row again.
    std::optional<WaterfallRow> push(const SpectrumFrame& frame);

private:
    int frames_per_row_;
    int max_columns_;
    int frames_ = 0;
    std::vector<float> power_;  // linear power summed over the row's frames, FS^2 per bin
};

// frames per row for a target row period, s, at a hop of hop_s, s: the nearest whole number, at least 1.
int frames_per_row_for(double row_period_s, double hop_s);

}  // namespace kz4ap::app
