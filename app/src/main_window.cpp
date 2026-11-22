#include "main_window.hpp"

#include "decoded_file.hpp"
#include "kz4ap/cpu_time.hpp"
#include "kz4ap/decoder_name.hpp"
#include "kz4ap/engine.hpp"
#include "kz4ap/file_iq_source.hpp"
#include "kz4ap/sdrplay_source.hpp"
#include "kz4ap/utc_time.hpp"
#include "recording_name.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTableView>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <utility>
#include <variant>
#include <vector>

namespace kz4ap::app {

namespace {

// The GUI's clock, ms: 12.5 pictures/s, each about 2 new rows at 23.4 rows/s (heuristic: a waterfall scrolls 1 pixel per 43 ms, no faster to the eye). The list changes
// at a third of that, 4.2 times/s: the decoder publishes every 0.25 s. The status bar about once a second. Each is what the GUI thread's CPU goes with.
constexpr int kTickMs = 80;
constexpr int kTextEveryTicks = 3;
constexpr int kStatusEveryTicks = 12;
// The position slider's steps across a recording.
constexpr int kPositionSteps = 1000;
// The calls written on the waterfall: those heard within kCallLabelAgeS of stream, s, and brighter within kCallLabelRecentS, about the
// picture's height at its first size (380 rows at 23.4 rows/s). Heuristics: a station that calls every half minute keeps its label.
constexpr double kCallLabelAgeS = 60.0;
constexpr double kCallLabelRecentS = 16.0;

}  // namespace

MainWindow::MainWindow(std::string decoder_name, int threads, Automation automation, Startup startup)
    : decoder_name_(std::move(decoder_name)),
      speed_(startup.start.speed),
      threads_(threads),
      automation_(std::move(automation)),
      startup_(std::move(startup)),
      recording_folder_(QString::fromStdString(startup_.start.recording_folder)),
      decoder_only_(startup_.start.decoder_only) {
    const UserChoices& start = startup_.start;
    setWindowTitle(tr("KZ4AP Skimmer"));
    resize(1360, 900);

    waterfall_ = new WaterfallWidget;
    model_ = new TrackListModel(this);
    table_view_ = new QTableView;
    table_view_->setModel(model_);
    table_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_view_->setTextElideMode(Qt::ElideLeft);  // the newest text is on the right: keep it
    table_view_->setWordWrap(false);
    table_view_->verticalHeader()->hide();
    table_view_->verticalHeader()->setDefaultSectionSize(22);
    QHeaderView* header = table_view_->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Fixed);
    header->setSectionResizeMode(TrackListModel::kText, QHeaderView::Stretch);
    table_view_->setColumnWidth(TrackListModel::kTrack, 50);
    table_view_->setColumnWidth(TrackListModel::kFrequency, 115);
    table_view_->setColumnWidth(TrackListModel::kSpeed, 60);
    table_view_->setColumnWidth(TrackListModel::kLevel, 170);
    connect(table_view_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &MainWindow::selection_changed);
    connect(model_, &TrackListModel::layoutRedone, this, &MainWindow::restore_selection);

    // The call list, beside the track list: a click on a heading sorts it; the latest heard first to begin with.
    calls_ = new CallListModel(this);
    auto* sorted_calls = new QSortFilterProxyModel(this);
    sorted_calls->setSourceModel(calls_);
    sorted_calls->setSortRole(Qt::UserRole);
    sorted_calls->setDynamicSortFilter(true);
    calls_view_ = new QTableView;
    calls_view_->setModel(sorted_calls);
    calls_view_->setSortingEnabled(true);
    calls_view_->sortByColumn(CallListModel::kLastHeard, Qt::AscendingOrder);
    calls_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    calls_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    calls_view_->setWordWrap(false);
    calls_view_->verticalHeader()->hide();
    calls_view_->verticalHeader()->setDefaultSectionSize(22);
    calls_view_->setColumnWidth(CallListModel::kCall, 100);
    calls_view_->setColumnWidth(CallListModel::kFrequency, 115);
    calls_view_->setColumnWidth(CallListModel::kLastHeard, 75);
    calls_view_->setColumnWidth(CallListModel::kCount, 65);
    calls_view_->setColumnWidth(CallListModel::kHow, 60);
    lists_splitter_ = new QSplitter(Qt::Horizontal);
    lists_splitter_->addWidget(table_view_);
    lists_splitter_->addWidget(calls_view_);
    lists_splitter_->setStretchFactor(0, 3);
    lists_splitter_->setStretchFactor(1, 1);
    lists_splitter_->setSizes({920, 440});  // pixels

    // A click on a call follows the track nearest its frequency in the one-signal pane.
    connect(calls_view_, &QAbstractItemView::clicked, this, [this, sorted_calls](const QModelIndex& index) {
        // By the call shown on that row, looked up now: the stage's rows may have moved since the list was laid out.
        const std::optional<std::string> base = calls_->base_at(sorted_calls->mapToSource(index).row());
        if (!base) return;
        if (const std::optional<double> freq_hz = calls_->freq_of(*base)) {
            follow_frequency(*freq_hz, QString::fromStdString(*base));
        } else {
            statusBar()->showMessage(tr("%1 has just left the call list").arg(QString::fromStdString(*base)), 4000);  // ms
        }
    });
    connect(waterfall_, &WaterfallWidget::trackClicked, this, [this](std::uint32_t id) { follow_track(id); });

    // The waterfall, the one-signal pane under it, and the lists.
    main_splitter_ = new QSplitter(Qt::Vertical);
    main_splitter_->addWidget(waterfall_);
    main_splitter_->addWidget(build_pane());
    main_splitter_->addWidget(lists_splitter_);
    main_splitter_->setStretchFactor(0, 3);
    main_splitter_->setStretchFactor(1, 0);
    main_splitter_->setStretchFactor(2, 1);
    main_splitter_->setSizes({520, 110, 260});  // pixels; the pane has four lines of text, the list room for about nine tracks
    setCentralWidget(main_splitter_);

    build_transport();
    build_scale_controls();
    build_radio_controls();
    serial_box_->setText(QString::fromStdString(start.radio_serial));
    center_box_->setValue(start.center_khz);
    rate_box_->setCurrentIndex(std::max(0, rate_box_->findData(start.sample_rate_hz)));
    if_gain_box_->setValue(start.if_gain_reduction_db);
    lna_box_->setValue(start.lna_state);
    // The color scale: the range, then the floor by hand if the automatic one was off.
    range_slider_->setValue(static_cast<int>(std::lround(start.range_db)));
    floor_slider_->setValue(static_cast<int>(std::lround(start.floor_dbfs)));
    auto_floor_->setChecked(start.auto_floor);
    if (!start.auto_floor) {
        waterfall_->set_floor_dbfs(start.floor_dbfs);
        floor_label_->setText(tr("Floor %1 dBFS").arg(start.floor_dbfs, 0, 'f', 0));
    }
    build_call_controls();
    build_status_bar();
    update_transport();
    update_radio_controls();
    // The call controls' first values. A list given on the command line has been read already (main.cpp); a saved one is read here,
    // from disk only, unless the run starts in decoder-only mode, which has none: then on going to skimmer mode.
    if (startup_.given_known) {
        const QString path = QString::fromStdString(start.known.path);
        set_known_calls(startup_.given_known, path, tr("%1 calls from %2").arg(startup_.given_known->size()).arg(QFileInfo(path).fileName()));
    } else if (start.known.kind != KnownKind::None && !decoder_only_) {
        restore_known_calls_from(start.known);
    } else {
        set_known_calls(nullptr, {}, {}, KnownKind::None);
        if (start.known.kind != KnownKind::None) pending_known_ = start.known;
    }
    list_box_->setCurrentIndex(start.signed_only ? 1 : 0);
    set_signed_only(start.signed_only);
    mode_box_->setCurrentIndex(decoder_only_ ? 1 : 0);
    set_decoder_only(decoder_only_);
    restore_layout();
    // On quit, however it comes: the window closed, or a script's quit.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { save_settings(); });

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::on_tick);
    timer_->start(kTickMs);
    automation_clock_.start();
}

MainWindow::~MainWindow() { stop_stream(); }

