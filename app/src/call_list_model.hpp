// The call list's Qt face: the callsign stage (kz4ap/callsign.hpp) as a table model. It holds the stage: the main window feeds it what it
// feeds the track list. In decoder-only mode it holds none: no stage runs, and the list is empty.
#pragma once

#include "kz4ap/callsign.hpp"
#include "kz4ap/stream_runner.hpp"  // ahead of Qt, in every file AUTOMOC puts this one in first: Qt's macro emit breaks classical_decoder.hpp

#include <QAbstractTableModel>
#include <QFont>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace kz4ap::app {

class CallListModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { kCall, kFrequency, kLastHeard, kCount, kHow, kColumnCount };

    explicit CallListModel(QObject* parent = nullptr);

    // Starts over, for a new stream or on leaving decoder-only mode: a new stage, which reads the tracks born from now on. known: the
    // known-call list, if the operator gave one.
    void start(std::shared_ptr<const callsign::KnownCalls> known, std::optional<double> center_hz);
    // Decoder-only mode: the stage goes, and with it the list.
    void stop();
    // The stage, or null in decoder-only mode.
    callsign::CallSearch* search() { return search_.get(); }
    // A new known-call list, or none, for the stage running (CallSearch::set_known); the next start takes it too.
    void set_known(std::shared_ptr<const callsign::KnownCalls> known);
    // Every call (the default, as CW Skimmer lists them), or only the signed ones (callsign::is_signed).
    void set_signed_only(bool on);
    bool signed_only() const { return signed_only_; }
    std::optional<double> center_hz() const { return center_hz_; }
    // Tells views what changed since the last call; now_s, the stream's clock, s, for the time since a call was last heard.
    void refresh(double now_s);
    // The rows shown, in the stage's order, and how many the stage has reported (shown or not).
    std::vector<callsign::CallRow> shown_rows() const;
    std::size_t shown_count() const { return shown_.size(); }
    // The frequency of the call on this row of the model, Hz from the span's center (CallRow::freq_hz); none for no such row.
    std::optional<double> freq_at(int row) const;
    std::size_t reported_count() const { return search_ ? search_->calls().size() : 0; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    // The stage's rows that the filter shows, by index.
    std::vector<std::size_t> pick() const;
    void reset_rows();

    std::unique_ptr<callsign::CallSearch> search_;
    std::shared_ptr<const callsign::KnownCalls> known_;
    std::vector<std::size_t> shown_;  // indices into search_->calls() of the rows shown, in order
    bool signed_only_ = false;
    std::uint64_t seen_version_ = 0;
    std::optional<double> center_hz_;
    double now_s_ = 0;
    double shown_second_ = -1;  // s of stream: the whole second the list last showed
    QFont fixed_font_;
};

}  // namespace kz4ap::app
