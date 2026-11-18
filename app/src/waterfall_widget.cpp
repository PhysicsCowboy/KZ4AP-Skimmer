#include "waterfall_widget.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>

namespace kz4ap::app {

namespace {

// The rulers' sizes, pixels.
constexpr int kLeftRuler = 58;
constexpr int kRightRuler = 84;
constexpr int kTopTitle = 18;
constexpr int kTopTicks = 20;
constexpr int kTopMarkers = 18;
constexpr int kBottomMargin = 4;
// A change of the automatic floor smaller than this waits, dB: redrawing the whole picture for less is not worth it.
constexpr double kFloorRedrawStepDb = 1.0;
// Seconds between the time ruler's labels.
constexpr double kTimeLabelStepS = 5.0;

}  // namespace

WaterfallWidget::WaterfallWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(480, 200);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void WaterfallWidget::start(double sample_rate_hz, int fft_bins, std::optional<double> center_hz, double time_origin_s) {
    center_hz_ = center_hz;
    time_origin_s_ = time_origin_s;
    axis_ = FrequencyAxis{sample_rate_hz, fft_bins};
    started_ = true;
    history_.clear();
    markers_.clear();
    call_labels_.clear();
    placed_labels_.clear();
    floor_tracker_.reset();
    chrome_stale_ = true;
    markers_stale_ = true;
    marker_x_.clear();
    rebuild_image();
    update();
}

QRect WaterfallWidget::plot_rect() const {
    const int top = kTopTitle + kTopTicks + kTopMarkers;
    return QRect(kLeftRuler, top, std::max(1, width() - kLeftRuler - kRightRuler),
                 std::max(1, height() - top - kBottomMargin));
}

void WaterfallWidget::add_rows(std::vector<WaterfallRow>&& rows) {
    if (rows.empty()) return;
    for (WaterfallRow& row : rows) {
        floor_tracker_.update(row.dbfs);
        history_.push_back(std::move(row));
        if (history_.size() > kHistoryRows) history_.pop_front();
    }
    bool redraw = image_.isNull();
    if (auto_floor_) {
        if (const auto floor = floor_tracker_.display_floor_dbfs();
            floor && std::abs(*floor - scale_.floor_dbfs) >= kFloorRedrawStepDb) {
            scale_.floor_dbfs = *floor;
            emit floorChanged(*floor);
            redraw = true;
            chrome_stale_ = true;
        }
    }
    if (redraw) {
        rebuild_image();
    } else {
        // Only the rows that can show: a burst far taller than the plot needs its last height rows.
        const std::size_t count = std::min(rows.size(), static_cast<std::size_t>(image_.height()));
        for (std::size_t i = history_.size() - count; i < history_.size(); ++i) draw_row_into_image(history_[i]);
    }
    if (redraw) update();
    else update(scrolling_region());
}

void WaterfallWidget::draw_row_into_image(const WaterfallRow& row) {
    head_ = (head_ - 1 + image_.height()) % image_.height();
    auto* line = reinterpret_cast<std::uint32_t*>(image_.scanLine(head_));
    render_row(row.dbfs, scale_, scratch_, std::span<std::uint32_t>(line, static_cast<std::size_t>(image_.width())));
}

void WaterfallWidget::rebuild_image() {
    const QRect plot = plot_rect();
    image_ = QImage(plot.size(), QImage::Format_RGB32);
    image_.fill(static_cast<QRgb>(waterfall_palette()[0]));
    head_ = 0;
    const std::size_t count = std::min(history_.size(), static_cast<std::size_t>(image_.height()));
    for (std::size_t i = history_.size() - count; i < history_.size(); ++i) draw_row_into_image(history_[i]);
}

void WaterfallWidget::set_markers(std::vector<Marker> markers) {
    // Only a change that shows: a frequency moving within a pixel, or the same list again, costs no picture.
    const int width = plot_rect().width();
    std::vector<int> x;
    x.reserve(markers.size());
    for (const Marker& m : markers) {
        const double px = started_ ? axis_.x_for_hz(m.freq_hz, width) : -1.0;
        x.push_back(px < 0 || px > width ? -1 : static_cast<int>(std::lround(px)));
    }
    const auto same_marker = [](const Marker& a, const Marker& b) { return a.id == b.id && a.live == b.live && a.selected == b.selected; };
    const bool same = !markers_stale_ && x == marker_x_ && std::equal(markers.begin(), markers.end(), markers_.begin(), markers_.end(), same_marker);
    markers_ = std::move(markers);
    if (same) return;
    marker_x_ = std::move(x);
    markers_stale_ = true;
    update(marker_band_rect());
    update(plot_rect());
}

void WaterfallWidget::set_call_labels(std::vector<CallLabel> labels) {
    const auto same = [](const CallLabel& a, const CallLabel& b) {
        return a.freq_hz == b.freq_hz && a.text == b.text && a.recent == b.recent;
    };
    if (std::equal(labels.begin(), labels.end(), call_labels_.begin(), call_labels_.end(), same)) return;
    call_labels_ = std::move(labels);
    place_call_labels();
    update(plot_rect());
}

void WaterfallWidget::place_call_labels() {
    placed_labels_.clear();
    if (!started_ || call_labels_.empty()) return;
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.9);
    const QFontMetrics metrics(small);
    const QRect plot = plot_rect();
    const int height = metrics.height() + 2;  // pixels
    std::vector<LabelSpan> spans;
    std::vector<std::size_t> which;
    for (std::size_t i = 0; i < call_labels_.size(); ++i) {
        const double px = axis_.x_for_hz(call_labels_[i].freq_hz, plot.width());
        if (px < 0 || px > plot.width()) continue;
        // Just right of the trace, so that the trace itself stays in view; near the right edge, moved left to end there.
        const double width = metrics.horizontalAdvance(call_labels_[i].text) + 6;
        spans.push_back({std::max(0.0, std::min(px + 3.0, plot.width() - width)), width});
        which.push_back(i);
    }
    const std::vector<std::optional<int>> lanes = place_labels(spans, kLabelLanes, kLabelGapPx);
    for (std::size_t k = 0; k < spans.size(); ++k) {
        if (!lanes[k]) continue;
        const QRect rect(plot.left() + static_cast<int>(std::lround(spans[k].left_px)), plot.top() + 16 + *lanes[k] * (height + 1),
                         static_cast<int>(std::lround(spans[k].width_px)), height);
        placed_labels_.push_back({rect, call_labels_[which[k]].text, call_labels_[which[k]].recent});
    }
}