void MainWindow::build_transport() {
    auto* bar = addToolBar(tr("Transport"));
    bar->setMovable(false);
    QAction* open = bar->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Open..."));
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, [this] {
        const QString path =
            QFileDialog::getOpenFileName(this, tr("Open an I/Q recording"), recording_folder_, tr("I/Q recordings (*.wav);;All files (*)"));
        if (path.isEmpty()) return;
        recording_folder_ = QFileInfo(path).absolutePath();
        if (may_end_recording(tr("Opening a recording"))) open_recording(path);
    });
    play_pause_ = bar->addAction(tr("Pause"));
    play_pause_->setShortcut(Qt::Key_Space);
    connect(play_pause_, &QAction::triggered, this, [this] {
        if (!runner_) return;
        if (PlaybackControl* playback = runner_->source().playback()) playback->set_paused(!playback->paused());
        update_transport();
    });
    restart_ = bar->addAction(tr("Restart"));
    connect(restart_, &QAction::triggered, this, [this] {
        if (!current_path_.isEmpty()) open_recording(current_path_);
    });
    bar->addSeparator();
    bar->addWidget(new QLabel(tr(" Speed: ")));
    speed_box_ = new QComboBox;
    speed_box_->addItem(tr("1x (real time)"), 1.0);
    speed_box_->addItem(tr("2x"), 2.0);
    speed_box_->addItem(tr("5x"), 5.0);
    speed_box_->addItem(tr("10x"), 10.0);
    speed_box_->addItem(tr("Max"), kAsFastAsPossible);
    int index = speed_box_->findData(speed_);
    if (index < 0) {
        speed_box_->addItem(tr("%1x").arg(speed_), speed_);
        index = speed_box_->count() - 1;
    }
    speed_box_->setCurrentIndex(index);
    connect(speed_box_, &QComboBox::currentIndexChanged, this, [this] { set_speed_from_combo(); });
    bar->addWidget(speed_box_);
    bar->addSeparator();
    bar->addWidget(new QLabel(tr(" Position: ")));
    position_slider_ = new QSlider(Qt::Horizontal);
    position_slider_->setRange(0, kPositionSteps);
    position_slider_->setFixedWidth(180);
    position_slider_->setEnabled(false);
    position_slider_->setToolTip(tr("Drag to a place in the recording: the decoders start afresh from there"));
    // A seek is a new stream from there, with new decoders: tracks start again (engine: FileIqSource's start_s).
    connect(position_slider_, &QSlider::sliderReleased, this, [this] {
        if (!current_path_.isEmpty() && total_s_)
            open_recording(current_path_, *total_s_ * position_slider_->value() / kPositionSteps);
    });
    bar->addWidget(position_slider_);
    bar->addSeparator();
}

void MainWindow::build_radio_controls() {
    addToolBarBreak();
    auto* bar = addToolBar(tr("Radio"));
    bar->setMovable(false);
    bar->addWidget(new QLabel(tr(" Radio serial: ")));
    serial_box_ = new QLineEdit;
    serial_box_->setPlaceholderText(tr("required"));
    serial_box_->setFixedWidth(110);
    serial_box_->setToolTip(tr("The SDRplay radio's serial number: only the radio with exactly this one is opened. "
                               "Start radio with none lists the radios found and opens none; so does kz4ap-skimmer --list-sdrplay."));
    bar->addWidget(serial_box_);
    bar->addWidget(new QLabel(tr("  Center: ")));
    center_box_ = new QDoubleSpinBox;
    center_box_->setDecimals(3);
    center_box_->setRange(1.0, 2000000.0);
    center_box_->setSuffix(tr(" kHz"));
    center_box_->setKeyboardTracking(false);
    center_box_->setFixedWidth(130);
    bar->addWidget(center_box_);
    bar->addWidget(new QLabel(tr("  Rate: ")));
    rate_box_ = new QComboBox;
    for (const int rate : sdrplay_sample_rates()) rate_box_->addItem(tr("%1 kHz").arg(rate / 1000), rate);
    bar->addWidget(rate_box_);
    bar->addWidget(new QLabel(tr("  IF gain reduction: ")));
    if_gain_box_ = new QSpinBox;
    if_gain_box_->setRange(20, 59);
    if_gain_box_->setSuffix(tr(" dB"));
    if_gain_box_->setKeyboardTracking(false);
    if_gain_box_->setToolTip(tr("More reduction, less gain. AGC is off, so a level stays a level."));
    bar->addWidget(if_gain_box_);
    bar->addWidget(new QLabel(tr("  LNA state: ")));
    lna_box_ = new QSpinBox;
    lna_box_->setRange(0, 9);
    lna_box_->setKeyboardTracking(false);
    lna_box_->setToolTip(tr("0 is the most gain; each model has a table of reductions by band"));
    bar->addWidget(lna_box_);
    bar->addSeparator();
    radio_action_ = bar->addAction(tr("Start radio"));
    connect(radio_action_, &QAction::triggered, this, [this] {
        if (radio_on_) {
            radio_stopped();
        } else {
            start_radio();
        }
    });
    record_action_ = bar->addAction(tr("Record"));
    record_action_->setToolTip(tr("Save the radio's raw I/Q to a WAV recording, which this program can then play"));
    connect(record_action_, &QAction::triggered, this, [this] { toggle_recording(); });
    bar->addSeparator();
    radio_label_ = new QLabel;
    bar->addWidget(radio_label_);

    // A new center or rate is a new stream (the decoders' stations are gone anyway); the gain changes as it runs.
    const auto restart_radio = [this] {
        if (radio_on_) start_radio();
    };
    connect(center_box_, &QDoubleSpinBox::valueChanged, this, restart_radio);
    connect(rate_box_, &QComboBox::currentIndexChanged, this, restart_radio);
    const auto change_gain = [this] {
        if (!radio_on_ || !runner_ || runner_->source().radio() == nullptr) return;
        try {
            runner_->source().radio()->set_gain(if_gain_box_->value(), lna_box_->value());
            gain_problem_.clear();
        } catch (const std::exception& e) {
            // The boxes go back to what the radio has, and the reason stays on the screen until a gain is taken.
            const RadioStatus radio = runner_->source().radio()->radio_status();
            const QSignalBlocker gain_blocker(if_gain_box_);
            const QSignalBlocker lna_blocker(lna_box_);
            if_gain_box_->setValue(radio.if_gain_reduction_db);
            lna_box_->setValue(radio.lna_state);
            gain_problem_ = tr("Gain not changed: %1").arg(QString::fromStdString(e.what()));
        }
        if (runner_) update_radio_status(runner_->status());
    };
    connect(if_gain_box_, &QSpinBox::valueChanged, this, change_gain);
    connect(lna_box_, &QSpinBox::valueChanged, this, change_gain);
}

void MainWindow::build_call_controls() {
    addToolBarBreak();
    auto* bar = addToolBar(tr("Calls"));
    bar->setMovable(false);
    bar->addWidget(new QLabel(tr(" Mode: ")));
    mode_box_ = new QComboBox;
    mode_box_->addItem(tr("Skimmer (calls)"));
    mode_box_->addItem(tr("Decoder only"));
    mode_box_->setToolTip(tr("Decoder only: the waterfall and the decoded text, with no call search, call list or known-call list, "
                             "so the search costs nothing. Back to Skimmer, calls are searched in the tracks that start from then on."));
    connect(mode_box_, &QComboBox::currentIndexChanged, this, [this](int index) { set_decoder_only(index == 1); });
    bar->addWidget(mode_box_);
    // The rest only in skimmer mode.
    skimmer_controls_.push_back(bar->addSeparator());
    skimmer_controls_.push_back(bar->addWidget(new QLabel(tr(" List: "))));
    list_box_ = new QComboBox;
    list_box_->addItem(tr("All calls"));
    list_box_->addItem(tr("Signed calls only"));
    list_box_->setToolTip(tr("All calls: every call found, signed, bare or named, as CW Skimmer lists them. Signed calls only: the calls "
                             "sent at least once after DE, CQ or TEST, or before K, KN or AR."));
    connect(list_box_, &QComboBox::currentIndexChanged, this, [this](int index) { set_signed_only(index == 1); });
    skimmer_controls_.push_back(bar->addWidget(list_box_));
    skimmer_controls_.push_back(bar->addSeparator());
    skimmer_controls_.push_back(bar->addWidget(new QLabel(tr(" Known calls: "))));
    QAction* open = bar->addAction(tr("Open list..."));
    open->setToolTip(tr("Your own known-call list in MASTER.SCP's format: one call a line. The call list works without one."));
    connect(open, &QAction::triggered, this, [this] {
        const QString start = known_path_.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : QFileInfo(known_path_).path();
        const QString path = QFileDialog::getOpenFileName(this, tr("Open a known-call list"), start,
                                                          tr("Known-call lists (*.scp *.txt);;All files (*)"));
        if (!path.isEmpty()) load_known_calls(path, tr("from"));
    });
    skimmer_controls_.push_back(open);
    known_download_ = bar->addAction(tr("Download MASTER.SCP"));
    known_download_->setToolTip(tr("Download Super Check Partial's MASTER.SCP from supercheckpartial.com and use it as the known-call list. "
                                   "The program reaches the network only when this is pressed. It sends the program's name and version, "
                                   "and no language, cookie or name of yours."));
    connect(known_download_, &QAction::triggered, this, [this] { download_known_calls(); });
    skimmer_controls_.push_back(known_download_);
    QAction* none = bar->addAction(tr("No list"));
    none->setToolTip(tr("Search calls without a known-call list"));
    connect(none, &QAction::triggered, this, [this] { set_known_calls(nullptr, {}, {}, KnownKind::None); });
    skimmer_controls_.push_back(none);
    // Last on the row, so that a long status or error never pushes the buttons out of sight; the whole of it is in the tooltip.
    known_label_ = new QLabel;
    known_label_->setTextFormat(Qt::RichText);
    skimmer_controls_.push_back(bar->addWidget(known_label_));
}

