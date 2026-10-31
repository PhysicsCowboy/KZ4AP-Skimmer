#include "kz4ap/iq_recorder.hpp"

#include "kz4ap/wav_reader.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#if defined(_WIN32)
#include <share.h>
#endif
#include <span>
#include <stdexcept>
#include <vector>

namespace kz4ap {

namespace {

constexpr std::uint32_t kHeaderBytes = 44;

// A little-endian host, as wav_reader.cpp assumes.
void write_header(std::FILE* file, int sample_rate, std::uint64_t data_bytes) {
    const auto u32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, file); };
    const auto u16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, file); };
    std::fseek(file, 0, SEEK_SET);
    std::fwrite("RIFF", 1, 4, file);
    u32(static_cast<std::uint32_t>(data_bytes + kHeaderBytes - 8));
    std::fwrite("WAVEfmt ", 1, 8, file);
    u32(16);
    u16(1);  // PCM
    u16(2);  // I and Q
    u32(static_cast<std::uint32_t>(sample_rate));
    u32(static_cast<std::uint32_t>(sample_rate) * 4);
    u16(4);
    u16(16);
    std::fwrite("data", 1, 4, file);
    u32(static_cast<std::uint32_t>(data_bytes));
}

std::FILE* open_for_writing(const std::filesystem::path& path) {
#if defined(_WIN32)
    return _wfsopen(path.c_str(), L"wb", _SH_DENYWR);  // others may read it as it grows, as on POSIX
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

}  // namespace

IqRecorder::IqRecorder(int sample_rate)
    : sample_rate_(sample_rate), ring_(static_cast<std::size_t>(kRingS * sample_rate)) {
    if (sample_rate <= 0) throw std::invalid_argument("recorder: the sample rate must be positive (samples/s)");
}

IqRecorder::~IqRecorder() { stop(); }

void IqRecorder::start(const std::filesystem::path& path) {
    std::lock_guard lock(mutex_);
    if (writer_.joinable()) {
        if (accepting_.load()) throw std::runtime_error("a recording is already running");
        writer_.join();  // it stopped by itself (the limit, a failed write)
    }
    std::FILE* probe = open_for_writing(path);
    if (probe == nullptr) throw std::runtime_error("cannot create " + path_text(path));
    write_header(probe, sample_rate_, 0);
    std::fclose(probe);
    path_ = path;
    error_.clear();
    samples_ = 0;
    dropped_ = 0;
    full_ = false;
    stop_requested_ = false;
    ring_.clear();
    writer_ = std::thread([this, path] { write_loop(path); });
    accepting_ = true;
}

void IqRecorder::stop() {
    std::lock_guard lock(mutex_);
    accepting_ = false;
    stop_requested_ = true;
    if (writer_.joinable()) writer_.join();
}

RecordingStatus IqRecorder::status() const {
    std::lock_guard lock(mutex_);
    RecordingStatus s;
    s.active = accepting_.load();
    s.path = path_;
    s.samples = samples_.load();
    s.dropped = dropped_.load();
    s.full = full_.load();
    s.error = error_;
    return s;
}

void IqRecorder::push(const short* xi, const short* xq, std::size_t count) {
    if (!accepting_.load(std::memory_order_acquire)) return;
    for (std::size_t done = 0; done < count;) {
        const std::size_t n = std::min(count - done, scratch_.size());
        for (std::size_t i = 0; i < n; ++i) scratch_[i] = Pair{xi[done + i], xq[done + i]};
        const std::size_t taken = ring_.push(std::span<const Pair>(scratch_.data(), n));
        if (taken < n) dropped_.fetch_add(n - taken, std::memory_order_relaxed);
        done += n;
    }
}

void IqRecorder::write_loop(std::filesystem::path path) {
    std::FILE* file = open_for_writing(path);
    const auto give_up = [&](const std::string& message) {
        accepting_ = false;
        // status() holds the mutex only briefly, but stop() holds it across the join, so this must not wait for it.
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (lock.owns_lock()) error_ = message;
    };
    if (file == nullptr) {
        give_up("cannot write " + path_text(path));
        return;
    }
    write_header(file, sample_rate_, 0);
    std::vector<Pair> chunk(65536);
    std::uint64_t data_bytes = 0;
    std::uint64_t since_header = 0;
    const auto header_every = static_cast<std::uint64_t>(kHeaderEveryS * sample_rate_);
    const auto bring_header_up_to_date = [&] {
        write_header(file, sample_rate_, data_bytes);
        std::fseek(file, 0, SEEK_END);
        std::fflush(file);
    };
    for (;;) {
        const bool stopping = stop_requested_.load();  // before the read: what came before the stop gets written
        const std::size_t got = ring_.pop(chunk);
        if (got == 0) {
            if (stopping) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        std::size_t take = got;
        const std::uint64_t room = (kMaxDataBytes - data_bytes) / sizeof(Pair);
        if (take >= room) {
            take = static_cast<std::size_t>(room);
            full_ = true;
        }
        if (std::fwrite(chunk.data(), sizeof(Pair), take, file) != take) {
            give_up("the write to " + path_text(path) + " failed (is the disk full?)");
            break;
        }
        data_bytes += take * sizeof(Pair);
        samples_.fetch_add(take, std::memory_order_relaxed);
        since_header += take;
        if (full_.load()) {
            accepting_ = false;
            break;
        }
        if (since_header >= header_every) {
            since_header = 0;
            bring_header_up_to_date();
        }
    }
    bring_header_up_to_date();
    std::fclose(file);
}

}  // namespace kz4ap
