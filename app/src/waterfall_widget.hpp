// The waterfall: newest row at the top, frequency across in kHz from the center, stream time down the left, the color
// scale on the right in dBFS, and a marker per track, which a click picks (trackClicked). The arithmetic is in core/waterfall.hpp.
#pragma once

#include "waterfall.hpp"

#include <QImage>
#include <QLine>
#include <QPixmap>
#include <QRect>
#include <QRegion>
#include <QString>
#include <QWidget>

#include <array>
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
    // Calls written on the picture, each just right of its frequency at the top, most important first: lanes of labels without
    // overlap (place_labels); what fits in none is left out. Empty in decoder-only mode.
    struct CallLabel {
        double freq_hz = 0;  // Hz from the center
        QString text;
        bool recent = true;  // heard lately: drawn brighter
    };
    void set_call_labels(std::vector<CallLabel> labels);
    // Lanes of call labels, and the gap kept between two labels in a lane, pixels (heuristics).
    static constexpr int kLabelLanes = 3;
    static constexpr int kLabelGapPx = 6;

    // The color scale. In automatic mode the floor follows the noise (NoiseFloorTracker) and floorChanged says so.
    void set_floor_dbfs(double floor_dbfs);
    void set_range_db(double range_db);
    void set_auto_floor(bool on);
    const DbScale& scale() const { return scale_; }

    // A click picks the marker nearest to it across, within this, pixels (heuristic: the marker's triangle is 8 pixels wide).
    static constexpr int kPickPx = 8;

signals:
    void floorChanged(double floor_dbfs);
    // A click on the picture or the markers' band at a marker: that track.
    void trackClicked(std::uint32_t id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    QRect plot_rect() const;
    void rebuild_image();
    void draw_row_into_image(const WaterfallRow& row);
    // What does not change with each row, drawn once and blitted: the background, the title, the axes, the color scale.
    void draw_chrome();
    // The markers' band above the plot, and their lines into it, which change only when a marker moves by a pixel.
    void draw_marker_band();
    QRect marker_band_rect() const;
    // What a new row moves: the picture and the time ruler beside it.
    QRegion scrolling_region() const;

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
    std::vector<int> marker_x_;   // pixels in the plot, one per marker, -1 outside it: what the band was drawn from
    QPixmap chrome_;
    QPixmap marker_band_;
    bool chrome_stale_ = true;
    bool markers_stale_ = true;
    std::array<std::vector<QLine>, 2> marker_lines_;  // into the plot: live, ended
    std::optional<QLine> selected_line_;
    std::vector<CallLabel> call_labels_;
    struct PlacedLabel {
        QRect rect;  // in the widget
        QString text;
        bool recent = true;
    };
    std::vector<PlacedLabel> placed_labels_;  // laid out for the plot's present width; redone with the labels or a resize
    void place_call_labels();
    QImage image_;       // the plot, used as a ring: head_ is the scan line of the newest row
    int head_ = 0;
    std::vector<float> scratch_;
};

}  // namespace kz4ap::app