void WaterfallWidget::mousePressEvent(QMouseEvent* event) {
    QWidget::mousePressEvent(event);
    if (!started_ || event->button() != Qt::LeftButton) return;
    const QRect plot = plot_rect();
    const QPoint at = event->position().toPoint();
    if (at.x() < plot.left() || at.x() > plot.right() || at.y() < marker_band_rect().top() || at.y() > plot.bottom()) return;
    const int x = at.x() - plot.left();
    std::optional<std::uint32_t> best;
    int best_px = kPickPx + 1;
    for (std::size_t i = 0; i < markers_.size() && i < marker_x_.size(); ++i) {
        if (marker_x_[i] < 0) continue;
        const int px = std::abs(marker_x_[i] - x);
        // The nearer; at the same distance a live one. (marker_x_ is set with the markers, at most a tick after a resize.)
        const bool better = px < best_px || (px == best_px && best && markers_[i].live);
        if (better) {
            best_px = px;
            best = markers_[i].id;
        }
    }
    if (best) emit trackClicked(*best);
}

void WaterfallWidget::set_floor_dbfs(double floor_dbfs) {
    if (auto_floor_) return;
    chrome_stale_ = true;
    scale_.floor_dbfs = floor_dbfs;
    rebuild_image();
    update();
}

void WaterfallWidget::set_range_db(double range_db) {
    chrome_stale_ = true;
    scale_.range_db = range_db;
    rebuild_image();
    update();
}

void WaterfallWidget::set_auto_floor(bool on) {
    auto_floor_ = on;
    if (on) {
        if (const auto floor = floor_tracker_.display_floor_dbfs()) {
            chrome_stale_ = true;
            scale_.floor_dbfs = *floor;
            emit floorChanged(*floor);
            rebuild_image();
            update();
        }
    }
}

void WaterfallWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    chrome_stale_ = true;
    markers_stale_ = true;
    rebuild_image();
    place_call_labels();
}

void WaterfallWidget::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange) {
        chrome_stale_ = true;
        markers_stale_ = true;
        update();
    }
}

QRect WaterfallWidget::marker_band_rect() const {
    // Over the plot only: beside it stand the time ruler's title and the color scale's.
    const QRect plot = plot_rect();
    return QRect(plot.left(), plot.top() - kTopMarkers, plot.width(), kTopMarkers);
}

QRegion WaterfallWidget::scrolling_region() const {
    const QRect plot = plot_rect();
    // The ruler's labels stand 9 pixels either side of their row.
    return QRegion(plot) + QRegion(QRect(0, plot.top() - 9, kLeftRuler, plot.height() + 18));
}

