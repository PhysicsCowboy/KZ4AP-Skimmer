#include "call_list_model.hpp"

#include <QFontDatabase>

#include <algorithm>
#include <utility>

namespace kz4ap::app {

CallListModel::CallListModel(QObject* parent) : QAbstractTableModel(parent), search_(std::make_unique<callsign::CallSearch>()) {}

void CallListModel::start(std::shared_ptr<const callsign::KnownCalls> known, std::optional<double> center_hz) {
    beginResetModel();
    search_ = std::make_unique<callsign::CallSearch>(callsign::CallSearchConfig{}, std::move(known));
    seen_version_ = search_->version();
    center_hz_ = center_hz;
    now_s_ = 0;
    endResetModel();
    emit headerDataChanged(Qt::Horizontal, kFrequency, kFrequency);
}

void CallListModel::refresh(double now_s) {
    now_s_ = now_s;
    if (search_->version() != seen_version_) {
        beginResetModel();
        seen_version_ = search_->version();
        endResetModel();
    } else if (rowCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, kColumnCount - 1));
    }
}

int CallListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(search_->calls().size()); }

int CallListModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : kColumnCount; }

QVariant CallListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const callsign::CallRow& row = search_->calls()[static_cast<std::size_t>(index.row())];
    if (role == Qt::UserRole) {  // what to sort by
        switch (index.column()) {
            case kCall: return QString::fromStdString(row.call);
            case kFrequency: return row.freq_hz;
            case kLastHeard: return -row.last_s;
            case kCount: return row.count;
            default: return row.framed ? 0 : (row.heard ? 1 : 2);
        }
    }
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case kCall: return QString::fromStdString(row.call);
            case kFrequency:
                return center_hz_ ? QString::asprintf("%.2f", (*center_hz_ + row.freq_hz) / 1000.0) : QString::asprintf("%+.2f", row.freq_hz / 1000.0);
            case kLastHeard: {
                const int ago = static_cast<int>(std::max(0.0, now_s_ - row.last_s));
                return ago < 60 ? tr("%1 s").arg(ago) : tr("%1 min").arg(ago / 60);
            }
            case kCount: return row.count;
            case kHow:
                if (row.framed) return tr("signed");
                return row.heard ? tr("bare") : tr("named");
            default: return {};
        }
    }
    if (role == Qt::ToolTipRole && index.column() == kHow)
        return tr("signed: sent with DE or CQ, or before K or TEST. bare: the call alone. named: only by another station (before DE or 5NN), so "
                  "the frequency is that station's.");
    if (role == Qt::TextAlignmentRole && index.column() != kCall && index.column() != kHow)
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::FontRole && index.column() == kCall) return QFontDatabase::systemFont(QFontDatabase::FixedFont);
    return {};
}

QVariant CallListModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case kCall: return tr("Call");
        case kFrequency: return center_hz_ ? tr("Frequency\n(kHz)") : tr("Frequency\n(kHz from center)");
        case kLastHeard: return tr("Last heard\n(ago)");
        case kCount: return tr("Count\n(readings)");
        case kHow: return tr("How");
        default: return {};
    }
}

}  // namespace kz4ap::app