void MainWindow::set_decoder_only(bool on) {
    decoder_only_ = on;
    for (QAction* action : skimmer_controls_) action->setVisible(!on);
    calls_view_->setVisible(!on);
    if (calls_label_) calls_label_->setVisible(!on);
    if (!on && pending_known_) {
        // The saved list of a run that started in decoder-only mode.
        const KnownChoice saved = *pending_known_;
        pending_known_.reset();
        restore_known_calls_from(saved);
    }
    if (on) {
        calls_->stop();
        waterfall_->set_call_labels({});
    } else if (calls_->search() == nullptr) {
        calls_->start(known_calls_, stream_center_hz_);
    }
    if (mode_box_->currentIndex() != (on ? 1 : 0)) {
        const QSignalBlocker blocker(mode_box_);
        mode_box_->setCurrentIndex(on ? 1 : 0);
    }
}

void MainWindow::set_signed_only(bool on) {
    calls_->set_signed_only(on);
    if (list_box_->currentIndex() != (on ? 1 : 0)) {
        const QSignalBlocker blocker(list_box_);
        list_box_->setCurrentIndex(on ? 1 : 0);
    }
}

void MainWindow::show_known_status(const QString& text, bool problem) {
    known_label_->setText(problem ? QStringLiteral("<b>%1</b>").arg(text.toHtmlEscaped()) : text.toHtmlEscaped());
    known_label_->setToolTip(known_path_.isEmpty() ? text : tr("%1\nIn use: %2").arg(text, QDir::toNativeSeparators(known_path_)));
    if (problem || !text.isEmpty()) transport_log_ << QStringLiteral("known calls: %1").arg(text);
}

void MainWindow::set_known_calls(std::shared_ptr<const callsign::KnownCalls> known, const QString& path, const QString& status, KnownKind kind) {
    known_calls_ = std::move(known);
    known_path_ = known_calls_ ? path : QString();
    known_kind_ = known_calls_ ? kind : KnownKind::None;
    pending_known_.reset();  // a choice made now replaces the saved one
    missing_known_.reset();
    calls_->set_known(known_calls_);
    known_status_ = known_calls_ ? status : tr("none");
    show_known_status(known_status_, false);
}

void MainWindow::restore_known_calls_from(const KnownChoice& choice) {
    const RestoredKnown r = restore_known_calls(choice, QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    if (r.list) {
        set_known_calls(r.list, r.path, r.status, choice.kind);
    } else {
        // No list, and the reason in the controls' status: never a dialog.
        known_calls_.reset();
        known_path_.clear();
        known_kind_ = KnownKind::None;
        pending_known_.reset();
        missing_known_ = choice;  // kept as saved until the user picks another list: the drive may be back next time
        calls_->set_known(nullptr);
        known_status_ = tr("none");
        show_known_status(r.status, r.problem);
    }
    std::fprintf(stderr, "kz4ap-skimmer: saved known-call list: %s\n", r.status.toLocal8Bit().constData());
}

void MainWindow::restore_layout() {
    const SettingsStore& store = startup_.store;
    if (!store.enabled()) return;
    if (const QByteArray g = store.read_layout(QStringLiteral("geometry")); !g.isEmpty()) restoreGeometry(g);
    if (const QByteArray s = store.read_layout(QStringLiteral("main_splitter")); !s.isEmpty()) {
        const QList<int> before = main_splitter_->sizes();
        // A state saved with another number of parts, or one hiding the waterfall, is not taken.
        if (!main_splitter_->restoreState(s) || main_splitter_->sizes().value(0) == 0) main_splitter_->setSizes(before);
    }
    if (const QByteArray s = store.read_layout(QStringLiteral("lists_splitter")); !s.isEmpty()) lists_splitter_->restoreState(s);
    if (const QByteArray s = store.read_layout(QStringLiteral("track_columns")); !s.isEmpty()) table_view_->horizontalHeader()->restoreState(s);
    if (const QByteArray s = store.read_layout(QStringLiteral("call_columns")); !s.isEmpty()) {
        QHeaderView* header = calls_view_->horizontalHeader();
        header->restoreState(s);
        calls_view_->sortByColumn(header->sortIndicatorSection(), header->sortIndicatorOrder());
    }
}

UserChoices MainWindow::choices_now() const {
    UserChoices c;
    c.decoder_only = decoder_only_;
    c.signed_only = calls_->signed_only();
    if (pending_known_) {
        c.known = *pending_known_;
    } else if (missing_known_) {
        c.known = *missing_known_;
    } else if (known_kind_ == KnownKind::File) {
        // Absolute, so that a run started elsewhere finds it; the command line's own form when unchanged (choices_to_save).
        c.known = {KnownKind::File, startup_.given.known && known_path_ == QString::fromStdString(startup_.given.known->path)
                                        ? startup_.given.known->path
                                        : QFileInfo(known_path_).absoluteFilePath().toStdString()};
    } else {
        c.known = {known_kind_, {}};
    }
    c.speed = speed_box_->currentData().toDouble();
    c.radio_serial = serial_box_->text().trimmed().toStdString();
    c.center_khz = std::round(center_box_->value() * 1000.0) / 1000.0;  // to the box's 1 Hz, as main.cpp rounds --center-khz
    c.sample_rate_hz = rate_box_->currentData().toInt();
    c.if_gain_reduction_db = if_gain_box_->value();
    c.lna_state = lna_box_->value();
    c.auto_floor = auto_floor_->isChecked();
    c.floor_dbfs = floor_slider_->value();
    c.range_db = range_slider_->value();
    c.recording_folder = recording_folder_.toStdString();
    return c;
}

void MainWindow::save_settings() {
    const SettingsStore& store = startup_.store;
    if (settings_saved_ || !store.enabled()) return;
    settings_saved_ = true;
    store.write_choices(choices_to_save(startup_.saved, startup_.start, choices_now(), startup_.given));
    store.write_layout(QStringLiteral("geometry"), saveGeometry());
    store.write_layout(QStringLiteral("main_splitter"), main_splitter_->saveState());
    store.write_layout(QStringLiteral("lists_splitter"), lists_splitter_->saveState());
    store.write_layout(QStringLiteral("track_columns"), table_view_->horizontalHeader()->saveState());
    store.write_layout(QStringLiteral("call_columns"), calls_view_->horizontalHeader()->saveState());
}

bool MainWindow::load_known_calls(const QString& path, const QString& how) {
    if (decoder_only_) {
        show_known_status(tr("no known-call list in decoder-only mode"), true);
        return false;
    }
    try {
        std::shared_ptr<const callsign::KnownCalls> list = read_known_calls(path);
        set_known_calls(list, path, tr("%1 calls %2 %3").arg(list->size()).arg(how, QFileInfo(path).fileName()));
        return true;
    } catch (const std::exception& e) {
        show_known_status(QString::fromStdString(e.what()), true);
        return false;
    }
}

void MainWindow::download_known_calls() {
    if (decoder_only_) {
        show_known_status(tr("no download in decoder-only mode"), true);
        return;
    }
    if (downloader_ == nullptr) {
        // Made on the first press: nothing reaches the network before.
        downloader_ = new KnownCallDownloader(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation), this);
        connect(downloader_, &KnownCallDownloader::progress, this, [this](const QString& text) { show_known_status(text, false); });
        connect(downloader_, &KnownCallDownloader::failed, this, [this](const QString& message) {
            known_download_->setEnabled(true);
            // The list in use, if any, stays.
            show_known_status(tr("Download failed: %1%2").arg(message, known_calls_ ? tr("; still using %1").arg(known_status_) : QString()), true);
        });
        connect(downloader_, &KnownCallDownloader::done, this,
                [this](std::shared_ptr<const callsign::KnownCalls> list, bool fresh, qint64 bytes) {
                    known_download_->setEnabled(true);
                    const QString path = downloader_->list_path();
                    const QString when = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd HH:mm 'UTC'"));
                    transport_log_ << QStringLiteral("download: %1, %2 bytes: %3")
                                          .arg(fresh ? QStringLiteral("HTTP 200, saved") : QStringLiteral("HTTP 304, the copy kept is current"))
                                          .arg(bytes)
                                          .arg(QDir::toNativeSeparators(path));
                    if (decoder_only_) return;  // the mode changed while it downloaded: kept, not used
                    set_known_calls(list, path,
                                    fresh ? tr("%1 calls, downloaded %2").arg(list->size()).arg(when)
                                          : tr("%1 calls from %2 (unchanged on the server, %3)").arg(list->size()).arg(QFileInfo(path).fileName(), when),
                                    KnownKind::Downloaded);
                });
    }
    if (downloader_->busy()) return;
    known_download_->setEnabled(false);
    downloader_->start();
}

