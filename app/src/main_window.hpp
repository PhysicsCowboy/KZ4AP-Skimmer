// The main window: the transport, the radio's controls, the waterfall, the one-signal pane, the track and call lists and the status
// bar, around one stream at a time. start_stream takes any IqSource: a recording, or the live radio.
#pragma once

#include "cpu_meter.hpp"
#include "delivery_watch.hpp"
#include "display_feed.hpp"
#include "call_list_model.hpp"
#include "known_call_downloader.hpp"
#include "settings_store.hpp"
#include "signal_follow.hpp"
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
class QLineEdit;
class QPlainTextEdit;
class QSlider;
class QSpinBox;
class QSplitter;
class QTableView;
class QTimer;

namespace kz4ap::app {

// For runs without an operator (--screenshot, --report, --quit-at-end, --transport): what to do at which point of the
// stream. The transport steps press the same controls an operator does, and the report logs where the stream stood.
struct TransportStep {
    double at_wall_s = 0;  // s of wall clock since the first stream started
    // pause, play, restart, quit, speed=X (X as the speed box offers it: 1, 2, 5, 10 or max), seek=S (s into the recording),
    // radio (starts it at the controls' settings), radiostop, serial=SERIAL (the radio's serial number), center=KHZ, rate=HZ,
    // gain=DB,STATE, record=PATH, stoprecord, shot=PATH (a picture of the window now), mode=decoder or mode=skimmer, list=all or
    // list=signed, known=PATH (a known-call list), noknown, download (presses Download MASTER.SCP), follow=KHZ (the one-signal pane
    // follows the track nearest that frequency, as a click on a call there does: kHz as the track list shows it)
    QString action;
};

struct Automation {
    std::vector<TransportStep> transport;  // in order of at_wall_s
    QString screenshot_path;
    double screenshot_at_s = 0;  // stream time, s
    QString report_path;
    QString decoded_path;  // the tracks' text, as kz4ap-bench --score-decoded reads it
    QString calls_path;    // the call list, as training/kz4ap_proto/calls_score.py reads it
    bool quit_at_end = false;
    std::optional<double> quit_at_s;  // stream time, s
    // A recording's center frequency, Hz, overriding its header's and its file name's (--center-khz with a recording).
    std::optional<double> recording_center_hz;
    // A recording's spectrum is mirrored (--mirror; RecordingOptions::mirror).
    bool mirror = false;
    bool active() const {
        return !screenshot_path.isEmpty() || !report_path.isEmpty() || !decoded_path.isEmpty() || !calls_path.isEmpty() || quit_at_end || quit_at_s ||
               !transport.empty();
    }
};

// The run's start: the choices the controls take first, where they came from, and where they are kept (README, "Settings kept
// between runs"; core/app_settings.hpp). Decoder-only mode (decoder_only) is the plain CW decoder: no call stage, call list or
// known-call list (the owner's ruling, 2026-11-18). The list shows only the calls that signed with signed_only (callsign::is_signed).
// The radio's serial number (SdrplaySettings::serial): none opens none.
struct Startup {
    SettingsStore store;  // disabled: nothing read, nothing written
    UserChoices saved;    // what the store held at the start
    GivenChoices given;   // what the command line gave
    UserChoices start;    // the run's first choices: start_choices(saved, given)
    // The known-call list --known-calls read (main.cpp), or null: then the window reads start.known's from disk itself.
    std::shared_ptr<const callsign::KnownCalls> given_known;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // decoder_name: front_end_from_name's names; the caller has checked it. threads: EngineConfig::worker_threads. The speed (stream
    // seconds per second of wall clock, kAsFastAsPossible for no pacing), the radio's controls and the call stage's mode, filter and
    // known-call list come from startup.start.
    MainWindow(std::string decoder_name, int threads, Automation automation, Startup startup);
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
    void build_call_controls();
    void build_status_bar();
    // Decoder-only mode on or off: the stage goes or comes (a new one, reading the tracks born from then on), with its controls and list.
    void set_decoder_only(bool on);
    void set_signed_only(bool on);
    // A known-call list from a file, or the reason it was refused (also in the controls' status). False if refused, as in decoder-only mode.
    bool load_known_calls(const QString& path, const QString& how);
    // kind: where the list came from, for the settings (KnownKind::None with a null list).
    void set_known_calls(std::shared_ptr<const callsign::KnownCalls> known, const QString& path, const QString& status,
                         KnownKind kind = KnownKind::File);
    // The saved known-call list, read from disk (never the network); one that is gone or refused gives none, said in the status.
    void restore_known_calls_from(const KnownChoice& choice);
    // The window's layout and the choices as they are now; written when the program quits (save_settings), if the store is enabled.
    void restore_layout();
    UserChoices choices_now() const;
    void save_settings();
    // The button: asks supercheckpartial.com for its list of files, then for MASTER.SCP (KnownCallDownloader). Refused in decoder-only mode.
    void download_known_calls();
    void show_known_status(const QString& text, bool problem);
    void report_failure(const QString& message);
    void update_radio_controls();
    void update_radio_status(const StreamStatus& status);
    void update_position(const StreamStatus& status);
    void toggle_recording();
    void begin_recording(const QString& path);
    void write_decoded();
    void write_calls();
    void on_tick();
    void update_markers();
    // The calls written on the waterfall: the list's calls heard at their own frequency lately, the latest first (none in
    // decoder-only mode). now_s: the stream's clock, s.
    void update_call_labels(double now_s);
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
    // The pane's signal looked up again, then the track list told what changed.
    void refresh_track_views();
    void selection_changed();
    void restore_selection();
    // The one-signal pane: follow the row holding this track (a click on the waterfall, the track list or the call list), and show it.
    void follow_track(std::uint32_t id);
    // The listed row nearest to this frequency, Hz from the span's center, within TrackTable::kSamePlaceHz (a call's or a script's);
    // what = what is followed, for the status bar's message when none is that near.
    void follow_frequency(double freq_hz, const QString& what);
    QWidget* build_pane();
    void update_pane();

