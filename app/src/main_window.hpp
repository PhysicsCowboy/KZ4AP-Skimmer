// The main window: the transport, the radio's controls, the waterfall, the track list and the status bar, around one stream
// at a time. start_stream takes any IqSource: a recording, or the live radio.
#pragma once

#include "cpu_meter.hpp"
#include "delivery_watch.hpp"
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
class QDoubleSpinBox;
class QLabel;
class QSlider;
class QSpinBox;
class QTableView;
class QTimer;

namespace kz4ap::app {

// For runs without an operator (--screenshot, --report, --quit-at-end, --transport): what to do at which point of the
// stream. The transport steps press the same controls an operator does, and the report logs where the stream stood.
struct TransportStep {
    double at_wall_s = 0;  // s of wall clock since the first stream started
    // pause, play, restart, quit, speed=X (X as the speed box offers it: 1, 2, 5, 10 or max), seek=S (s into the recording),
    // radio (starts it at the controls' settings), radiostop, center=KHZ, rate=HZ, gain=DB,STATE, record=PATH, stoprecord,
    // shot=PATH (a picture of the window now)
    QString action;
};

struct Automation {
    std::vector<TransportStep> transport;  // in order of at_wall_s
    QString screenshot_path;
    double screenshot_at_s = 0;  // stream time, s
    QString report_path;
    QString decoded_path;  // the tracks' text, as kz4ap-bench --score-decoded reads it
    bool quit_at_end = false;
    std::optional<double> quit_at_s;  // stream time, s
    bool active() const {
        return !screenshot_path.isEmpty() || !report_path.isEmpty() || !decoded_path.isEmpty() || quit_at_end || quit_at_s ||
               !transport.empty();
    }
};

// The radio's settings at the program's start, the controls' first values.
struct RadioDefaults {
    double center_khz = 7031.5;
    int sample_rate = 192000;
    int if_gain_reduction_db = 40;
    int lna_state = 0;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // decoder_name: front_end_from_name's names; the caller has checked it. speed: stream seconds per second of wall
    // clock for the next recording, kAsFastAsPossible for no pacing. threads: EngineConfig::worker_threads.
    MainWindow(std::string decoder_name, double speed, int threads, RadioDefaults radio, Automation automation);
    ~MainWindow() override;

    // Opens the recording and starts it, start_s into it. False (having told the operator) if it cannot be opened.
    bool open_recording(const QString& path, double start_s = 0.0);
    // Starts the radio at the controls' settings. False (having told the operator) if it cannot start.
    bool start_radio();
    // Runs the pipeline over the source, replacing any stream running.
    void start_stream(std::unique_ptr<IqSource> source);

private:
    void build_transport();
    void build_scale_controls();
    void build_radio_controls();
    void build_status_bar();
    void report_failure(const QString& message);
    void update_radio_controls();
    void update_radio_status(const StreamStatus& status);
    void update_position(const StreamStatus& status);
    void toggle_recording();
    void begin_recording(const QString& path);
    void write_decoded();
    void on_tick();
    void update_markers();
    void update_status(const StreamStatus& status);
    void update_transport();
    void set_speed_from_combo();
    void stop_stream();
    // The operator asked for the radio to stop.
    void radio_stopped();
    // A message that stays in the radio's status until the next try at the same thing (message: empty when that went well).
    void set_recording_problem(const QString& message);
    // Whether what is about to end a recording may do so; true at once when none is running. Asks the operator.
    bool may_end_recording(const QString& what);
    void closeEvent(QCloseEvent* event) override;
    void write_report(const StreamStatus& status);
    void run_automation(const StreamStatus& status);
    void run_transport_steps(const StreamStatus& status);
    void selection_changed();
    void restore_selection();

    std::string decoder_name_;
    double speed_;
    int threads_;
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
    std::optional<double> total_s_;  // the recording's whole length, s; none for a live source
    double origin_s_ = 0.0;          // where this stream began in the recording, s
    bool stream_ended_ = false;
    bool radio_on_ = false;
    RecordingStatus last_recording_;
    DeliveryWatch delivery_;
    QString recording_problem_;  // why the last recording did not start
    QString gain_problem_;       // why the last gain was refused
    std::uint64_t ticks_ = 0;
    std::uint64_t rows_dropped_ = 0;
    std::size_t most_live_ = 0;  // the most live tracks so far
    std::optional<std::uint32_t> selected_track_;

    WaterfallWidget* waterfall_ = nullptr;
    TrackListModel* model_ = nullptr;
    QTableView* table_view_ = nullptr;
    QTimer* timer_ = nullptr;
    QAction* play_pause_ = nullptr;
    QAction* restart_ = nullptr;
    QComboBox* speed_box_ = nullptr;
    QSlider* position_slider_ = nullptr;
    QDoubleSpinBox* center_box_ = nullptr;
    QComboBox* rate_box_ = nullptr;
    QSpinBox* if_gain_box_ = nullptr;
    QSpinBox* lna_box_ = nullptr;
    QAction* radio_action_ = nullptr;
    QAction* record_action_ = nullptr;
    QLabel* radio_label_ = nullptr;
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
