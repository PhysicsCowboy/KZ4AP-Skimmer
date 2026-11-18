#include "settings_store.hpp"

#include "known_call_download.hpp"
#include "known_call_downloader.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace kz4ap::app {

namespace {

// The file's layout; a later version that changes a key's meaning moves this on and reads the old one as it can.
constexpr int kSettingsVersion = 1;

double read_double(const QSettings& s, const QString& key, double fallback) {
    bool ok = false;
    const double v = s.value(key).toDouble(&ok);
    return ok && std::isfinite(v) ? v : fallback;
}

int read_int(const QSettings& s, const QString& key, int fallback) {
    bool ok = false;
    const int v = s.value(key).toInt(&ok);
    return ok ? v : fallback;
}

bool read_bool(const QSettings& s, const QString& key, bool fallback) {
    const QString v = s.value(key).toString();
    if (v == QStringLiteral("true")) return true;
    if (v == QStringLiteral("false")) return false;
    return fallback;
}

}  // namespace

QString user_settings_path() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/settings.ini");
}

SettingsStore::SettingsStore(QString path) : path_(std::move(path)) {}

UserChoices SettingsStore::read_choices() const {
    UserChoices c;
    if (!enabled()) return c;
    const QSettings s(path_, QSettings::IniFormat);
    c.decoder_only = s.value(QStringLiteral("calls/mode")).toString() == QStringLiteral("decoder");
    c.signed_only = s.value(QStringLiteral("calls/list")).toString() == QStringLiteral("signed");
    const QString known = s.value(QStringLiteral("calls/known")).toString();
    if (known == QStringLiteral("file")) {
        c.known = {KnownKind::File, s.value(QStringLiteral("calls/known_file")).toString().toStdString()};
    } else if (known == QStringLiteral("downloaded")) {
        c.known = {KnownKind::Downloaded, {}};
    }
    const QString speed = s.value(QStringLiteral("transport/speed")).toString();
    c.speed = speed == QStringLiteral("max") ? std::numeric_limits<double>::infinity() : read_double(s, QStringLiteral("transport/speed"), c.speed);
    c.radio_serial = s.value(QStringLiteral("radio/serial")).toString().trimmed().toStdString();
    c.center_khz = read_double(s, QStringLiteral("radio/center_khz"), c.center_khz);
    c.sample_rate_hz = read_int(s, QStringLiteral("radio/rate_hz"), c.sample_rate_hz);
    c.if_gain_reduction_db = read_int(s, QStringLiteral("radio/if_gain_reduction_db"), c.if_gain_reduction_db);
    c.lna_state = read_int(s, QStringLiteral("radio/lna_state"), c.lna_state);
    c.auto_floor = read_bool(s, QStringLiteral("waterfall/auto_floor"), c.auto_floor);
    c.floor_dbfs = read_double(s, QStringLiteral("waterfall/floor_dbfs"), c.floor_dbfs);
    c.range_db = read_double(s, QStringLiteral("waterfall/range_db"), c.range_db);
    c.recording_folder = s.value(QStringLiteral("files/recording_folder")).toString().toStdString();
    return sanitized(std::move(c));
}

void SettingsStore::write_choices(const UserChoices& c) const {
    if (!enabled()) return;
    QSettings s(path_, QSettings::IniFormat);
    s.setValue(QStringLiteral("version"), kSettingsVersion);
    s.setValue(QStringLiteral("calls/mode"), c.decoder_only ? QStringLiteral("decoder") : QStringLiteral("skimmer"));
    s.setValue(QStringLiteral("calls/list"), c.signed_only ? QStringLiteral("signed") : QStringLiteral("all"));
    switch (c.known.kind) {
        case KnownKind::None: s.setValue(QStringLiteral("calls/known"), QStringLiteral("none")); break;
        case KnownKind::File: s.setValue(QStringLiteral("calls/known"), QStringLiteral("file")); break;
        case KnownKind::Downloaded: s.setValue(QStringLiteral("calls/known"), QStringLiteral("downloaded")); break;
    }
    if (c.known.kind == KnownKind::File) {
        s.setValue(QStringLiteral("calls/known_file"), QString::fromStdString(c.known.path));
    } else {
        s.remove(QStringLiteral("calls/known_file"));
    }
    s.setValue(QStringLiteral("transport/speed"), std::isinf(c.speed) ? QStringLiteral("max") : QString::number(c.speed));
    s.setValue(QStringLiteral("radio/serial"), QString::fromStdString(c.radio_serial));
    s.setValue(QStringLiteral("radio/center_khz"), QString::number(c.center_khz, 'f', 3));
    s.setValue(QStringLiteral("radio/rate_hz"), c.sample_rate_hz);
    s.setValue(QStringLiteral("radio/if_gain_reduction_db"), c.if_gain_reduction_db);
    s.setValue(QStringLiteral("radio/lna_state"), c.lna_state);
    s.setValue(QStringLiteral("waterfall/auto_floor"), c.auto_floor ? QStringLiteral("true") : QStringLiteral("false"));
    s.setValue(QStringLiteral("waterfall/floor_dbfs"), QString::number(c.floor_dbfs, 'f', 1));
    s.setValue(QStringLiteral("waterfall/range_db"), QString::number(c.range_db, 'f', 1));
    s.setValue(QStringLiteral("files/recording_folder"), QString::fromStdString(c.recording_folder));
}

QByteArray SettingsStore::read_layout(const QString& name) const {
    if (!enabled()) return {};
    const QSettings s(path_, QSettings::IniFormat);
    return s.value(QStringLiteral("window/") + name).toByteArray();
}

void SettingsStore::write_layout(const QString& name, const QByteArray& state) const {
    if (!enabled()) return;
    QSettings s(path_, QSettings::IniFormat);
    s.setValue(QStringLiteral("window/") + name, state);
}

RestoredKnown restore_known_calls(const KnownChoice& choice, const QString& kept_folder) {
    RestoredKnown out;
    if (choice.kind == KnownKind::None) return out;
    const bool downloaded = choice.kind == KnownKind::Downloaded;
    const QString path = downloaded ? kept_folder + QLatin1Char('/') + QString::fromUtf8(kScpFileName.data(), static_cast<qsizetype>(kScpFileName.size()))
                                    : QString::fromStdString(choice.path);
    const QString shown = QDir::toNativeSeparators(path);
    if (!QFileInfo(path).isFile()) {
        out.status = downloaded ? QCoreApplication::translate("settings", "the downloaded MASTER.SCP kept in %1 is gone: no list").arg(QDir::toNativeSeparators(kept_folder))
                                : QCoreApplication::translate("settings", "the saved list %1 no longer exists: no list").arg(shown);
        out.problem = true;
        return out;
    }
    try {
        out.list = read_known_calls(path);
        out.path = path;
        out.status = downloaded ? QCoreApplication::translate("settings", "%1 calls from the downloaded %2 (kept, not fetched again)")
                                      .arg(out.list->size())
                                      .arg(QFileInfo(path).fileName())
                                : QCoreApplication::translate("settings", "%1 calls from %2").arg(out.list->size()).arg(QFileInfo(path).fileName());
    } catch (const std::exception& e) {
        out.list.reset();
        out.status = QCoreApplication::translate("settings", "the saved list was refused (%1): no list").arg(QString::fromUtf8(e.what()));
        out.problem = true;
    }
    return out;
}

}  // namespace kz4ap::app
