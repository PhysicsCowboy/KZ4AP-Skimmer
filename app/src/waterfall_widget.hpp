// The waterfall: newest row at the top, frequency across in kHz from the center, stream time down the left, the color
// scale on the right in dBFS, and a marker per track. The arithmetic is in core/waterfall.hpp.
#pragma once

#include "waterfall.hpp"

#include <QImage>
#include <QWidget>

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace kz4ap::app {

class WaterfallWidget : public QWidget {
    Q_OBJECT

public:
    struct Marker {
        std::uint32_t id = 0;
        double freq_hz = 0;
        bool live = true;
        bool selected = false;
    };

    explicit WaterfallWidget(QWidget* parent = nullptr);

    // A new stream: forgets all rows.
    // center_hz: the radio's tuned frequency, when known, named in the title; time_origin_s: the stream time of the rows' zero, s
    // (a stream begun part way into a recording).
    void start(double sample_rate_hz, int fft_bins, std::optional<double> center_hz = std::nullopt, double time_origin_s = 0.0);
    void add_rows(std::vector<WaterfallRow>&& rows);
    void set_markers(std::vector<Marker> markers);

    // The color scale. In automatic mode the floor follows the noise (NoiseFloorTracker) and floorChanged says so.
    void set_floor_dbfs(double floor_dbfs);
    void set_range_db(double range_db);
    void set_auto_floor(bool on);
    const DbScale& scale() const { return scale_; }

signals:
    void floorChanged(double floor_dbfs);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QRect plot_rect() const;
    void rebuild_image();
    void draw_row_into_image(const WaterfallRow& row);

    // Rows kept to redraw after a resize or a change of scale, newest last; a 4K display's height (heuristic).
    static constexpr std::size_t kHistoryRows = 2160;

    FrequencyAxis axis_;
    bool started_ = false;
    std::optional<double> center_hz_;
    double time_origin_s_ = 0.0;
    DbScale scale_;
    bool auto_floor_ = true;
    NoiseFloorTracker floor_tracker_;
    std::deque<WaterfallRow> history_;
    std::vector<Marker> markers_;
    QImage image_;       // the plot, used as a ring: head_ is the scan line of the newest row
    int head_ = 0;
    std::vector<float> scratch_;
};

}  // namespace kz4ap::app
