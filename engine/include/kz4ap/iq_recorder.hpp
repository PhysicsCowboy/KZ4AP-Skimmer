// Saves raw I/Q to the project's WAV form (wav_reader.hpp: 16-bit stereo PCM, left = I, right = Q) as it arrives. The
// producer is a driver's thread, which must not wait for a disk: it puts the samples in a ring of their own, apart from the
// pipeline's, and a thread of the recorder's writes them, so a pipeline that falls behind loses nothing from the recording.
#pragma once

#include "kz4ap/iq_ring.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace kz4ap {

// What a recording has done so far.
struct RecordingStatus {
    bool active = false;
    std::filesystem::path path;
    std::uint64_t samples = 0;  // I/Q samples written
    std::uint64_t dropped = 0;  // lost because the disk could not keep up: a gap in time in the file
    bool full = false;          // stopped at the form's limit of 4 GiB of data
    std::string error;          // stopped by a write that failed
};

class IqRecorder {
public:
    // sample_rate: complex samples/s, written in the file's header. The ring holds kRingS of stream.
    explicit IqRecorder(int sample_rate);
    ~IqRecorder();
    IqRecorder(const IqRecorder&) = delete;
    IqRecorder& operator=(const IqRecorder&) = delete;

    // Starts a new file, replacing one at that path. Throws std::runtime_error if it cannot be created or a recording is
    // already running. Any thread but the producer's.
    void start(const std::filesystem::path& path);
    // Writes what is queued and closes the file; a valid recording is left. Any thread but the producer's.
    void stop();
    RecordingStatus status() const;

    // The producer: one block, I and Q as the driver gives them, returning at once. Nothing happens unless recording.
    void push(const short* xi, const short* xq, std::size_t count);

    // The most of a file's data, bytes: the form's 32-bit sizes, with room for its header.
    static constexpr std::uint64_t kMaxDataBytes = 0xFFFFFF00ull;
    // Heuristic: long enough for a disk's stall; 31 MB at 192 kHz.
    static constexpr double kRingS = 10.0;
    // How often the header is brought up to date, s of stream, so a crash loses no more than this. Heuristic.
    static constexpr double kHeaderEveryS = 1.0;

private:
    using Pair = std::array<short, 2>;
    void write_loop(std::filesystem::path path);

    int sample_rate_;
    SpscRing<Pair> ring_;
    std::array<Pair, 4096> scratch_;  // the producer's
    std::atomic<bool> accepting_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<std::uint64_t> samples_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<bool> full_{false};
    mutable std::mutex mutex_;
    std::filesystem::path path_;
    std::string error_;
    std::thread writer_;
};

}  // namespace kz4ap
