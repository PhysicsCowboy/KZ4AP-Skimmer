#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kz4ap::bench {

struct Transmission {
    std::string text;
    double start_s;
    double end_s;
    // Hz: the sender's carrier offset from the signal's freq_offset_hz (a QSO's transmissions; 0 when the file has none:
    // a signal of one station, or a per-station label of a QSO, whose transmissions are at its own frequency)
    double offset_hz = 0;
};

struct LabeledSignal {
    std::string text;
    double freq_offset_hz;
    double wpm;
    double snr_db;
    double start_s;
    double end_s;
    std::vector<Transmission> transmissions;  // empty in older labels files: the whole text is one transmission
    bool score = true;                        // false: an interferer, left out of every error rate

    // The generator's other fields (training/kz4ap_synth, SignalSpec), which the new decoder's genie runs read
    // (kz4ap-bench --genie). A labels file without one has the generator's default for it: older files, and the
    // per-station labels of a QSO, which write only some of them.
    // Hz/s: the carrier's frequency change from start_s on (freq_offset_hz is its frequency at start_s)
    double drift_hz_per_s = 0.0;
    // Hz: the Rayleigh fading's frequency spread f_D, twice its standard deviation (0: no fading)
    double fading_hz = 0.0;
    // the keying style: a key of kz4ap_synth.keying.STYLES
    std::string keying = "machine";
    // dots: every mark is longer, and every space shorter, by this
    double imbalance_dits = 0.0;
    // WPM: the speed at the end of each sending, if the speed changes (null or absent: it does not)
    std::optional<double> wpm_end;
    // the stations of a QSO label (its "senders"); 0 for a label of one station
    int sender_count = 0;
    // One of a QSO's per-station labels (training/kz4ap_synth.generate.station_labels), which names its QSO
    // (qso_index): a label of one station, but its start_s is the station's first over, not the QSO's start from
    // which the QSO's drift is measured.
    bool per_station = false;
    // Whether the file wrote fading_hz (not null). The per-station labels written before 2026-10-21 did not, so for
    // them the default above, 0, is a guess and not a truth.
    bool fading_hz_written = false;
};

struct Labels {
    int sample_rate = 0;
    double duration_s = 0;
    std::vector<LabeledSignal> signals;
};

// Parses a labels file written by training/kz4ap_synth. Throws std::runtime_error on bad input.
Labels parse_labels(const std::string& json_text);
Labels load_labels(const std::filesystem::path& path);

}  // namespace kz4ap::bench
