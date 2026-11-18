// kz4ap-skimmer: the desktop app. Opens an I/Q recording or an SDRplay radio, shows the band as a waterfall and each signal's
// decoded text, and saves a radio's raw I/Q.
#include "main_window.hpp"
#include "known_call_downloader.hpp"

#include "kz4ap/decoder_name.hpp"
#include "kz4ap/file_iq_source.hpp"
#include "kz4ap/sdrplay_source.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPalette>
#include <QStyleFactory>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

QPalette dark_palette() {
    QPalette p;
    p.setColor(QPalette::Window, QColor(38, 38, 42));
    p.setColor(QPalette::WindowText, QColor(225, 225, 225));
    p.setColor(QPalette::Base, QColor(24, 24, 28));
    p.setColor(QPalette::AlternateBase, QColor(38, 38, 42));
    p.setColor(QPalette::Text, QColor(235, 235, 235));
    p.setColor(QPalette::Button, QColor(52, 52, 58));
    p.setColor(QPalette::ButtonText, QColor(225, 225, 225));
    p.setColor(QPalette::Highlight, QColor(60, 100, 170));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, QColor(52, 52, 58));
    p.setColor(QPalette::ToolTipText, QColor(235, 235, 235));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(130, 130, 130));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(130, 130, 130));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(130, 130, 130));
    return p;
}

// The transport's value for a --speed option: a positive number, or "max"; none for anything else.
std::optional<double> parse_speed(const QString& text) {
    if (text == QStringLiteral("max")) return kz4ap::kAsFastAsPossible;
    bool ok = false;
    const double speed = text.toDouble(&ok);
    if (ok && speed > 0) return speed;
    return std::nullopt;
}

int usage_error(const QString& message, bool quiet) {
    std::fprintf(stderr, "kz4ap-skimmer: %s\n", message.toLocal8Bit().constData());
    if (!quiet) QMessageBox::critical(nullptr, QStringLiteral("KZ4AP Skimmer"), message);
    return 2;
}

// Whether what is written to the stream goes anywhere: a Windows program started with no console and no redirection (from
// Explorer, or from cmd.exe, which does not wait for it) has none.
bool stream_connected(std::FILE* stream) {
#if defined(_WIN32)
    return _fileno(stream) >= 0;
#else
    (void)stream;
    return true;
#endif
}

