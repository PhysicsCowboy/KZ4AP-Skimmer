#include "track_list_model.hpp"

#include <QBrush>
#include <QColor>
#include <QFontDatabase>

namespace kz4ap::app {

void TrackListModel::set_bin_width_hz(double bin_hz) {
    bin_hz_ = bin_hz;
    emit headerDataChanged(Qt::Horizontal, kLevel, kLevel);
}

void TrackListModel::refresh() {
    if (table_.structure_version() != seen_version_) {
        beginResetModel();
        seen_version_ = table_.structure_version();
        endResetModel();
        emit layoutRedone();
    } else if (rowCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, kColumnCount - 1));
    }
}

void TrackListModel::clear() {
    beginResetModel();
    table_ = TrackTable{};
    seen_version_ = table_.structure_version();
    endResetModel();
}

std::optional<std::uint32_t> TrackListModel::track_at(int row) const {
    if (row < 0 || row >= rowCount()) return std::nullopt;
    return table_.rows()[static_cast<std::size_t>(row)].id;
}

int TrackListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(table_.rows().size());
}

int TrackListModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : kColumnCount; }

QVariant TrackListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const TrackRow& row = table_.rows()[static_cast<std::size_t>(index.row())];
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case kTrack: return row.id;
            case kFrequency: return QString::asprintf("%+.3f", row.freq_hz / 1000.0);
            case kSpeed: return row.wpm > 0 ? QString::number(row.wpm, 'f', 1) : QStringLiteral("-");
            case kLevel: return QString::number(row.level_db, 'f', 1);
            case kText: return QString::fromStdString(row.text_tail(kTextSymbols));
            default: return {};
        }
    }
    if (role == Qt::TextAlignmentRole && index.column() != kText)
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::ForegroundRole && !row.live()) return QBrush(QColor(140, 140, 140));
    if (role == Qt::FontRole && index.column() == kText) return QFontDatabase::systemFont(QFontDatabase::FixedFont);
    return {};
}

QVariant TrackListModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case kTrack: return tr("Track");
        case kFrequency: return tr("Frequency\n(kHz from center)");
        case kSpeed: return tr("Speed\n(WPM)");
        case kLevel:
            return bin_hz_ > 0 ? tr("Level at birth (dB above\nthe floor, %1 Hz bin)").arg(bin_hz_, 0, 'f', 1)
                               : tr("Level at birth (dB above\nthe floor, one bin)");
        case kText: return tr("Decoded text");
        default: return {};
    }
}

}  // namespace kz4ap::app