bool MainWindow::start_radio() {
    stop_stream();
    radio_on_ = false;
    try {
        SdrplaySettings settings;
        settings.serial = serial_box_->text().trimmed().toStdString();
        settings.center_hz = center_box_->value() * 1000.0;
        settings.sample_rate = rate_box_->currentData().toInt();
        settings.if_gain_reduction_db = if_gain_box_->value();
        settings.lna_state = lna_box_->value();
        auto source = std::make_unique<SdrplaySource>(settings);
        current_path_.clear();
        origin_s_ = 0.0;
        radio_on_ = true;
        start_stream(std::move(source));
        return true;
    } catch (const std::exception& e) {
        update_radio_controls();
        update_transport();
        report_failure(QString::fromStdString(e.what()));
        return false;
    }
}

void MainWindow::update_radio_controls() {
    const bool recording = last_recording_.active;
    radio_action_->setText(radio_on_ ? tr("Stop radio") : tr("Start radio"));
    radio_action_->setEnabled(!recording);
    // Another radio is chosen with this one stopped.
    serial_box_->setEnabled(!radio_on_);
    // A new center or rate would break a recording in two.
    center_box_->setEnabled(!recording);
    rate_box_->setEnabled(!recording);
    record_action_->setEnabled(radio_on_ && runner_ && runner_->source().recorder() != nullptr);
    record_action_->setText(recording ? tr("Stop recording") : tr("Record"));
}