// --list-sdrplay: each radio's serial number and model, a line each, from the API's list alone: no radio is taken or
// started. To standard output (an error to standard error), and in a window too when that goes nowhere.
int list_radios() {
    QString text;
    int code = 0;
    try {
        const std::vector<kz4ap::SdrplayDevice> devices = kz4ap::list_sdrplay_devices();
        if (devices.empty()) text = QStringLiteral("No SDRplay radio found.\n");
        for (const kz4ap::SdrplayDevice& device : devices)
            text += QStringLiteral("%1\t%2\n").arg(QString::fromStdString(device.serial), QString::fromStdString(device.model));
    } catch (const std::exception& e) {
        text = QString::fromUtf8(e.what()) + QLatin1Char('\n');
        code = 1;
    }
    std::FILE* out = code == 0 ? stdout : stderr;
    std::fputs(text.toLocal8Bit().constData(), out);
    std::fflush(out);
    if (!stream_connected(out)) {
        const QString title = QStringLiteral("KZ4AP Skimmer");
        if (code == 0) QMessageBox::information(nullptr, title, QStringLiteral("SDRplay radios (serial number, model):\n\n") + text);
        else QMessageBox::critical(nullptr, title, text);
    }
    return code;
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("kz4ap-skimmer"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(dark_palette());

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("KZ4AP Skimmer: a waterfall and decoded text from I/Q recordings or an SDRplay radio.\n\n"
                       "Settings kept between runs: the mode, the list's filter, the known-call list (your file, or the downloaded "
                       "MASTER.SCP, read from disk only), the speed, the radio's serial, center, rate and gains, the waterfall's scale, "
                       "the window's size and layout, and the last recording folder, in %1. An option given here sets its choice for "
                       "this run only and is not saved: the saved value stays unless you change that control during the run. "
                       "--no-settings reads and writes none; so do runs without an operator (--screenshot, --report, --decoded, --calls, "
                       "--quit-at-end, --quit-at, --transport) and offscreen runs (-platform offscreen), unless --settings names a file.")
            .arg(QDir::toNativeSeparators(kz4ap::app::user_settings_path())));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("recording"),
                                 QStringLiteral("An I/Q recording to open (stereo WAV: 16-, 24- or 32-bit integer, or 32-bit float)."),
                                 QStringLiteral("[recording]"));
    const QCommandLineOption decoder_option(
        QStringLiteral("decoder"), QStringLiteral("The decoder: envelope, matched, bank, viterbi or segmental."), QStringLiteral("name"),
        QString::fromUtf8(kz4ap::kDefaultDecoderName.data(), static_cast<qsizetype>(kz4ap::kDefaultDecoderName.size())));
    const QCommandLineOption speed_option(QStringLiteral("speed"),
                                          QStringLiteral("Stream seconds per second of wall clock, or max (default: the saved speed, else 1)."),
                                          QStringLiteral("x"));
    const QCommandLineOption no_settings_option(QStringLiteral("no-settings"),
                                                QStringLiteral("Read no saved settings and save none: every choice starts at the program's default."));
    const QCommandLineOption settings_option(
        QStringLiteral("settings"),
        QStringLiteral("Keep the settings in this INI file instead of your own, in any run (also without an operator or offscreen)."),
        QStringLiteral("path"));
    const QCommandLineOption screenshot_option(QStringLiteral("screenshot"),
                                               QStringLiteral("Save a picture of the window to this file."),
                                               QStringLiteral("path"));
    const QCommandLineOption screenshot_at_option(QStringLiteral("screenshot-at"),
                                                  QStringLiteral("Stream time of the picture, s (default: the end)."),
                                                  QStringLiteral("s"));
    const QCommandLineOption report_option(QStringLiteral("report"),
                                           QStringLiteral("Write the run's numbers and every track's text to this file."),
                                           QStringLiteral("path"));
    const QCommandLineOption quit_at_end_option(QStringLiteral("quit-at-end"), QStringLiteral("Quit when the stream ends."));
    const QCommandLineOption quit_at_option(QStringLiteral("quit-at"), QStringLiteral("Quit at this stream time, s."),
                                            QStringLiteral("s"));
    const QCommandLineOption transport_option(
        QStringLiteral("transport"),
        QStringLiteral("Press the transport's controls at given times: seconds of wall clock and an action, for example "
                       "8:pause;14:play;20:speed=5;30:restart."),
        QStringLiteral("steps"));
    const int half_the_machine = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) / 2);
    const QCommandLineOption threads_option(QStringLiteral("threads"),
                                            QStringLiteral("Threads for the channels (default: half the machine's)."),
                                            QStringLiteral("n"), QString::number(half_the_machine));
    const QCommandLineOption decoded_option(QStringLiteral("decoded"),
                                            QStringLiteral("Write the tracks' text to this file, for kz4ap-bench --score-decoded."),
                                            QStringLiteral("path"));
    const QCommandLineOption calls_option(QStringLiteral("calls"),
                                          QStringLiteral("Write the call list to this file (CSV), for training/kz4ap_proto/calls_score.py."),
                                          QStringLiteral("path"));
    const QCommandLineOption known_calls_option(QStringLiteral("known-calls"),
                                                QStringLiteral("A known-call list in MASTER.SCP's format: one call a line. The call list works without one."),
                                                QStringLiteral("path"));
    const QCommandLineOption decoder_only_option(
        QStringLiteral("decoder-only"),
        QStringLiteral("Start as a plain CW decoder: the waterfall and the decoded text, with no call search, call list or known-call "
                       "list (the Mode box's Decoder only). Not with --known-calls or --signed-only; --calls then writes the list as "
                       "it stands at the end (the header alone if still in decoder-only mode)."));
    const QCommandLineOption signed_only_option(
        QStringLiteral("signed-only"),
        QStringLiteral("The call list shows only the calls sent at least once after DE, CQ or TEST, or before K, KN or AR (the List "
                       "box's Signed calls only); by default it shows every call."));
    const QCommandLineOption radio_option(QStringLiteral("radio"), QStringLiteral("Start the SDRplay radio at once."));
    const QCommandLineOption serial_option(
        QStringLiteral("sdrplay-serial"),
        QStringLiteral("The serial number of the SDRplay radio to open: only the radio with exactly this one is opened, and with "
                       "none given, none is (--list-sdrplay lists them)."),
        QStringLiteral("SERIAL"));
    const QCommandLineOption list_option(QStringLiteral("list-sdrplay"),
                                         QStringLiteral("List the SDRplay radios' serial numbers and models, opening none, and quit."));
    const QCommandLineOption center_option(
        QStringLiteral("center-khz"),
        QStringLiteral("The radio's center frequency, kHz (default: the saved one, else 7031.5). With a recording, it overrides what the "
                       "recording's header or file name says."),
        QStringLiteral("kHz"));
    const QCommandLineOption rate_option(
        QStringLiteral("rate"), QStringLiteral("The radio's sample rate: 96000, 192000, 384000 or 768000 Hz (default: the saved one, else 192000)."),
        QStringLiteral("Hz"));
    const QCommandLineOption gain_option(QStringLiteral("gain-reduction"),
                                         QStringLiteral("The radio's IF gain reduction, 20 to 59 dB (default: the saved one, else 40)."),
                                         QStringLiteral("dB"));
    const QCommandLineOption lna_option(QStringLiteral("lna-state"),
                                        QStringLiteral("The radio's LNA state, 0 (the most gain) to 9 (default: the saved one, else 0)."),
                                        QStringLiteral("n"));
    const QCommandLineOption mirror_option(
        QStringLiteral("mirror"),
        QStringLiteral("The recording's spectrum is mirrored (an SDR on a transceiver's IF output): conjugate its samples, so a "
                       "signal's true frequency is the center minus the file's offset."));
    parser.addOption(mirror_option);
    parser.addOptions({decoder_option, speed_option, screenshot_option, screenshot_at_option, report_option,
                       quit_at_end_option, quit_at_option, transport_option, threads_option, decoded_option, calls_option, known_calls_option,
                       decoder_only_option, signed_only_option, radio_option,
                       serial_option, list_option, center_option, rate_option, gain_option, lna_option, no_settings_option, settings_option});
    parser.process(app);

    if (parser.isSet(list_option)) return list_radios();

    kz4ap::app::Automation automation;
    automation.decoded_path = parser.value(decoded_option);
    automation.calls_path = parser.value(calls_option);
    automation.screenshot_path = parser.value(screenshot_option);
    automation.screenshot_at_s =
        parser.isSet(screenshot_at_option) ? parser.value(screenshot_at_option).toDouble() : 1e300;
    automation.report_path = parser.value(report_option);
    automation.quit_at_end = parser.isSet(quit_at_end_option);
    automation.mirror = parser.isSet(mirror_option);
    if (parser.isSet(quit_at_option)) automation.quit_at_s = parser.value(quit_at_option).toDouble();
    for (const QString& step : parser.value(transport_option).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const qsizetype colon = step.indexOf(QLatin1Char(':'));
        automation.transport.push_back({step.left(colon).toDouble(), step.mid(colon + 1)});
    }
    const bool quiet = automation.active();

    const std::string decoder = parser.value(decoder_option).toStdString();
    if (!kz4ap::front_end_from_name(decoder))
        return usage_error(QStringLiteral("unknown decoder: %1").arg(parser.value(decoder_option)), quiet);
    kz4ap::app::GivenChoices given;
    if (parser.isSet(speed_option)) {
        given.speed = parse_speed(parser.value(speed_option));
        if (!given.speed) return usage_error(QStringLiteral("bad speed: %1").arg(parser.value(speed_option)), quiet);
    }

    bool threads_ok = false;
    const int threads = parser.value(threads_option).toInt(&threads_ok);
    if (!threads_ok || threads < 1) return usage_error(QStringLiteral("bad thread count: %1").arg(parser.value(threads_option)), quiet);

    if (parser.isSet(serial_option)) given.radio_serial = parser.value(serial_option).trimmed().toStdString();
    if (parser.isSet(center_option)) {
        // The Center box's range, kHz; its 3 decimals (1 Hz) are what it keeps.
        const double center_khz = std::round(parser.value(center_option).toDouble() * 1000.0) / 1000.0;
        if (!(center_khz >= 1.0 && center_khz <= 2000000.0))
            return usage_error(QStringLiteral("the center frequency must be 1 to 2 000 000 kHz, not %1 kHz").arg(parser.value(center_option)), quiet);
        given.center_khz = center_khz;
        automation.recording_center_hz = parser.value(center_option).toDouble() * 1000.0;  // a recording's, as given
    }
    if (parser.isSet(rate_option)) {
        const int rate = parser.value(rate_option).toInt();
        if (rate != 96000 && rate != 192000 && rate != 384000 && rate != 768000)
            return usage_error(QStringLiteral("the radio's sample rate must be 96000, 192000, 384000 or 768000 Hz, not %1").arg(parser.value(rate_option)), quiet);
        given.sample_rate_hz = rate;
    }
    // Refused outside what the boxes take, rather than clamped by them: a clamped value would differ from the option's, and so be saved.
    if (parser.isSet(gain_option)) {
        bool ok = false;
        const int db = parser.value(gain_option).toInt(&ok);
        if (!ok || db < 20 || db > 59)
            return usage_error(QStringLiteral("the IF gain reduction must be a whole number from 20 to 59 dB, not %1").arg(parser.value(gain_option)), quiet);
        given.if_gain_reduction_db = db;
    }
    if (parser.isSet(lna_option)) {
        bool ok = false;
        const int state = parser.value(lna_option).toInt(&ok);
        if (!ok || state < 0 || state > 9)
            return usage_error(QStringLiteral("the LNA state must be a whole number from 0 to 9, not %1").arg(parser.value(lna_option)), quiet);
        given.lna_state = state;
    }

    // The options set the start. --known-calls and --signed-only configure a call list that decoder-only mode does not have, so they
    // are refused with it, and without it they start in skimmer mode whatever was saved; a transport step (mode=skimmer, then
    // known=PATH, list=signed) sets them later. --calls is an output, written at the end as the list then stands: the header alone if the
    // run ends in decoder-only mode.
    const bool decoder_only = parser.isSet(decoder_only_option);
    if (decoder_only && (parser.isSet(known_calls_option) || parser.isSet(signed_only_option)))
        return usage_error(QStringLiteral("--decoder-only runs no call search: it takes no --known-calls or --signed-only"), quiet);
    if (decoder_only) given.decoder_only = true;
    if (parser.isSet(known_calls_option) || parser.isSet(signed_only_option)) given.decoder_only = false;
    if (parser.isSet(signed_only_option)) given.signed_only = true;

    kz4ap::app::Startup startup;
    if (parser.isSet(known_calls_option)) {
        try {
            const QString path = parser.value(known_calls_option);
            startup.given_known = kz4ap::app::read_known_calls(path);
            // As given: it is saved only if the user changes the list during the run, and then as the new one.
            given.known = kz4ap::app::KnownChoice{kz4ap::app::KnownKind::File, path.toStdString()};
            std::fprintf(stderr, "kz4ap-skimmer: known-call list: %zu calls from %s\n", startup.given_known->size(),
                         QDir::toNativeSeparators(path).toLocal8Bit().constData());
        } catch (const std::exception& e) {
            return usage_error(QString::fromUtf8(e.what()), quiet);
        }
    }

    // Where the settings are kept: none with --no-settings, and none by default in a run without an operator or offscreen, so that
    // scripted runs start from the program's defaults whatever the user has saved; --settings names a file for any run.
    const bool unattended = quiet || QGuiApplication::platformName() == QStringLiteral("offscreen");
    if (parser.isSet(no_settings_option) && parser.isSet(settings_option))
        return usage_error(QStringLiteral("--no-settings and --settings exclude each other"), quiet);
    QString settings_path;
    if (parser.isSet(settings_option)) {
        settings_path = QFileInfo(parser.value(settings_option)).absoluteFilePath();
    } else if (!parser.isSet(no_settings_option) && !unattended) {
        settings_path = kz4ap::app::user_settings_path();
    }
    startup.store = kz4ap::app::SettingsStore(settings_path);
    startup.saved = startup.store.read_choices();
    startup.given = given;
    startup.start = kz4ap::app::start_choices(startup.saved, given);

    kz4ap::app::MainWindow window(decoder, threads, automation, std::move(startup));
    window.show();
    if (parser.isSet(radio_option)) {
        window.start_radio();
    } else if (!parser.positionalArguments().isEmpty()) {
        window.open_recording(parser.positionalArguments().front());
    }
    return QApplication::exec();
}
