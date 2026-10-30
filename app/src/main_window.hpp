// The main window: the transport, the waterfall, the track list and the status bar, around one stream at a time.
// start_stream takes any IqSource; a recording is one way to get one, and the live radio will be another.
#pragma once

#include "cpu_meter.hpp"
#include "display_feed.hpp"
#include "track_list_model.hpp"
#include "waterfall_widget.hpp"

#include "kz4ap/iq_source.hpp"
#include "kz4ap/stream_runner.hpp"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QTableView;
class QTimer;

namespace kz4ap::app {

// For runs without an operator (--screenshot, --report, --quit-at-end, --transport): what to do at which point of the
// stream. The transport steps press the same controls an operator does, and the report logs where the stream stood.
struct TransportStep {
    double at_wall_s = 0;  // s of wall clock since the first stream started
    QString action;        // pause, play, restart, quit or speed=X (X as the speed box offers it: 1, 2, 5, 10 or max)
};

struct Automation {
    std::vector<TransportStep> transport;  // in order of at_wall_s
    QString screenshot_path;
    double screenshot_at_s = 0;  // stream time, s
    QString report_path;
    bool quit_at_end = false;
    std::optional<double> quit_at_s;  // stream time, s
    bool active() const {
        return !screenshot_path.isEmpty() || !report_path.isEmpty() || quit_at_end || quit_at_s || !transport.empty();
    }
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // decoder_name: front_end_from_name's names; the caller has checked it. speed: stream seconds per second of wall
    // clock for the next recording, kAsFastAsPossible for no pacing.
    MainWindow(std::string decoder_name, double speed, Automation automation);
    ~MainWindow() override;

    // Opens the recording and starts it. False (having told the operator) if it cannot be opened.
    bool open_recording(const QString& path);
    // Runs the pipeline over the source, replacing any stream running.
    void start_stream(std::unique_ptr<IqSource> source);

private:
    void build_transport();
    void build_scale_controls();
    void build_status_bar();
    void on_tick();
    void update_markers();
    void update_status(const StreamStatus& status);
    void update_transport();
    void set_speed_from_combo();
    void stop_stream();
    void write_report(const StreamStatus& status);
    void run_automation(const StreamStatus& status);
    void run_transport_steps(const StreamStatus& status);
    void selection_changed();
    void restore_selection();

    std::string decoder_name_;
    double speed_;
    Automation automation_;
    bool screenshot_done_ = false;
    bool report_done_ = false;
    QElapsedTimer automation_clock_;
    std::size_t next_transport_step_ = 0;
    QStringList transport_log_;

    // Destroyed in reverse: the runner, whose thread feeds the feed, goes first.
    std::unique_ptr<DisplayFeed> feed_;
    std::unique_ptr<StreamRunner> runner_;
    CpuMeter meter_;
    QString current_path_;
    std::string source_name_;
    std::optional<double> total_s_;
    bool stream_ended_ = false;
    std::uint64_t ticks_ = 0;
    std::uint64_t rows_dropped_ = 0;
    std::optional<std::uint32_t> selected_track_;

    WaterfallWidget* waterfall_ = nullptr;
    TrackListModel* model_ = nullptr;
    QTableView* table_view_ = nullptr;
    QTimer* timer_ = nullptr;
    QAction* play_pause_ = nullptr;
    QAction* restart_ = nullptr;
    QComboBox* speed_box_ = nullptr;
    QCheckBox* auto_floor_ = nullptr;
    QSlider* floor_slider_ = nullptr;
    QSlider* range_slider_ = nullptr;
    QLabel* floor_label_ = nullptr;
    QLabel* range_label_ = nullptr;
    QLabel* stream_label_ = nullptr;
    QLabel* speed_label_ = nullptr;
    QLabel* tracks_label_ = nullptr;
    QLabel* cpu_label_ = nullptr;
};

}  // namespace kz4ap::app