void WaterfallWidget::draw_chrome() {
    const qreal ratio = devicePixelRatioF();
    if (chrome_.size() != size() * ratio || chrome_.devicePixelRatio() != ratio) {
        chrome_ = QPixmap(size() * ratio);
        chrome_.setDevicePixelRatio(ratio);
    }
    chrome_stale_ = false;
    QPainter p(&chrome_);
    const QColor background = palette().color(QPalette::Window);
    const QColor foreground = palette().color(QPalette::WindowText);
    p.fillRect(rect(), background);
    const QRect plot = plot_rect();

    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.9);
    p.setFont(small);

    p.setPen(foreground);
    p.drawText(QRect(plot.left(), 0, plot.width(), kTopTitle), Qt::AlignCenter,
               center_hz_ ? tr("Frequency (kHz from %1 kHz)").arg(*center_hz_ / 1000.0, 0, 'f', 3) : tr("Frequency (kHz from center)"));

    // The frequency axis.
    if (started_) {
        const double lo_khz = axis_.lowest_hz() / 1000.0;
        const double hi_khz = axis_.highest_hz() / 1000.0;
        const int ticks_bottom = kTopTitle + kTopTicks;
        for (const double khz : round_ticks(lo_khz, hi_khz, plot.width(), 70.0)) {
            const int x = plot.left() + static_cast<int>(std::lround(axis_.x_for_hz(khz * 1000.0, plot.width())));
            p.setPen(foreground);
            p.drawLine(x, ticks_bottom - 4, x, ticks_bottom);
            const QString label = khz == 0.0 ? QStringLiteral("0") : QString::asprintf("%+g", khz);
            p.drawText(QRect(x - 40, kTopTitle, 80, kTopTicks - 4), Qt::AlignCenter, label);
        }
    }

    p.setPen(foreground);
    p.drawText(QRect(0, kTopTitle, kLeftRuler - 4, kTopTicks + kTopMarkers), Qt::AlignRight | Qt::AlignVCenter | Qt::TextWordWrap,
               tr("Stream time (s)"));

    // The color scale, with its reference: dBFS in one FFT bin.
    const QRect bar(plot.right() + 10, plot.top(), 14, plot.height());
    for (int y = 0; y < bar.height(); ++y) {
        const double level = 1.0 - static_cast<double>(y) / std::max(1, bar.height() - 1);
        p.setPen(QColor::fromRgb(static_cast<QRgb>(level_to_rgb(level))));
        p.drawLine(bar.left(), bar.top() + y, bar.right(), bar.top() + y);
    }
    p.setPen(foreground);
    const QString scale_title =
        started_ ? tr("dBFS in a %1 Hz bin").arg(axis_.sample_rate_hz / axis_.fft_bins, 0, 'f', 1) : tr("dBFS");
    p.drawText(QRect(plot.right() + 4, 0, kRightRuler - 6, plot.top() - 2), Qt::AlignLeft | Qt::AlignBottom | Qt::TextWordWrap,
               scale_title);
    const double scale_lo = scale_.floor_dbfs;
    const double scale_hi = scale_.floor_dbfs + std::max(scale_.range_db, DbScale::kMinRangeDb);
    for (const double db : round_ticks(scale_lo, scale_hi, bar.height(), 40.0)) {
        const int y = bar.top() + static_cast<int>(std::lround((scale_hi - db) / (scale_hi - scale_lo) * (bar.height() - 1)));
        p.drawLine(bar.right() + 1, y, bar.right() + 4, y);
        p.drawText(QRect(bar.right() + 6, y - 9, kRightRuler - 30, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(db, 'f', 0));
    }
}

