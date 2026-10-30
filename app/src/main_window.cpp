#include "main_window.hpp"

#include "kz4ap/cpu_time.hpp"
#include "kz4ap/decoder_name.hpp"
#include "kz4ap/engine.hpp"
#include "kz4ap/file_iq_source.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMessageBox>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTableView>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>

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

}  // namespace

MainWindow::MainWindow(std::string decoder_name, double speed, Automation automation)
    : decoder_name_(std::move(decoder_name)), speed_(speed), automation_(std::move(automation)) {
    setWindowTitle(tr("KZ4AP Skimmer"));
    resize(1360, 860);

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

    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(waterfall_);
    splitter->addWidget(table_view_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({600, 260});  // pixels; the list starts with room for about nine tracks
    setCentralWidget(splitter);

    build_transport();
    build_scale_controls();
    build_status_bar();
    update_transport();

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
        if (!path.isEmpty()) open_recording(path);
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
    cpu_label_ = new QLabel;
    statusBar()->addWidget(stream_label_);
    statusBar()->addWidget(speed_label_);
    statusBar()->addWidget(tracks_label_);
    statusBar()->addPermanentWidget(cpu_label_);
    cpu_label_->setToolTip(tr("Over the last 10 s of wall clock. Pipeline: CPU of the pipeline's thread (spectrum, "
                              "detector, channelizer, decoders, waterfall rows) per second of channel output, summed "
                              "over channels. Decoder: time inside the decoders, the same way."));
}

bool MainWindow::open_recording(const QString& path) {
    try {
        std::unique_ptr<FileIqSource> source = std::make_unique<FileIqSource>(std::filesystem::path(path.toStdWString()));
        source->set_speed(speed_);
        current_path_ = path;
        start_stream(std::move(source));
        return true;
    } catch (const std::exception& e) {
        const QString message = tr("Cannot open %1: %2").arg(path, QString::fromStdString(e.what()));
        std::fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
        if (automation_.active()) {
            QFile report(automation_.report_path);
            if (!automation_.report_path.isEmpty() && report.open(QIODevice::WriteOnly | QIODevice::Text))
                QTextStream(&report) << "error: " << message << "\n";
            QTimer::singleShot(0, qApp, [] { QApplication::exit(1); });
        } else {
            QMessageBox::critical(this, tr("KZ4AP Skimmer"), message);
        }
        return false;
    }
}

void MainWindow::start_stream(std::unique_ptr<IqSource> source) {
    stop_stream();
    const int rate = source->sample_rate();
    const int fft_size = choose_fft_size(rate);
    source_name_ = source->description();
    const std::optional<std::uint64_t> total = source->total_samples();
    total_s_ = total ? std::optional<double>(static_cast<double>(*total) / rate) : std::nullopt;
    setWindowTitle(tr("KZ4AP Skimmer - %1 (%2 kHz, decoder: %3)")
                       .arg(QString::fromStdString(source_name_))
                       .arg(rate / 1000.0)
                       .arg(QString::fromStdString(decoder_name_)));

    model_->clear();
    model_->table().set_keep_retired(!automation_.report_path.isEmpty());
    model_->set_bin_width_hz(static_cast<double>(rate) / fft_size);
    selected_track_.reset();
    waterfall_->start(rate, fft_size);
    meter_ = CpuMeter{};
    stream_ended_ = false;
    rows_dropped_ = 0;

    // The engine's hop is half its FFT (EngineConfig; Engine::Engine).
    const double hop_s = (fft_size / 2.0) / rate;
    feed_ = std::make_unique<DisplayFeed>(frames_per_row_for(kRowPeriodS, hop_s));
    DisplayFeed* feed = feed_.get();
    runner_ = std::make_unique<StreamRunner>(std::move(source), engine_config_for_decoder(decoder_name_),
                                             [feed](const Event& event) { feed->on_event(event); });
    update_transport();
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
    play_pause_->setText(playback && playback->paused() ? tr("Play") : tr("Pause"));
}

void MainWindow::on_tick() {
    ++ticks_;
    if (!runner_) return;
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
    meter_.push(status);

    const bool ended = status.state == StreamStatus::State::Finished || status.state == StreamStatus::State::Failed;
    if (ended && !stream_ended_) {
        stream_ended_ = true;
        table.end_of_stream();
        update_transport();
        if (status.state == StreamStatus::State::Failed && !automation_.active())
            QMessageBox::critical(this, tr("KZ4AP Skimmer"), QString::fromStdString(status.error));
    }

    if (ticks_ % kTextEveryTicks == 0 || ended) {
        model_->refresh();
        update_markers();
        update_status(status);
    }
    run_automation(status);
}

void MainWindow::update_markers() {
    std::vector<WaterfallWidget::Marker> markers;
    for (const TrackRow& row : model_->table().rows())
        markers.push_back({row.id, row.freq_hz, row.live(), selected_track_ == row.id});
    waterfall_->set_markers(std::move(markers));
}

void MainWindow::update_status(const StreamStatus& status) {
    const QString stream = total_s_ ? tr("Stream %1 of %2 s").arg(status.stream_s, 0, 'f', 1).arg(*total_s_, 0, 'f', 1)
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
    stream_label_->setText(stream + state);

    const CpuRates recent = meter_.recent();
    QString speed = recent.speed ? tr("Speed %1x").arg(*recent.speed, 0, 'f', 2) : tr("Speed -");
    if (rows_dropped_ > 0) speed += tr(", waterfall skipped %1 rows").arg(rows_dropped_);
    speed_label_->setText(speed);

    tracks_label_->setText(tr("Live tracks %1").arg(model_->table().live_count()));

    if (recent.pipeline_ms_per_channel_s && recent.decoder_ms_per_channel_s) {
        cpu_label_->setText(tr("CPU: pipeline %1 ms per channel-second, decoder %2 ms per channel-second")
                                .arg(*recent.pipeline_ms_per_channel_s, 0, 'f', 2)
                                .arg(*recent.decoder_ms_per_channel_s, 0, 'f', 2));
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
        } else if (action == QStringLiteral("quit")) {
            if (!automation_.report_path.isEmpty()) write_report(status);
            QApplication::quit();
        }
    }
}

