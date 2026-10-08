#pragma once

#include <filesystem>
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
