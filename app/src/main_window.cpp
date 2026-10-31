#include "main_window.hpp"

#include "kz4ap/cpu_time.hpp"
#include "kz4ap/decoder_name.hpp"
#include "kz4ap/engine.hpp"
#include "kz4ap/file_iq_source.hpp"
#include "kz4ap/sdrplay_source.hpp"
#include "recording_name.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMessageBox>
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

// The GUI's clock, ms: about 30 pictures/s (heuristic). The list and the status bar change at a third of that.
constexpr int kTickMs = 33;
constexpr int kTextEveryTicks = 3;
// The position slider's steps across a recording.
constexpr int kPositionSteps = 1000;

}  // namespace

MainWindow::MainWindow(std::string decoder_name, double speed, int threads, RadioDefaults radio, Automation automation,
                       std::shared_ptr<const callsign::KnownCalls> known_calls)
    : decoder_name_(std::move(decoder_name)), speed_(speed), threads_(threads), automation_(std::move(automation)), known_calls_(std::move(known_calls)) {
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
    auto* calls_view = new QTableView;
    calls_view->setModel(sorted_calls);
    calls_view->setSortingEnabled(true);
    calls_view->sortByColumn(CallListModel::kLastHeard, Qt::AscendingOrder);
    calls_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    calls_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    calls_view->setWordWrap(false);
    calls_view->verticalHeader()->hide();
    calls_view->verticalHeader()->setDefaultSectionSize(22);
    calls_view->setColumnWidth(CallListModel::kCall, 100);
    calls_view->setColumnWidth(CallListModel::kFrequency, 115);
    calls_view->setColumnWidth(CallListModel::kLastHeard, 75);
    calls_view->setColumnWidth(CallListModel::kCount, 65);
    calls_view->setColumnWidth(CallListModel::kHow, 60);
    auto* lists = new QSplitter(Qt::Horizontal);
    lists->addWidget(table_view_);
    lists->addWidget(calls_view);
    lists->setStretchFactor(0, 3);
    lists->setStretchFactor(1, 1);
    lists->setSizes({920, 440});  // pixels

    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(waterfall_);
    splitter->addWidget(lists);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({600, 260});  // pixels; the list starts with room for about nine tracks
    setCentralWidget(splitter);

    build_transport();
    build_scale_controls();
    build_radio_controls();
    center_box_->setValue(radio.center_khz);
    rate_box_->setCurrentIndex(std::max(0, rate_box_->findData(radio.sample_rate)));
    if_gain_box_->setValue(radio.if_gain_reduction_db);
    lna_box_->setValue(radio.lna_state);
    build_status_bar();
    update_transport();
    update_radio_controls();

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
            QFileDialog::getOpenFileName(this, tr("Open an I/Q recording"), {}, tr("I/Q recordings (*.wav);;All files (*)"));
        if (!path.isEmpty() && may_end_recording(tr("Opening a recording"))) open_recording(path);
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
    bar->addWidget(new QLabel(tr(" Radio center: ")));
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

bool MainWindow::start_radio() {
    stop_stream();
    radio_on_ = false;
    try {
        SdrplaySettings settings;
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
    const QString suggested = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/') + QString::fromStdString(name);
    const QString path = QFileDialog::getSaveFileName(this, tr("Save the radio's I/Q"), suggested, tr("I/Q recordings (*.wav)"));
    if (!path.isEmpty()) begin_recording(path);
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
    model_->refresh();
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
                              "over channels. Decoder: time inside the decoders, the same way."));
}

