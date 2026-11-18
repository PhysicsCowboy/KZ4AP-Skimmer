// The choices the app keeps between runs, free of Qt: what they are, which the command line overrides, and what is saved at the end.
// The store itself (QSettings, an INI file per user) is app/src/settings_store.hpp; the rules are in the README, "Settings kept between
// runs": an option given on the command line sets its choice for that run only, and the saved value stays as it was unless the user
// changes that control during the run.
#pragma once

#include <optional>
#include <string>

namespace kz4ap::app {

// The known-call list to start with: none, the user's own file, or the copy of MASTER.SCP the Download button keeps.
enum class KnownKind { None, File, Downloaded };

struct KnownChoice {
    KnownKind kind = KnownKind::None;
    std::string path;  // UTF-8, the user's file (KnownKind::File only)

    bool operator==(const KnownChoice&) const = default;
};

// What the user chose in the controls. Each default is the program's value without saved settings (the controls' first values before).
struct UserChoices {
    // Calls toolbar.
    bool decoder_only = false;  // Mode: Decoder only
    bool signed_only = false;   // List: Signed calls only
    KnownChoice known;
    // Transport: stream seconds per second of wall clock, 1, 2, 5, 10, or kAsFastAsPossible (Max; infinity).
    double speed = 1.0;
    // Radio toolbar.
    std::string radio_serial;
    double center_khz = 7031.5;    // kHz
    int sample_rate_hz = 192000;   // Hz: 96 000, 192 000, 384 000 or 768 000
    int if_gain_reduction_db = 40; // dB, 20 to 59
    int lna_state = 0;             // 0 to 9
    // The waterfall's color scale (DbScale): automatic floor, the floor when set by hand, dBFS in one FFT bin, and the range, dB above it.
    bool auto_floor = true;
    double floor_dbfs = -80.0;     // dBFS
    double range_db = 50.0;        // dB re the floor
    // The folder a recording was last opened from or saved to, UTF-8; empty for none yet.
    std::string recording_folder;

    bool operator==(const UserChoices&) const = default;
};

// The choices the command line gave for this run; none for each option not given.
struct GivenChoices {
    std::optional<bool> decoder_only;
    std::optional<bool> signed_only;
    std::optional<KnownChoice> known;
    std::optional<double> speed;
    std::optional<std::string> radio_serial;
    std::optional<double> center_khz;
    std::optional<int> sample_rate_hz;
    std::optional<int> if_gain_reduction_db;
    std::optional<int> lna_state;
};

// Saved values outside what the controls take (a file edited by hand, a store from another version) go back to their defaults.
UserChoices sanitized(UserChoices choices);

// The run's first choices: what the command line gave, and the saved value for the rest.
UserChoices start_choices(const UserChoices& saved, const GivenChoices& given);

// What to save at the end of the run: the choices as they are now, except that a choice the command line gave keeps its saved value
// unless the user changed it during the run (now differs from start).
UserChoices choices_to_save(const UserChoices& saved, const UserChoices& start, const UserChoices& now, const GivenChoices& given);

}  // namespace kz4ap::app
