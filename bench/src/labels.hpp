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
