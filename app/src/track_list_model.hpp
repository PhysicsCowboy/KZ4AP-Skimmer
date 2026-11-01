// The track list's Qt face: TrackTable (core/track_table.hpp) as a table model.
#pragma once

#include "track_table.hpp"

#include <QAbstractTableModel>
#include <QFont>

#include <cstdint>
#include <optional>
#include <vector>

namespace kz4ap::app {

class TrackListModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { kTrack, kFrequency, kSpeed, kLevel, kText, kColumnCount };

    // The newest characters shown per track; the column elides further on its left when it is too narrow. Heuristic.
    static constexpr std::size_t kTextSymbols = 400;

    explicit TrackListModel(QObject* parent = nullptr);

    TrackTable& table() { return table_; }
    // bin_hz: the width of one FFT bin, Hz, named in the level column's header.
    void set_bin_width_hz(double bin_hz);
    // The radio's tuned frequency, Hz, when known: the frequency column then reads absolute.
    void set_center_hz(std::optional<double> center_hz);
    // Tells views what changed since the last call.
    void refresh();
    void clear();
    std::optional<std::uint32_t> track_at(int row) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

signals:
    // The rows were laid out anew: a selection must be restored by track.
    void layoutRedone();

private:
    TrackTable table_;
    std::uint64_t seen_version_ = 0;
    std::vector<std::uint64_t> seen_revisions_;  // by row: what the views last showed
    QFont fixed_font_;
    double bin_hz_ = 0;
    std::optional<double> center_hz_;
};

}  // namespace kz4ap::app