void MainWindow::run_automation(const StreamStatus& status) {
    if (!automation_.active()) return;
    run_transport_steps(status);
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
    if ((quit_now || ended) && !automation_.report_path.isEmpty() && !report_done_) write_report(status);
    if (quit_now) QApplication::exit(failed ? 1 : 0);
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
    out << "live_tracks_at_end: " << model_->table().live_count() << "\n";
    out << "pipeline_cpu_ms_per_channel_s: " << number(whole.pipeline_ms_per_channel_s) << "\n";
    out << "decoder_ms_per_channel_s: " << number(whole.decoder_ms_per_channel_s) << "\n";
    out << "pipeline_cpu_ms_per_channel_s_last_10s: " << number(recent.pipeline_ms_per_channel_s) << "\n";
    out << "process_cpu_s: " << process_cpu_seconds() << "\n";
    out << "pipeline_thread_cpu_s: " << status.thread_cpu_s << "\n";
    out << "rows_skipped: " << rows_dropped_ << "\n";
    out << "error: " << QString::fromStdString(status.error) << "\n";
    for (const QString& line : transport_log_) out << line << "\n";
    const auto write_rows = [&](const std::vector<TrackRow>& rows) {
        for (const TrackRow& row : rows) {
            out << "track " << row.id << " freq_hz " << QString::number(row.freq_hz, 'f', 1) << " wpm "
                << QString::number(row.wpm, 'f', 1) << " start_s " << QString::number(row.start_s, 'f', 2) << " text "
                << QString::fromStdString(row.text()) << "\n";
        }
    };
    write_rows(model_->table().rows());
    write_rows(model_->table().retired());
}

}  // namespace kz4ap::app
