#include "waterfall_widget.hpp"

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

void WaterfallWidget::start(double sample_rate_hz, int fft_bins) {
    axis_ = FrequencyAxis{sample_rate_hz, fft_bins};
    started_ = true;
    history_.clear();
    markers_.clear();
    floor_tracker_.reset();
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
        }
    }
    if (redraw) {
        rebuild_image();
    } else {
        // Only the rows that can show: a burst far taller than the plot needs its last height rows.
        const std::size_t count = std::min(rows.size(), static_cast<std::size_t>(image_.height()));
        for (std::size_t i = history_.size() - count; i < history_.size(); ++i) draw_row_into_image(history_[i]);
    }
    update();
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
    markers_ = std::move(markers);
    update();
}

void WaterfallWidget::set_floor_dbfs(double floor_dbfs) {
    if (auto_floor_) return;
    scale_.floor_dbfs = floor_dbfs;
    rebuild_image();
    update();
}

void WaterfallWidget::set_range_db(double range_db) {
    scale_.range_db = range_db;
    rebuild_image();
    update();
}

void WaterfallWidget::set_auto_floor(bool on) {
    auto_floor_ = on;
    if (on) {
        if (const auto floor = floor_tracker_.display_floor_dbfs()) {
            scale_.floor_dbfs = *floor;
            emit floorChanged(*floor);
            rebuild_image();
            update();
        }
    }
}

void WaterfallWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    rebuild_image();
}

void WaterfallWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QColor background = palette().color(QPalette::Window);
    const QColor foreground = palette().color(QPalette::WindowText);
    p.fillRect(rect(), background);
    const QRect plot = plot_rect();

    // The picture: the ring's newest part first.
    const int below = image_.height() - head_;
    p.drawImage(QRect(plot.left(), plot.top(), plot.width(), below), image_, QRect(0, head_, image_.width(), below));
    if (head_ > 0)
        p.drawImage(QRect(plot.left(), plot.top() + below, plot.width(), head_), image_,
                    QRect(0, 0, image_.width(), head_));

    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.9);
    p.setFont(small);

    p.setPen(foreground);
    p.drawText(QRect(plot.left(), 0, plot.width(), kTopTitle), Qt::AlignCenter, tr("Frequency (kHz from center)"));

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

        // The markers: a triangle and the track's number above the plot, a short line down into the picture, and for
        // the selected track a line all the way down.
        for (const Marker& m : markers_) {
            const double px = axis_.x_for_hz(m.freq_hz, plot.width());
            if (px < 0 || px > plot.width()) continue;
            const int x = plot.left() + static_cast<int>(std::lround(px));
            const QColor color = m.selected ? QColor(255, 120, 60) : (m.live ? QColor(70, 240, 110) : QColor(150, 150, 150));
            p.setPen(color);
            const QString tag = QString::number(m.id);
            const int tag_top = kTopTitle + kTopTicks;
            p.drawText(QRect(x - 20, tag_top - 1, 40, kTopMarkers - 6), Qt::AlignCenter, tag);
            QPainterPath triangle;
            triangle.moveTo(x - 4, plot.top() - 6);
            triangle.lineTo(x + 4, plot.top() - 6);
            triangle.lineTo(x, plot.top());
            triangle.closeSubpath();
            p.fillPath(triangle, color);
            if (m.selected) {
                p.setPen(QPen(color, 1, Qt::DashLine));
                p.drawLine(x, plot.top(), x, plot.bottom());
            } else {
                p.drawLine(x, plot.top(), x, plot.top() + 14);
            }
        }
    }

    // The time ruler: a label each kTimeLabelStepS of stream time, at the row it falls on.
    p.setPen(foreground);
    p.drawText(QRect(0, kTopTitle, kLeftRuler - 4, kTopTicks + kTopMarkers), Qt::AlignRight | Qt::AlignVCenter | Qt::TextWordWrap,
               tr("Stream time (s)"));
    const int rows_shown = std::min(static_cast<int>(history_.size()), plot.height());
    for (int y = 0; y + 1 < rows_shown; ++y) {
        const double t = history_[history_.size() - 1 - static_cast<std::size_t>(y)].time_s;
        const double older = history_[history_.size() - 2 - static_cast<std::size_t>(y)].time_s;
        if (std::floor(t / kTimeLabelStepS) != std::floor(older / kTimeLabelStepS)) {
            const int row_y = plot.top() + y;
            p.drawLine(plot.left() - 4, row_y, plot.left(), row_y);
            const QString label = QString::number(std::floor(t / kTimeLabelStepS) * kTimeLabelStepS, 'f', 0);
            p.drawText(QRect(0, row_y - 9, kLeftRuler - 8, 18), Qt::AlignRight | Qt::AlignVCenter, label);
        }
    }

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

    if (!started_) {
        p.setPen(foreground);
        p.drawText(plot, Qt::AlignCenter, tr("Open an I/Q recording to begin."));
    }
}

}  // namespace kz4ap::app
