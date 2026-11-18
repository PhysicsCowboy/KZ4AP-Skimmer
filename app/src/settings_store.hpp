// The settings kept between runs: an INI file per user (QSettings), read at the start and written when the window closes. With no path
// (--no-settings, and every run without an operator unless --settings names a file) nothing is read and nothing is written. The keys
// and the rules are in the README, "Settings kept between runs"; what the choices are and which the command line overrides,
// core/app_settings.hpp.
#pragma once

#include "app_settings.hpp"

#include "kz4ap/callsign.hpp"

#include <QByteArray>
#include <QString>

#include <memory>

namespace kz4ap::app {

// The user's settings file: settings.ini in the app's per-user configuration folder (QStandardPaths::AppConfigLocation; on Windows
// %LOCALAPPDATA%\kz4ap-skimmer\settings.ini, beside the downloaded MASTER.SCP; on Linux ~/.config/kz4ap-skimmer/settings.ini).
QString user_settings_path();

class SettingsStore {
public:
    // path: the INI file; empty for none (reads the defaults, writes nothing).
    explicit SettingsStore(QString path = {});

    bool enabled() const { return !path_.isEmpty(); }
    const QString& path() const { return path_; }

    // The saved choices, sanitized; the defaults for whatever is missing or unreadable, and all of them without a file.
    UserChoices read_choices() const;
    void write_choices(const UserChoices& choices) const;
    // The window's layout as Qt saves it (geometry, splitters, column headers), by name; empty when none is saved.
    QByteArray read_layout(const QString& name) const;
    void write_layout(const QString& name, const QByteArray& state) const;

private:
    QString path_;
};

// The known-call list a saved choice names, read from disk only, never the network: a downloaded choice reads the copy the Download
// button kept in kept_folder. A file that is gone or refused gives no list and a status that says so (problem), for the controls'
// status line, never a dialog.
struct RestoredKnown {
    std::shared_ptr<const callsign::KnownCalls> list;
    QString path;    // the file read, when list is not null
    QString status;  // what the controls say: the list's size and origin, or why there is none
    bool problem = false;
};
RestoredKnown restore_known_calls(const KnownChoice& choice, const QString& kept_folder);

}  // namespace kz4ap::app