void MainWindow::toggle_recording() {
    if (!radio_on_ || !runner_ || runner_->source().recorder() == nullptr) return;
    RecordControl* recorder = runner_->source().recorder();
    if (recorder->recording_status().active) {
        recorder->stop_recording();
        last_recording_ = recorder->recording_status();
        update_radio_controls();
        return;
    }
    const std::string name = recording_file_name(center_box_->value() * 1000.0, rate_box_->currentData().toInt(),
                                                  QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")).toStdString());
    const QString folder = !recording_folder_.isEmpty() && QFileInfo(recording_folder_).isDir()
                               ? recording_folder_
                               : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested = folder + QLatin1Char('/') + QString::fromStdString(name);
    const QString path = QFileDialog::getSaveFileName(this, tr("Save the radio's I/Q"), suggested, tr("I/Q recordings (*.wav)"));
    if (path.isEmpty()) return;
    recording_folder_ = QFileInfo(path).absolutePath();
    begin_recording(path);
}

void MainWindow::begin_recording(const QString& path) {
    RecordControl* recorder = runner_ ? runner_->source().recorder() : nullptr;
    if (recorder == nullptr) return;
    try {
        recorder->start_recording(std::filesystem::path(path.toStdWString()));
        last_recording_ = recorder->recording_status();
        set_recording_problem({});
    } catch (const std::exception& e) {
        set_recording_problem(tr("The recording did not start: %1").arg(QString::fromStdString(e.what())));
    }
    update_radio_controls();
}

void MainWindow::set_recording_problem(const QString& message) {
    recording_problem_ = message;
    if (runner_) update_radio_status(runner_->status());
    if (message.isEmpty()) return;
    transport_log_ << message;
    if (!automation_.active()) QMessageBox::warning(this, tr("KZ4AP Skimmer"), message);
}

bool MainWindow::may_end_recording(const QString& what) {
    if (!last_recording_.active) return true;
    // A script answers yes: it stops the recording by asking to. The file is valid either way.
    if (automation_.active()) return true;
    const QString file = QString::fromStdWString(last_recording_.path.wstring());
    return QMessageBox::question(this, tr("KZ4AP Skimmer"),
                                 tr("%1 ends the recording to %2, which stays valid. Go on?").arg(what, file)) == QMessageBox::Yes;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (may_end_recording(tr("Closing the window"))) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::radio_stopped() {
    stop_stream();
    radio_on_ = false;
    // Nothing is live now: the markers and the status say so, and the text stays to be read.
    delivery_ = DeliveryWatch{};
    recording_problem_.clear();
    gain_problem_.clear();
    last_recording_ = RecordingStatus{};
    model_->table().end_of_stream();
    refresh_track_views();
    update_markers();
    radio_label_->setText(tr("Radio stopped"));
    stream_label_->setText(tr("Stopped"));
    speed_label_->clear();
    tracks_label_->clear();
    cpu_label_->clear();
    update_radio_controls();
    update_transport();
}

void MainWindow::update_radio_status(const StreamStatus& status) {
    if (!runner_) return;
    IqSource& source = runner_->source();
    QStringList parts;
    bool silent = false;
    if (const RadioControl* radio = source.radio()) {
        const RadioStatus r = radio->radio_status();
        delivery_.update(r.received, status.wall_s);
        silent = delivery_.stalled();
        if (silent) parts << tr("<b>THE RADIO HAS DELIVERED NOTHING FOR %1 s</b>").arg(delivery_.silent_s(), 0, 'f', 0);
        if (r.gain_db) parts << tr("Gain %1 dB").arg(*r.gain_db, 0, 'f', 1);
        if (r.overload) parts << tr("<b>ADC OVERLOAD: more gain reduction</b>");
        if (r.resets > 0) parts << tr("stream restarted %1 times").arg(r.resets);
    }
    if (const std::uint64_t lost = source.dropped_samples(); lost > 0)
        parts << tr("pipeline lost %1 s").arg(static_cast<double>(lost) / source.sample_rate(), 0, 'f', 2);
    if (const RecordControl* recorder = source.recorder()) {
        const RecordingStatus was = last_recording_;
        last_recording_ = recorder->recording_status();
        if (was.active != last_recording_.active) update_radio_controls();
        if (!last_recording_.path.empty()) {
            const double seconds = static_cast<double>(last_recording_.samples) / source.sample_rate();
            QString line = (last_recording_.active ? (silent ? tr("<b>RECORDING NOTHING</b>, so far %1 s, %2 MB") : tr("REC %1 s, %2 MB")) : tr("Saved %1 s, %2 MB"))
                               .arg(seconds, 0, 'f', 1)
                               .arg(static_cast<double>(last_recording_.samples) * 4.0 / 1e6, 0, 'f', 1);
            if (last_recording_.dropped > 0) line += tr(", <b>disk lost %1 samples</b>").arg(last_recording_.dropped);
            if (last_recording_.full) line += tr(", stopped at the 4 GiB limit");
            if (!last_recording_.error.empty()) line += QStringLiteral(", ") + QString::fromStdString(last_recording_.error);
            parts << line;
        }
    }
    if (!recording_problem_.isEmpty()) parts << QStringLiteral("<b>%1</b>").arg(recording_problem_.toHtmlEscaped());
    if (!gain_problem_.isEmpty()) parts << QStringLiteral("<b>%1</b>").arg(gain_problem_.toHtmlEscaped());
    radio_label_->setText(parts.join(QStringLiteral("; ")));
}

void MainWindow::update_position(const StreamStatus& status) {
    if (!total_s_ || *total_s_ <= 0.0 || position_slider_->isSliderDown()) return;
    const QSignalBlocker blocker(position_slider_);
    position_slider_->setValue(static_cast<int>(kPositionSteps * std::min(1.0, (origin_s_ + status.stream_s) / *total_s_)));
}

void MainWindow::build_scale_controls() {
    auto* bar = findChild<QToolBar*>();
    auto_floor_ = new QCheckBox(tr("Auto floor"));
    auto_floor_->setChecked(true);
    auto_floor_->setToolTip(tr("Hold the color scale's floor 5 dB below the median of the band's bins"));
    bar->addWidget(auto_floor_);
    floor_slider_ = new QSlider(Qt::Horizontal);
    floor_slider_->setRange(-160, 0);
    floor_slider_->setFixedWidth(160);
    floor_slider_->setEnabled(false);
    bar->addWidget(floor_slider_);
    floor_label_ = new QLabel;
    floor_label_->setMinimumWidth(90);
    bar->addWidget(floor_label_);
    bar->addWidget(new QLabel(tr("  Range: ")));
    range_slider_ = new QSlider(Qt::Horizontal);
    range_slider_->setRange(10, 120);
    range_slider_->setFixedWidth(160);
    bar->addWidget(range_slider_);
    range_label_ = new QLabel;
    range_label_->setMinimumWidth(120);
    bar->addWidget(range_label_);

    const auto show_floor = [this](double floor_dbfs) {
        floor_label_->setText(tr("Floor %1 dBFS").arg(floor_dbfs, 0, 'f', 0));
    };
    connect(auto_floor_, &QCheckBox::toggled, this, [this](bool on) {
        floor_slider_->setEnabled(!on);
        waterfall_->set_auto_floor(on);
    });
    connect(floor_slider_, &QSlider::valueChanged, this, [this, show_floor](int value) {
        if (auto_floor_->isChecked()) return;
        waterfall_->set_floor_dbfs(value);
        show_floor(value);
    });
    connect(range_slider_, &QSlider::valueChanged, this, [this](int value) {
        waterfall_->set_range_db(value);
        range_label_->setText(tr("%1 dB above floor").arg(value));
    });
    connect(waterfall_, &WaterfallWidget::floorChanged, this, [this, show_floor](double floor_dbfs) {
        const QSignalBlocker blocker(floor_slider_);
        floor_slider_->setValue(static_cast<int>(std::lround(floor_dbfs)));
        show_floor(floor_dbfs);
    });
    const DbScale& scale = waterfall_->scale();
    floor_slider_->setValue(static_cast<int>(std::lround(scale.floor_dbfs)));
    show_floor(scale.floor_dbfs);
    range_slider_->setValue(static_cast<int>(std::lround(scale.range_db)));
    range_label_->setText(tr("%1 dB above floor").arg(static_cast<int>(std::lround(scale.range_db))));
}

void MainWindow::build_status_bar() {
    stream_label_ = new QLabel;
    speed_label_ = new QLabel;
    tracks_label_ = new QLabel;
    calls_label_ = new QLabel;
    cpu_label_ = new QLabel;
    statusBar()->addWidget(stream_label_);
    statusBar()->addWidget(speed_label_);
    statusBar()->addWidget(tracks_label_);
    statusBar()->addWidget(calls_label_);
    statusBar()->addPermanentWidget(cpu_label_);
    cpu_label_->setToolTip(tr("Over the last 10 s of wall clock. Pipeline: CPU of the pipeline's thread (spectrum, "
                              "detector, channelizer, decoders, waterfall rows) per second of channel output, summed "
                              "over channels. Decoder: time inside the decoders, the same way. The GUI's thread, painting, is not in this figure."));
}

bool MainWindow::open_recording(const QString& path, double start_s) {
    try {
        // The file first: one that cannot be opened leaves whatever is running as it is.
        // I/Q only, a one-channel file refused as before: receiver audio needs its passband, which the app does not set yet
        // (backlog section 3).
        std::unique_ptr<FileIqSource> source = std::make_unique<FileIqSource>(
            std::filesystem::path(path.toStdWString()), start_s, automation_.recording_center_hz,
            RecordingOptions{.mirror = automation_.mirror, .input = RecordingInput::Iq});
        input_note_ = QStringLiteral("file_rate_hz: %1\nresampled_to_hz: %2\nmirrored: %3\n")
                          .arg(source->recording().file_rate())
                          .arg(source->recording().resampled() ? QString::number(source->recording().sample_rate()) : QStringLiteral("none"))
                          .arg(source->recording().mirrored() ? 1 : 0);
        stop_stream();
        radio_on_ = false;
        switch (source->center_origin()) {
            case FileIqSource::CenterOrigin::Given: center_origin_ = QStringLiteral("given"); break;
            case FileIqSource::CenterOrigin::Header: center_origin_ = QStringLiteral("header"); break;
            case FileIqSource::CenterOrigin::FileName: center_origin_ = QStringLiteral("file name"); break;
            case FileIqSource::CenterOrigin::Unknown: center_origin_ = QStringLiteral("unknown"); break;
        }
        source->set_speed(speed_);
        current_path_ = path;
        origin_s_ = start_s;
        const double length_s = source->recording_length_s();
        start_stream(std::move(source));
        total_s_ = length_s;
        return true;
    } catch (const std::exception& e) {
        report_failure(tr("Cannot open %1: %2").arg(path, QString::fromStdString(e.what())));
        return false;
    }
}

void MainWindow::report_failure(const QString& message) {
    std::fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
    if (automation_.active()) {
        QFile report(automation_.report_path);
        if (!automation_.report_path.isEmpty() && report.open(QIODevice::WriteOnly | QIODevice::Text))
            QTextStream(&report) << "error: " << message << "\n";
        if (!automation_.screenshot_path.isEmpty()) {
            radio_label_->setText(message);
            QApplication::processEvents();
            grab().save(automation_.screenshot_path);
        }
        QTimer::singleShot(0, qApp, [] { QApplication::exit(1); });
    } else {
        QMessageBox::critical(this, tr("KZ4AP Skimmer"), message);
    }
}

void MainWindow::start_stream(std::unique_ptr<IqSource> source) {
    stop_stream();
    const int rate = source->sample_rate();
    const int fft_size = choose_fft_size(rate);
    source_name_ = source->description();
    total_s_ = std::nullopt;
    setWindowTitle(tr("KZ4AP Skimmer - %1 (%2 kHz, decoder: %3)")
                       .arg(QString::fromStdString(source_name_))
                       .arg(rate / 1000.0)
                       .arg(QString::fromStdString(decoder_name_)));

    model_->clear();
    model_->table().set_keep_retired(!automation_.report_path.isEmpty() || !automation_.decoded_path.isEmpty());
    // Only the segmental decoder's tracks are judged: the list then keeps to text that reads as plausible.
    model_->table().set_plausible_only(front_end_from_name(decoder_name_) == FrontEnd::Segmental);
    model_->set_bin_width_hz(static_cast<double>(rate) / fft_size);
    model_->set_center_hz(source->center_frequency_hz());
    // Decoder-only mode runs no call stage at all.
    if (decoder_only_) {
        calls_->stop();
    } else {
        calls_->start(known_calls_, source->center_frequency_hz());
    }
    call_stage_cpu_s_ = 0.0;
    stream_center_hz_ = source->center_frequency_hz();
    stream_start_unix_ms_ = source->start_unix_ms();
    // A new stream has new tracks: the pane starts empty.
    follow_.clear();
    update_pane();
    waterfall_->start(rate, fft_size, source->center_frequency_hz(), origin_s_);
    meter_ = CpuMeter{};
    stream_ended_ = false;
    rows_dropped_ = 0;
    last_recording_ = RecordingStatus{};
    delivery_ = DeliveryWatch{};
    recording_problem_.clear();
    gain_problem_.clear();

    // The engine's hop is half its FFT (EngineConfig; Engine::Engine).
    const double hop_s = (fft_size / 2.0) / rate;
    feed_ = std::make_unique<DisplayFeed>(frames_per_row_for(kRowPeriodS, hop_s));
    DisplayFeed* feed = feed_.get();
    EngineConfig config = engine_config_for_decoder(decoder_name_);
    config.worker_threads = threads_;
    runner_ = std::make_unique<StreamRunner>(std::move(source), std::move(config),
                                             [feed](const Event& event) { feed->on_event(event); });
    update_transport();
    update_radio_controls();
}

void MainWindow::stop_stream() {
    runner_.reset();  // joins the pipeline's thread
    feed_.reset();
}

void MainWindow::set_speed_from_combo() {
    speed_ = speed_box_->currentData().toDouble();
    if (runner_) {
        if (PlaybackControl* playback = runner_->source().playback()) playback->set_speed(speed_);
    }
}

void MainWindow::update_transport() {
    PlaybackControl* playback = runner_ ? runner_->source().playback() : nullptr;
    play_pause_->setEnabled(playback != nullptr && !stream_ended_);
    speed_box_->setEnabled(playback != nullptr || !runner_);
    restart_->setEnabled(!current_path_.isEmpty());
    position_slider_->setEnabled(playback != nullptr);
    play_pause_->setText(playback && playback->paused() ? tr("Play") : tr("Pause"));
}

void MainWindow::on_tick() {
    ++ticks_;
    if (!runner_) {
        if (automation_.active()) run_transport_steps(StreamStatus{});
        return;
    }
    if (stream_ended_) {  // nothing more will come
        run_automation(runner_->status());
        return;
    }
    // The status first: if it says Finished, everything the stream published is in the batch that follows.
    const StreamStatus status = runner_->status();
    DisplayBatch batch = feed_->drain();
    rows_dropped_ = batch.rows_dropped;
    waterfall_->add_rows(std::move(batch.rows));
    TrackTable& table = model_->table();
    table.advance(status.stream_s);
    for (const TrackUpdate& update : batch.updates) std::visit([&](const auto& event) { table.apply(event); }, update);
    // The call stage, when it runs, takes the same events in the same order; its CPU is counted apart (the report's call_stage_cpu_s).
    callsign::CallSearch* calls = calls_->search();
    const double stage_from_s = calls ? thread_cpu_precise_seconds() : 0.0;
    if (calls) {
        for (const TrackUpdate& update : batch.updates) std::visit([&](const auto& event) { calls->apply(event); }, update);
    }
    meter_.push(status);
    most_live_ = std::max(most_live_, table.live_count());

    const bool ended = status.state == StreamStatus::State::Finished || status.state == StreamStatus::State::Failed;
    if (calls && ended && !stream_ended_) calls->finish();
    if (calls) call_stage_cpu_s_ += thread_cpu_precise_seconds() - stage_from_s;
    if (ended && !stream_ended_) {
        stream_ended_ = true;
        table.end_of_stream();
        update_transport();
        if (status.state == StreamStatus::State::Failed) {
            if (RecordControl* recorder = runner_->source().recorder()) recorder->stop_recording();
            radio_on_ = false;
            update_radio_controls();
            if (!automation_.active()) QMessageBox::critical(this, tr("KZ4AP Skimmer"), QString::fromStdString(status.error));
        }
    }

    if (ticks_ % kTextEveryTicks == 0 || ended) {
        refresh_track_views();
        if (calls_->search()) {
            const double from_s = thread_cpu_precise_seconds();
            calls_->refresh(status.stream_s);
            call_stage_cpu_s_ += thread_cpu_precise_seconds() - from_s;
        }
        update_radio_status(status);  // first: the markers and the status show a radio that has stopped delivering
        update_markers();
        update_call_labels(status.stream_s);
    }
    if (ticks_ % kStatusEveryTicks == 0 || ended) {
        update_status(status);
        update_position(status);
    }
    run_automation(status);
}

void MainWindow::update_call_labels(double now_s) {
    std::vector<WaterfallWidget::CallLabel> labels;
    if (calls_->search()) {
        std::vector<callsign::CallRow> rows = calls_->shown_rows();
        // By when the station itself was last heard where the row places it (CallRow::heard_s: its own readings, not those that name it,
        // within the stage's same place of freq_hz); the latest first: they take the first lanes, and the others what room is left. A call
        // only named by another station has none (its frequency is that station's): no label.
        std::sort(rows.begin(), rows.end(), [](const callsign::CallRow& a, const callsign::CallRow& b) { return a.heard_s > b.heard_s; });
        for (const callsign::CallRow& row : rows) {
            if (row.heard_s < 0 || now_s - row.heard_s > kCallLabelAgeS) continue;
            labels.push_back({row.freq_hz, QString::fromStdString(row.call), now_s - row.heard_s <= kCallLabelRecentS});
        }
    }
    waterfall_->set_call_labels(std::move(labels));
}

void MainWindow::update_markers() {
    std::vector<WaterfallWidget::Marker> markers;
    for (const TrackRow& row : model_->table().rows())
        markers.push_back({row.id, row.freq_hz, row.live() && !delivery_.stalled(), follow_.row_id() == row.id});
    waterfall_->set_markers(std::move(markers));
}

void MainWindow::update_status(const StreamStatus& status) {
    // s of stream. The first seconds' replays spend their CPU over few live channel-seconds, and read 1 000 ms per channel-second or
    // more, which says nothing about the load. Heuristic: past the first stations' replays.
    constexpr double kCpuShownFromS = 10.0;
    const QString stream = total_s_ ? tr("Stream %1 of %2 s").arg(origin_s_ + status.stream_s, 0, 'f', 1).arg(*total_s_, 0, 'f', 1)
                                    : tr("Stream %1 s").arg(status.stream_s, 0, 'f', 1);
    QString state;
    switch (status.state) {
        case StreamStatus::State::Starting: state = tr(" (starting)"); break;
        case StreamStatus::State::Finished: state = tr(" (finished)"); break;
        case StreamStatus::State::Failed: state = tr(" (failed)"); break;
        default: break;
    }
    PlaybackControl* playback = runner_->source().playback();
    if (playback && playback->paused() && status.state == StreamStatus::State::Running) state = tr(" (paused)");
    if (delivery_.stalled()) state = tr(" (the radio is silent)");
    stream_label_->setText(stream + state);

    const CpuRates recent = meter_.recent();
    QString speed = recent.speed ? tr("Speed %1x").arg(*recent.speed, 0, 'f', 2) : tr("Speed -");
    if (rows_dropped_ > 0) speed += tr(", waterfall skipped %1 rows").arg(rows_dropped_);
    speed_label_->setText(speed);

    if (calls_->search()) {
        calls_label_->setText(calls_->signed_only() ? tr("Calls %1 signed of %2").arg(calls_->shown_count()).arg(calls_->reported_count())
                                                    : tr("Calls %1").arg(calls_->reported_count()));
    }
    // Hidden rows are live tracks too: without the count, 59 live over nine rows in view reads as a fault.
    const auto live_hidden = static_cast<std::size_t>(std::count_if(model_->table().hidden().begin(), model_->table().hidden().end(),
                                                                    [](const TrackRow& row) { return row.live(); }));
    QString tracks = tr("Live tracks %1, %2 of them hidden").arg(model_->table().live_count()).arg(live_hidden);
    if (status.track_budget > 0) {
        // A budget in force while the stream keeps up is the budget coming back up, one track per s.
        const bool keeping_up = recent.speed && *recent.speed >= 0.99;
        tracks += (keeping_up ? tr(" (recovering: at most %1)") : tr(" (falling behind: at most %1)")).arg(status.track_budget);
    }
    tracks_label_->setText(tracks);

    if (status.stream_s < kCpuShownFromS) {
        cpu_label_->setText(tr("CPU: shown from %1 s of stream").arg(kCpuShownFromS, 0, 'f', 0));
    } else if (recent.pipeline_ms_per_channel_s && recent.per_track_ms_per_channel_s && recent.fixed_ms_per_stream_s) {
        cpu_label_->setText(tr("CPU: %1 ms per channel-second in the pipeline; fixed %2 ms per s of stream, each track %3 ms per channel-second")
                                .arg(*recent.pipeline_ms_per_channel_s, 0, 'f', 2)
                                .arg(*recent.fixed_ms_per_stream_s, 0, 'f', 1)
                                .arg(*recent.per_track_ms_per_channel_s, 0, 'f', 2));
    } else {
        cpu_label_->setText(tr("CPU: no channel output"));
    }
}

void MainWindow::refresh_track_views() {
    // The pane first: the list's new layout selects the followed row again (restore_selection), which may now be another track's.
    const std::optional<std::uint32_t> was = follow_.row_id();
    const bool pane_changed = follow_.update(model_->table());
    model_->refresh();
    if (!pane_changed) return;
    update_pane();
    // The pane moved on to another row (a successor): the list's highlight and the waterfall's marker go with it now, not at the next
    // change of the list's layout.
    if (follow_.row_id() != was) {
        restore_selection();
        update_markers();
    }
}

void MainWindow::selection_changed() {
    // An empty selection leaves the pane as it is: it goes on following.
    const QModelIndexList rows = table_view_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;
    const std::optional<std::uint32_t> id = model_->track_at(rows.front().row());
    if (!id) return;
    const TrackTable& table = model_->table();
    // The followed row itself, selected again after the list's layout changed: nothing to do.
    if (const auto row = table.row_of(*id); row && follow_.is_row(table.rows()[*row])) return;
    follow_track(*id);
}

void MainWindow::restore_selection() {
    const std::optional<std::uint32_t> id = follow_.row_id();
    if (!id) return;
    if (const auto row = model_->table().row_of(*id)) table_view_->selectRow(static_cast<int>(*row));
}

void MainWindow::follow_track(std::uint32_t id) {
    if (!follow_.follow(model_->table(), id)) return;
    pane_shown_.clear();
    update_pane();
    if (const auto row = model_->table().row_of(id); row && static_cast<int>(*row) < model_->rowCount()) {
        table_view_->selectRow(static_cast<int>(*row));  // selection_changed finds the followed row and stops there
        table_view_->scrollTo(model_->index(static_cast<int>(*row), 0));
    }
    update_markers();
}

void MainWindow::follow_frequency(double freq_hz, const QString& what) {
    if (const std::optional<std::uint32_t> id = row_near(model_->table(), freq_hz, TrackTable::kSamePlaceHz)) {
        follow_track(*id);
        return;
    }
    const QString message = tr("No track within %1 Hz of %2").arg(TrackTable::kSamePlaceHz, 0, 'f', 0).arg(what);
    statusBar()->showMessage(message, 4000);  // ms
    transport_log_ << QStringLiteral("follow: %1").arg(message);
}

QWidget* MainWindow::build_pane() {
    auto* pane = new QWidget;
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);
    pane_label_ = new QLabel;
    pane_label_->setTextFormat(Qt::PlainText);
    layout->addWidget(pane_label_);
    pane_text_ = new QPlainTextEdit;
    pane_text_->setReadOnly(true);
    pane_text_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    pane_text_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pane_text_->setPlaceholderText(tr("Click a signal on the waterfall, in the track list or in the call list: its text comes here as it is decoded."));
    pane_text_->setToolTip(tr("One signal's text as it is decoded, the newest at the bottom. It follows the station on through a new track at "
                              "its place (within %1 Hz), after the old one ends.")
                               .arg(TrackTable::kSamePlaceHz, 0, 'f', 0));
    layout->addWidget(pane_text_, 1);
    pane->setMinimumHeight(60);  // pixels
    return pane;
}

void MainWindow::update_pane() {
    if (pane_label_ == nullptr) return;
    if (!follow_.active()) {
        pane_label_->setText(tr("One signal: none followed"));
        if (!pane_shown_.isEmpty() || !pane_text_->toPlainText().isEmpty()) pane_text_->clear();
        pane_shown_.clear();
        return;
    }
    const double freq_hz = follow_.freq_hz();
    const QString frequency = stream_center_hz_ ? tr("%1 kHz").arg((*stream_center_hz_ + freq_hz) / 1000.0, 0, 'f', 3)
                                                : tr("%1 kHz from the center").arg(QString::asprintf("%+.3f", freq_hz / 1000.0));
    const QString speed = follow_.wpm() > 0 ? tr("%1 WPM").arg(follow_.wpm(), 0, 'f', 1) : tr("- WPM");
    QString state = follow_.live() ? tr("live") : (follow_.row_id() ? tr("ended") : tr("ended, no longer listed"));
    if (follow_.rows_followed() > 1) state += tr("; %1 rows followed at this place").arg(follow_.rows_followed());
    const std::optional<std::uint32_t> id = follow_.row_id();
    pane_label_->setText(id ? tr("One signal: track %1, %2, %3, %4").arg(*id).arg(frequency, speed, state)
                            : tr("One signal: %1, %2, %3").arg(frequency, speed, state));
    const QString text = QString::fromStdString(follow_.text());
    if (text == pane_shown_) return;
    // The newest text in view, unless the user has scrolled back to read: then the place read stays.
    QScrollBar* bar = pane_text_->verticalScrollBar();
    const bool at_end = bar->value() >= bar->maximum();
    const int was = bar->value();
    pane_text_->setPlainText(text);
    bar->setValue(at_end ? bar->maximum() : was);
    pane_shown_ = text;
}

void MainWindow::run_transport_steps(const StreamStatus& status) {
    const double wall_s = automation_clock_.elapsed() / 1000.0;
    while (next_transport_step_ < automation_.transport.size() &&
           wall_s >= automation_.transport[next_transport_step_].at_wall_s) {
        const QString action = automation_.transport[next_transport_step_++].action;
        transport_log_ << QStringLiteral("transport: wall %1 s, stream %2 s: %3")
                              .arg(wall_s, 0, 'f', 2)
                              .arg(status.stream_s, 0, 'f', 2)
                              .arg(action);
        PlaybackControl* playback = runner_ ? runner_->source().playback() : nullptr;
        if (action == QStringLiteral("restart")) {
            restart_->trigger();
        } else if (action == QStringLiteral("pause") || action == QStringLiteral("play")) {
            if (playback && playback->paused() == (action == QStringLiteral("play"))) play_pause_->trigger();
        } else if (action.startsWith(QStringLiteral("speed="))) {
            const QString value = action.mid(6);
            const double speed = value == QStringLiteral("max") ? kAsFastAsPossible : value.toDouble();
            speed_box_->setCurrentIndex(speed_box_->findData(speed));
        } else if (action.startsWith(QStringLiteral("seek="))) {
            if (!current_path_.isEmpty()) open_recording(current_path_, action.mid(5).toDouble());
        } else if (action == QStringLiteral("radio")) {
            start_radio();
        } else if (action == QStringLiteral("radiostop")) {
            if (radio_on_) radio_action_->trigger();
        } else if (action.startsWith(QStringLiteral("open="))) {
            open_recording(action.mid(5));
        } else if (action.startsWith(QStringLiteral("serial="))) {
            if (!radio_on_) serial_box_->setText(action.mid(7));
        } else if (action.startsWith(QStringLiteral("center="))) {
            center_box_->setValue(action.mid(7).toDouble());
        } else if (action.startsWith(QStringLiteral("rate="))) {
            rate_box_->setCurrentIndex(rate_box_->findData(action.mid(5).toInt()));
        } else if (action.startsWith(QStringLiteral("gain="))) {
            const QStringList values = action.mid(5).split(QLatin1Char(','));
            if (values.size() == 2) {
                if_gain_box_->setValue(values[0].toInt());
                lna_box_->setValue(values[1].toInt());
            }
        } else if (action.startsWith(QStringLiteral("record="))) {
            begin_recording(action.mid(7));
        } else if (action == QStringLiteral("stoprecord")) {
            if (RecordControl* recorder = runner_ ? runner_->source().recorder() : nullptr) {
                recorder->stop_recording();
                last_recording_ = recorder->recording_status();
                transport_log_ << QStringLiteral("recording: %1 samples, %2 lost to the disk, %3")
                                      .arg(last_recording_.samples)
                                      .arg(last_recording_.dropped)
                                      .arg(QString::fromStdWString(last_recording_.path.wstring()));
                update_radio_controls();
            }
        } else if (action.startsWith(QStringLiteral("shot="))) {
            refresh_track_views();
            update_markers();
            if (runner_) {
                update_status(status);
                update_radio_status(status);
            }
            QApplication::processEvents();
            grab().save(action.mid(5));
        } else if (action == QStringLiteral("mode=decoder") || action == QStringLiteral("mode=skimmer")) {
            mode_box_->setCurrentIndex(action.endsWith(QStringLiteral("decoder")) ? 1 : 0);
        } else if (action == QStringLiteral("list=all") || action == QStringLiteral("list=signed")) {
            list_box_->setCurrentIndex(action.endsWith(QStringLiteral("signed")) ? 1 : 0);
        } else if (action.startsWith(QStringLiteral("known="))) {
            load_known_calls(action.mid(6), tr("from"));
        } else if (action == QStringLiteral("noknown")) {
            set_known_calls(nullptr, {}, {}, KnownKind::None);
        } else if (action == QStringLiteral("download")) {
            known_download_->trigger();
        } else if (action.startsWith(QStringLiteral("follow="))) {
            const double hz = action.mid(7).toDouble() * 1000.0 - stream_center_hz_.value_or(0.0);
            follow_frequency(hz, tr("%1 kHz").arg(action.mid(7)));
        } else if (action == QStringLiteral("quit")) {
            if (!automation_.report_path.isEmpty()) write_report(status);
            if (!automation_.decoded_path.isEmpty()) write_decoded(status.stream_s);
            if (!automation_.calls_path.isEmpty()) write_calls();
            QApplication::quit();
        }
    }
}

void MainWindow::run_automation(const StreamStatus& status) {
    if (!automation_.active()) return;
    run_transport_steps(status);
    if (!runner_) return;
    const bool ended = stream_ended_ || status.state == StreamStatus::State::Stopped;
    if (!automation_.screenshot_path.isEmpty() && !screenshot_done_ && (status.stream_s >= automation_.screenshot_at_s || ended)) {
        refresh_track_views();
        update_markers();
        update_status(status);
        QApplication::processEvents();
        grab().save(automation_.screenshot_path);
        screenshot_done_ = true;
    }
    const bool quitting = automation_.quit_at_end || automation_.quit_at_s;
    const bool failed = status.state == StreamStatus::State::Failed;
    const bool quit_now = (quitting && ended) || (automation_.quit_at_s && status.stream_s >= *automation_.quit_at_s);
    if ((quit_now || ended) && !report_done_) {
        if (!automation_.report_path.isEmpty()) write_report(status);
        if (!automation_.decoded_path.isEmpty()) write_decoded(status.stream_s);
        if (!automation_.calls_path.isEmpty()) write_calls();
        report_done_ = true;
    }
    if (quit_now) QApplication::exit(failed ? 1 : 0);
}

void MainWindow::write_calls() {
    take_stream_start();
    // The list as shown: all calls, or the signed ones only; in decoder-only mode, the header alone.
    QFile file(automation_.calls_path);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(callsign::calls_csv(calls_->shown_rows(), calls_->center_hz(), stream_start_unix_ms_).c_str());
}

void MainWindow::take_stream_start() {
    // A live radio's start is known only once its first block has come (SdrplaySource::start_unix_ms), after the stream began.
    if (!stream_start_unix_ms_ && runner_) stream_start_unix_ms_ = runner_->source().start_unix_ms();
}

void MainWindow::write_decoded(double stream_s) {
    take_stream_start();
    DecodedFileInfo info;
    info.front_end = decoder_name_;
    info.recording = source_name_;
    info.stream_start_unix_ms = stream_start_unix_ms_;
    info.stream_s = stream_s;
    info.written_unix_ms = system_unix_ms();
    if (runner_) {
        info.lost_samples = runner_->source().dropped_samples();  // the report's pipeline_lost_samples
        if (runner_->source().radio() != nullptr) info.radio_resets = runner_->source().radio()->radio_status().resets;
    }
    const std::string text = decoded_json(info, model_->table().rows(), model_->table().retired());
    QFile file(automation_.decoded_path);
    if (file.open(QIODevice::WriteOnly)) file.write(text.data(), static_cast<qint64>(text.size()));
}

void MainWindow::write_report(const StreamStatus& status) {
    report_done_ = true;
    take_stream_start();
    QFile file(automation_.report_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&file);
    const CpuRates whole = meter_.whole_run();
    const CpuRates recent = meter_.recent();
    const auto number = [](const std::optional<double>& v) {
        return v ? QString::number(*v, 'f', 3) : QStringLiteral("none");
    };
    out << "recording: " << QString::fromStdString(source_name_) << "\n";
    out << "decoder: " << QString::fromStdString(decoder_name_) << "\n";
    out << "center_hz: " << (stream_center_hz_ ? QString::number(*stream_center_hz_, 'f', 1) : QStringLiteral("none")) << "\n";
    if (!radio_on_) out << "center_from: " << center_origin_ << "\n" << input_note_;
    out << "stream_start_utc: "
        << (stream_start_unix_ms_ ? QString::fromStdString(utc_text(*stream_start_unix_ms_)) : QStringLiteral("none")) << "\n";
    out << "stream_s: " << status.stream_s << "\n";
    out << "wall_s: " << status.wall_s << "\n";
    out << "channel_seconds: " << status.channel_seconds << "\n";
    out << "replayed_channel_seconds: " << status.replayed_channel_seconds << "\n";
    out << "longest_replay_wait_s: " << status.longest_replay_wait_s << "\n";
    out << "live_tracks_at_end: " << model_->table().live_count() << "\n";
    out << "tracks_opened: " << model_->table().opened() << "\n";
    out << "track_budget_at_end: " << status.track_budget << "\n";
    out << "live_tracks_most: " << most_live_ << "\n";
    out << "pipeline_cpu_ms_per_channel_s: " << number(whole.pipeline_ms_per_channel_s) << "\n";
    out << "decoder_ms_per_channel_s: " << number(whole.decoder_ms_per_channel_s) << "\n";
    out << "pipeline_cpu_ms_per_channel_s_last_10s: " << number(recent.pipeline_ms_per_channel_s) << "\n";
    out << "fixed_ms_per_stream_s: " << number(whole.fixed_ms_per_stream_s) << "\n";
    out << "per_track_ms_per_channel_s: " << number(whole.per_track_ms_per_channel_s) << "\n";
    out << "realtime_factor: " << number(whole.speed) << "\n";
    out << "threads: " << threads_ << "\n";
    out << "settings: " << (startup_.store.enabled() ? QDir::toNativeSeparators(startup_.store.path()) : QStringLiteral("none")) << "\n";
    const double process_s = process_cpu_seconds();
    out << "process_cpu_s: " << process_s << "\n";
    out << "process_cpu_ms_per_channel_s: "
        << (status.channel_seconds > 0 ? QString::number(1000.0 * process_s / status.channel_seconds, 'f', 3) : QStringLiteral("none")) << "\n";
    out << "pipeline_thread_cpu_s: " << status.thread_cpu_s << "\n";
    // The call stage: its mode, the list's filter and its known-call list, what it found, and the GUI thread's CPU inside it and its
    // list's model this stream (painting the list is not in it).
    out << "mode: " << (calls_->search() ? "skimmer" : "decoder-only") << "\n";
    out << "call_list: " << (calls_->signed_only() ? "signed" : "all") << "\n";
    out << "known_calls: " << (known_calls_ ? QStringLiteral("%1 from %2").arg(known_calls_->size()).arg(QDir::toNativeSeparators(known_path_)) : QStringLiteral("none")) << "\n";
    if (const callsign::CallSearch* calls = calls_->search()) {
        const std::vector<callsign::CallRow>& rows = calls->calls();
        const auto signed_rows = std::count_if(rows.begin(), rows.end(), callsign::is_signed);
        const auto named_rows = std::count_if(rows.begin(), rows.end(), [](const callsign::CallRow& r) { return !callsign::is_signed(r) && !r.heard; });
        out << "calls_reported: " << rows.size() << "\n";
        out << "calls_signed: " << signed_rows << "\n";
        out << "calls_bare: " << static_cast<qint64>(rows.size()) - signed_rows - named_rows << "\n";
        out << "calls_named: " << named_rows << "\n";
        out << "calls_shown: " << calls_->shown_count() << "\n";
    }
    out << "call_stage_cpu_s: " << QString::number(call_stage_cpu_s_, 'f', 4) << "\n";
    out << "call_stage_ms_per_channel_s: "
        << (status.channel_seconds > 0 ? QString::number(1000.0 * call_stage_cpu_s_ / status.channel_seconds, 'f', 4) : QStringLiteral("none")) << "\n";
    // The one-signal pane, when it follows one: where, how many rows it has followed there, and its text.
    if (follow_.active()) {
        out << "pane_freq_hz: " << QString::number(follow_.freq_hz(), 'f', 1) << "\n";
        out << "pane_wpm: " << QString::number(follow_.wpm(), 'f', 1) << "\n";
        out << "pane_rows_followed: " << follow_.rows_followed() << "\n";
        out << "pane_text: " << QString::fromStdString(follow_.text()) << "\n";
    }
    out << "rows_skipped: " << rows_dropped_ << "\n";
    if (runner_) out << "pipeline_lost_samples: " << runner_->source().dropped_samples() << "\n";
    if (runner_ && runner_->source().radio() != nullptr) {
        const RadioStatus radio = runner_->source().radio()->radio_status();
        out << "radio_received_samples: " << radio.received << "\n";
        out << "radio_silent_s: " << delivery_.silent_s() << "\n";
        out << "radio_overload_now: " << radio.overload << "\n";
        out << "radio_resets: " << radio.resets << "\n";
    }
    out << "recording_samples: " << last_recording_.samples << "\n";
    out << "recording_dropped: " << last_recording_.dropped << "\n";
    out << "live_tracks: " << model_->table().live_count() << "\n";
    out << "error: " << QString::fromStdString(status.error) << "\n";
    for (const QString& line : transport_log_) out << line << "\n";
    const auto write_rows = [&](const std::vector<TrackRow>& rows, const char* word) {
        for (const TrackRow& row : rows) {
            out << word << " " << row.id << " freq_hz " << QString::number(row.freq_hz, 'f', 1) << " wpm "
                << QString::number(row.wpm, 'f', 1) << " start_s " << QString::number(row.start_s, 'f', 2) << " tracks "
                << row.tracks << " text " << QString::fromStdString(row.text()) << "\n";
        }
    };
    write_rows(model_->table().rows(), "track");
    write_rows(model_->table().retired(), "track");
    // What the list never showed: it read nothing plausible.
    write_rows(model_->table().hidden(), "hidden");
    write_rows(model_->table().retired_hidden(), "hidden");
}

}  // namespace kz4ap::app