    std::string decoder_name_;
    double speed_;
    int threads_;
    Automation automation_;
    Startup startup_;
    bool settings_saved_ = false;
    // A saved known-call list not read yet: the run started in decoder-only mode, which has none; it is read on going to skimmer mode.
    std::optional<KnownChoice> pending_known_;
    // A saved known-call list that was gone or refused at the start: saved again as it was until the user picks another (or none).
    std::optional<KnownChoice> missing_known_;
    KnownKind known_kind_ = KnownKind::None;
    QString recording_folder_;  // where the Open and Record dialogs start; empty for their defaults
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
    std::optional<std::int64_t> stream_start_unix_ms_;  // the stream's first sample, ms since 1970 UTC, when known
    std::optional<double> stream_center_hz_;            // the stream's center frequency, Hz, when known
    QString center_origin_;  // where a recording's center frequency came from, for the report
    QString input_note_;     // a recording's file rate, resampling and mirroring, for the report
    bool stream_ended_ = false;
    bool radio_on_ = false;
    RecordingStatus last_recording_;
    DeliveryWatch delivery_;
    QString recording_problem_;  // why the last recording did not start
    QString gain_problem_;       // why the last gain was refused
    std::uint64_t ticks_ = 0;
    std::uint64_t rows_dropped_ = 0;
    std::size_t most_live_ = 0;  // the most live tracks so far
    // The one-signal pane: the signal it follows (core/signal_follow.hpp), its header and its text, and the text it last showed.
    SignalFollow follow_;
    QLabel* pane_label_ = nullptr;
    QPlainTextEdit* pane_text_ = nullptr;
    QString pane_shown_;

    WaterfallWidget* waterfall_ = nullptr;
    QSplitter* main_splitter_ = nullptr;   // the waterfall above, the lists below
    QSplitter* lists_splitter_ = nullptr;  // the track list, the call list
    TrackListModel* model_ = nullptr;
    CallListModel* calls_ = nullptr;
    QTableView* calls_view_ = nullptr;
    bool decoder_only_ = false;
    std::shared_ptr<const callsign::KnownCalls> known_calls_;
    QString known_path_;
    QString known_status_;  // what the controls say of the list: its size and origin, or why the last try failed
    double call_stage_cpu_s_ = 0.0;  // s: the GUI thread's CPU inside the call stage and its list's model, this stream
    KnownCallDownloader* downloader_ = nullptr;  // made on the first download, never before
    QComboBox* mode_box_ = nullptr;
    QComboBox* list_box_ = nullptr;
    QLabel* known_label_ = nullptr;
    QAction* known_download_ = nullptr;
    std::vector<QAction*> skimmer_controls_;  // the toolbar's call controls, hidden in decoder-only mode
    QTableView* table_view_ = nullptr;
    QTimer* timer_ = nullptr;
    QAction* play_pause_ = nullptr;
    QAction* restart_ = nullptr;
    QComboBox* speed_box_ = nullptr;
    QSlider* position_slider_ = nullptr;
    QLineEdit* serial_box_ = nullptr;
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
    QLabel* calls_label_ = nullptr;
    QLabel* cpu_label_ = nullptr;
};

}  // namespace kz4ap::app
