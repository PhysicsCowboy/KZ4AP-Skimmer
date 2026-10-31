// kz4ap-skimmer: the desktop app. Opens an I/Q recording or an SDRplay radio, shows the band as a waterfall and each signal's
// decoded text, and saves a radio's raw I/Q.
#include "main_window.hpp"

#include "kz4ap/decoder_name.hpp"
#include "kz4ap/file_iq_source.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QPalette>
#include <QStyleFactory>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>

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

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("kz4ap-skimmer"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(dark_palette());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("KZ4AP Skimmer: a waterfall and decoded text from I/Q recordings or an SDRplay radio."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("recording"), QStringLiteral("An I/Q recording to open (16-bit stereo WAV)."),
                                 QStringLiteral("[recording]"));
    const QCommandLineOption decoder_option(
        QStringLiteral("decoder"), QStringLiteral("The decoder: envelope, matched, bank, viterbi or segmental."), QStringLiteral("name"),
        QString::fromUtf8(kz4ap::kDefaultDecoderName.data(), static_cast<qsizetype>(kz4ap::kDefaultDecoderName.size())));
    const QCommandLineOption speed_option(QStringLiteral("speed"),
                                          QStringLiteral("Stream seconds per second of wall clock, or max."),
                                          QStringLiteral("x"), QStringLiteral("1"));
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
    const QCommandLineOption radio_option(QStringLiteral("radio"), QStringLiteral("Start the SDRplay radio at once."));
    const QCommandLineOption center_option(QStringLiteral("center-khz"), QStringLiteral("The radio's center frequency, kHz."),
                                           QStringLiteral("kHz"), QStringLiteral("7030"));
    const QCommandLineOption rate_option(QStringLiteral("rate"), QStringLiteral("The radio's sample rate: 96000, 192000, 384000 or 768000 Hz."),
                                         QStringLiteral("Hz"), QStringLiteral("192000"));
    const QCommandLineOption gain_option(QStringLiteral("gain-reduction"), QStringLiteral("The radio's IF gain reduction, 20 to 59 dB."),
                                         QStringLiteral("dB"), QStringLiteral("40"));
    const QCommandLineOption lna_option(QStringLiteral("lna-state"), QStringLiteral("The radio's LNA state, 0 (the most gain) to 9."),
                                        QStringLiteral("n"), QStringLiteral("0"));
    parser.addOptions({decoder_option, speed_option, screenshot_option, screenshot_at_option, report_option,
                       quit_at_end_option, quit_at_option, transport_option, threads_option, decoded_option, radio_option,
                       center_option, rate_option, gain_option, lna_option});
    parser.process(app);

    kz4ap::app::Automation automation;
    automation.decoded_path = parser.value(decoded_option);
    automation.screenshot_path = parser.value(screenshot_option);
    automation.screenshot_at_s =
        parser.isSet(screenshot_at_option) ? parser.value(screenshot_at_option).toDouble() : 1e300;
    automation.report_path = parser.value(report_option);
    automation.quit_at_end = parser.isSet(quit_at_end_option);
    if (parser.isSet(quit_at_option)) automation.quit_at_s = parser.value(quit_at_option).toDouble();
    for (const QString& step : parser.value(transport_option).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const qsizetype colon = step.indexOf(QLatin1Char(':'));
        automation.transport.push_back({step.left(colon).toDouble(), step.mid(colon + 1)});
    }
    const bool quiet = automation.active();

    const std::string decoder = parser.value(decoder_option).toStdString();
    if (!kz4ap::front_end_from_name(decoder))
        return usage_error(QStringLiteral("unknown decoder: %1").arg(parser.value(decoder_option)), quiet);
    const std::optional<double> speed = parse_speed(parser.value(speed_option));
    if (!speed) return usage_error(QStringLiteral("bad speed: %1").arg(parser.value(speed_option)), quiet);

    bool threads_ok = false;
    const int threads = parser.value(threads_option).toInt(&threads_ok);
    if (!threads_ok || threads < 1) return usage_error(QStringLiteral("bad thread count: %1").arg(parser.value(threads_option)), quiet);

    kz4ap::app::RadioDefaults radio;
    radio.center_khz = parser.value(center_option).toDouble();
    radio.sample_rate = parser.value(rate_option).toInt();
    radio.if_gain_reduction_db = parser.value(gain_option).toInt();
    radio.lna_state = parser.value(lna_option).toInt();

    kz4ap::app::MainWindow window(decoder, *speed, threads, radio, automation);
    window.show();
    if (parser.isSet(radio_option)) {
        window.start_radio();
    } else if (!parser.positionalArguments().isEmpty()) {
        window.open_recording(parser.positionalArguments().front());
    }
    return QApplication::exec();
}