bool MainWindow::open_recording(const QString& path, double start_s) {
    try {
        // The file first: one that cannot be opened leaves whatever is running as it is.
        std::unique_ptr<FileIqSource> source = std::make_unique<FileIqSource>(std::filesystem::path(path.toStdWString()), start_s);
        stop_stream();
        radio_on_ = false;
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
    calls_->start(known_calls_, source->center_frequency_hz());
    selected_track_.reset();
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
    callsign::CallSearch& calls = calls_->search();
    for (const TrackUpdate& update : batch.updates) {
        std::visit([&](const auto& event) {
            table.apply(event);
            calls.apply(event);
        }, update);
    }
    meter_.push(status);
    most_live_ = std::max(most_live_, table.live_count());

    const bool ended = status.state == StreamStatus::State::Finished || status.state == StreamStatus::State::Failed;
    if (ended && !stream_ended_) {
        stream_ended_ = true;
        table.end_of_stream();
        calls.finish();
        update_transport();
        if (status.state == StreamStatus::State::Failed) {
            if (RecordControl* recorder = runner_->source().recorder()) recorder->stop_recording();
            radio_on_ = false;
            update_radio_controls();
            if (!automation_.active()) QMessageBox::critical(this, tr("KZ4AP Skimmer"), QString::fromStdString(status.error));
        }
    }

    if (ticks_ % kTextEveryTicks == 0 || ended) {
        model_->refresh();
        calls_->refresh(status.stream_s);
        update_radio_status(status);  // first: the markers and the status show a radio that has stopped delivering
        update_markers();
        update_status(status);
        update_position(status);
    }
    run_automation(status);
}

void MainWindow::update_markers() {
    std::vector<WaterfallWidget::Marker> markers;
    for (const TrackRow& row : model_->table().rows())
        markers.push_back({row.id, row.freq_hz, row.live() && !delivery_.stalled(), selected_track_ == row.id});
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

    calls_label_->setText(tr("Calls %1").arg(calls_->search().calls().size()));
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
        cpu_label_->setText(tr("CPU: %1 ms per channel-second in all; fixed %2 ms per s of stream, each track %3 ms per channel-second")
                                .arg(*recent.pipeline_ms_per_channel_s, 0, 'f', 2)
                                .arg(*recent.fixed_ms_per_stream_s, 0, 'f', 1)
                                .arg(*recent.per_track_ms_per_channel_s, 0, 'f', 2));
    } else {
        cpu_label_->setText(tr("CPU: no channel output"));
    }
}

void MainWindow::selection_changed() {
    const QModelIndexList rows = table_view_->selectionModel()->selectedRows();
    selected_track_ = rows.isEmpty() ? std::nullopt : model_->track_at(rows.front().row());
    update_markers();
}

void MainWindow::restore_selection() {
    if (!selected_track_) return;
    const std::optional<std::uint32_t> keep = selected_track_;
    if (const auto row = model_->table().row_of(*keep)) {
        table_view_->selectRow(static_cast<int>(*row));
    } else {
        selected_track_.reset();
    }
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
            model_->refresh();
            update_markers();
            if (runner_) {
                update_status(status);
                update_radio_status(status);
            }
            QApplication::processEvents();
            grab().save(action.mid(5));
        } else if (action == QStringLiteral("quit")) {
            if (!automation_.report_path.isEmpty()) write_report(status);
            if (!automation_.decoded_path.isEmpty()) write_decoded();
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
        model_->refresh();
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
        if (!automation_.decoded_path.isEmpty()) write_decoded();
        if (!automation_.calls_path.isEmpty()) write_calls();
        report_done_ = true;
    }
    if (quit_now) QApplication::exit(failed ? 1 : 0);
}

void MainWindow::write_calls() {
    QFile file(automation_.calls_path);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(callsign::calls_csv(calls_->search().calls(), calls_->center_hz()).c_str());
}

void MainWindow::write_decoded() {
    QJsonArray tracks;
    const auto add = [&](const std::vector<TrackRow>& rows) {
        for (const TrackRow& row : rows) {
            QJsonObject track;
            track[QStringLiteral("id")] = static_cast<qint64>(row.id);
            track[QStringLiteral("freq_hz")] = row.freq_hz;
            track[QStringLiteral("last_freq_hz")] = row.freq_hz;
            track[QStringLiteral("text")] = QString::fromStdString(row.text());
            tracks.append(track);
        }
    };
    add(model_->table().rows());
    add(model_->table().retired());
    QJsonObject root;
    root[QStringLiteral("front_end")] = QString::fromStdString(decoder_name_);
    root[QStringLiteral("recording")] = QString::fromStdString(source_name_);
    root[QStringLiteral("tracks")] = tracks;
    QFile file(automation_.decoded_path);
    if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(root).toJson());
}

void MainWindow::write_report(const StreamStatus& status) {
    report_done_ = true;
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
    out << "process_cpu_s: " << process_cpu_seconds() << "\n";
    out << "pipeline_thread_cpu_s: " << status.thread_cpu_s << "\n";
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
