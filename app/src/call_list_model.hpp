// The call list's Qt face: the callsign stage (kz4ap/callsign.hpp) as a table model. It holds the stage: the main window feeds it what it
// feeds the track list.
#pragma once

#include "kz4ap/callsign.hpp"
#include "kz4ap/stream_runner.hpp"  // ahead of Qt, in every file AUTOMOC puts this one in first: Qt's macro emit breaks classical_decoder.hpp

#include <QAbstractTableModel>
#include <QFont>

#include <cstdint>
#include <memory>
#include <optional>

namespace kz4ap::app {

class CallListModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { kCall, kFrequency, kLastHeard, kCount, kHow, kColumnCount };

    explicit CallListModel(QObject* parent = nullptr);

    // Starts over, for a new stream. known: the known-call list, if the operator gave one.
    void start(std::shared_ptr<const callsign::KnownCalls> known, std::optional<double> center_hz);
    callsign::CallSearch& search() { return *search_; }
    std::optional<double> center_hz() const { return center_hz_; }
    // Tells views what changed since the last call; now_s, the stream's clock, s, for the time since a call was last heard.
    void refresh(double now_s);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    std::unique_ptr<callsign::CallSearch> search_;
    std::uint64_t seen_version_ = 0;
    std::optional<double> center_hz_;
    double now_s_ = 0;
    double shown_second_ = -1;  // s of stream: the whole second the list last showed
    QFont fixed_font_;
};

}  // namespace kz4ap::app