void WaterfallWidget::draw_marker_band() {
    markers_stale_ = false;
    const qreal ratio = devicePixelRatioF();
    const QRect band = marker_band_rect();
    if (marker_band_.size() != band.size() * ratio || marker_band_.devicePixelRatio() != ratio) {
        marker_band_ = QPixmap(band.size() * ratio);
        marker_band_.setDevicePixelRatio(ratio);
    }
    marker_band_.fill(palette().color(QPalette::Window));
    for (auto& lines : marker_lines_) lines.clear();
    selected_line_.reset();
    if (!started_) return;
    QPainter p(&marker_band_);
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.9);
    p.setFont(small);
    const QRect plot = plot_rect();
    // The markers: a triangle and the track's number above the plot, a short line down into the picture, and for
    // the selected track a line all the way down.
    const int width = plot.width();
    for (const Marker& m : markers_) {
        const double px = axis_.x_for_hz(m.freq_hz, width);
        if (px < 0 || px > width) continue;
        const int x = plot.left() + static_cast<int>(std::lround(px));
        const QColor color = m.selected ? QColor(255, 120, 60) : (m.live ? QColor(70, 240, 110) : QColor(150, 150, 150));
        p.setPen(color);
        const int band_x = x - plot.left();
        p.drawText(QRect(band_x - 20, -1, 40, kTopMarkers - 6), Qt::AlignCenter, QString::number(m.id));
        QPainterPath triangle;
        triangle.moveTo(band_x - 4, kTopMarkers - 6);
        triangle.lineTo(band_x + 4, kTopMarkers - 6);
        triangle.lineTo(band_x, kTopMarkers);
        triangle.closeSubpath();
        p.fillPath(triangle, color);
        if (m.selected) selected_line_ = QLine(x, plot.top(), x, plot.bottom());
        else marker_lines_[m.live ? 0 : 1].push_back(QLine(x, plot.top(), x, plot.top() + 14));
    }
}

void WaterfallWidget::paintEvent(QPaintEvent* event) {
    if (chrome_stale_ || chrome_.size() != size() * devicePixelRatioF()) draw_chrome();
    if (markers_stale_) draw_marker_band();
    QPainter p(this);
    const QRect plot = plot_rect();

    // What stands around the picture, only where this paint reaches it.
    const QRegion around = event->region() - plot;
    if (!around.isEmpty()) {
        p.setClipRegion(around);
        p.drawPixmap(0, 0, chrome_);
        if (started_) p.drawPixmap(marker_band_rect().topLeft(), marker_band_);
        p.setClipping(false);
    }

    // The picture: the ring's newest part first.
    const int below = image_.height() - head_;
    p.drawImage(QRect(plot.left(), plot.top(), plot.width(), below), image_, QRect(0, head_, image_.width(), below));
    if (head_ > 0)
        p.drawImage(QRect(plot.left(), plot.top() + below, plot.width(), head_), image_,
                    QRect(0, 0, image_.width(), head_));

    if (started_) {
        p.setPen(QColor(70, 240, 110));
        p.drawLines(marker_lines_[0].data(), static_cast<int>(marker_lines_[0].size()));
        p.setPen(QColor(150, 150, 150));
        p.drawLines(marker_lines_[1].data(), static_cast<int>(marker_lines_[1].size()));
        if (selected_line_) {
            p.setPen(QPen(QColor(255, 120, 60), 1, Qt::DashLine));
            p.drawLine(*selected_line_);
        }
        // The calls, below the markers' short lines, each on a dark box so it reads over any color.
        if (!placed_labels_.empty()) {
            QFont small = font();
            small.setPointSizeF(font().pointSizeF() * 0.9);
            p.setFont(small);
            for (const PlacedLabel& label : placed_labels_) {
                p.fillRect(label.rect, QColor(0, 0, 0, 170));
                p.setPen(label.recent ? QColor(255, 255, 255) : QColor(170, 170, 170));
                p.drawText(label.rect, Qt::AlignCenter, label.text);
            }
        }
    }

    // The time ruler: a label each kTimeLabelStepS of stream time, at the row it falls on.
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.9);
    p.setFont(small);
    p.setPen(palette().color(QPalette::WindowText));
    const int rows_shown = std::min(static_cast<int>(history_.size()), plot.height());
    for (int y = 0; y + 1 < rows_shown; ++y) {
        const double t = time_origin_s_ + history_[history_.size() - 1 - static_cast<std::size_t>(y)].time_s;
        const double older = time_origin_s_ + history_[history_.size() - 2 - static_cast<std::size_t>(y)].time_s;
        if (std::floor(t / kTimeLabelStepS) != std::floor(older / kTimeLabelStepS)) {
            const int row_y = plot.top() + y;
            p.drawLine(plot.left() - 4, row_y, plot.left(), row_y);
            const QString label = QString::number(std::floor(t / kTimeLabelStepS) * kTimeLabelStepS, 'f', 0);
            p.drawText(QRect(0, row_y - 9, kLeftRuler - 8, 18), Qt::AlignRight | Qt::AlignVCenter, label);
        }
    }

    if (!started_) {
        p.drawText(plot, Qt::AlignCenter, tr("Open an I/Q recording, or press Start radio, to begin."));
    }
}

}  // namespace kz4ap::app
