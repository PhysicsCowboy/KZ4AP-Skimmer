#pragma once

#include "kz4ap/engine.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kz4ap::bench {

// Writes each recorded channel's stream (kz4ap-bench --record-channels): oracle channels (add_channel) or the
// channels the detector opens (open_track, close_track). DIR/channel-<track id>.c64 holds the channelizer's output
// as complex64 (float32 I then Q, little-endian; numpy dtype "<c8"), FS, and DIR/channels.json each channel's
// label and labeled frequency (oracle) or birth frequency (detector), center, first sample, opening and closing
// times, sample count and the anchor (the detector's frequency for the track) at every change.
class ChannelRecorder {
public:
    // Throws std::runtime_error if dir cannot be created.
    ChannelRecorder(std::filesystem::path dir, double sample_rate_hz);

    // track_id: the oracle channel (label_index + 1); label_freq_hz: its labeled frequency, Hz from the
    // span's center. Throws std::runtime_error if the file cannot be opened.
    void add_channel(std::uint32_t track_id, std::size_t label_index, double label_freq_hz);

    // A channel the detector opened (--record-channels without --oracle): no label; birth_freq_hz is the track's
    // frequency at birth, Hz from the span's center. Throws std::runtime_error if the file cannot be opened.
    void open_track(std::uint32_t track_id, double birth_freq_hz);

    // The track died: its file is closed and its close time recorded; later blocks for it throw.
    void close_track(std::uint32_t track_id);

    // Appends a block. Throws std::runtime_error for an unknown or closed channel, a block that does not follow
    // the last one, a center that moved, or a write error.
    void write(const ChannelBlock& block);

    // Closes the files and writes channels.json (recording and labels: file names, for the record).
    void finish(const std::string& recording, const std::string& labels);

private:
    struct Channel {
        std::optional<std::size_t> label_index;   // oracle channels only
        std::optional<double> label_freq_hz;      // oracle channels only
        std::optional<double> birth_freq_hz;      // detector channels only
        bool closed = false;
        std::vector<std::pair<std::uint64_t, double>> anchors;  // (first sample index, Hz) at every change
        std::optional<double> center_hz;
        std::optional<std::uint64_t> first_index;
        std::uint64_t samples = 0;
        std::string file;
        std::ofstream out;
    };

    std::filesystem::path dir_;
    double rate_;
    std::map<std::uint32_t, Channel> channels_;
};

}  // namespace kz4ap::bench
