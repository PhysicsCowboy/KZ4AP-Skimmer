#pragma once

// A temporary folder that belongs to this test process alone, so that two build trees or worktrees running their tests
// at once, with the same TEMP, never write, read or remove each other's files.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace kz4ap::test {

namespace detail {

// Owns the folder: made on first use, removed with everything in it when the process exits normally.
class ProcessTempDir {
public:
    ProcessTempDir() {
#ifdef _WIN32
        const auto pid = static_cast<unsigned long long>(_getpid());
#else
        const auto pid = static_cast<unsigned long long>(getpid());
#endif
        std::random_device device;
        const auto now = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        const std::uint64_t suffix = ((static_cast<std::uint64_t>(device()) << 32) | device()) ^ now;
        char name[64];
        std::snprintf(name, sizeof name, "kz4ap_test_%llu_%016llx", pid, static_cast<unsigned long long>(suffix));
        path_ = std::filesystem::temp_directory_path() / name;
        std::filesystem::create_directories(path_);
    }
    ProcessTempDir(const ProcessTempDir&) = delete;
    ProcessTempDir& operator=(const ProcessTempDir&) = delete;
    ~ProcessTempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace detail

// This process's own temporary folder (one per process, shared by every test in it).
inline const std::filesystem::path& temp_dir() {
    static const detail::ProcessTempDir dir;
    return dir.path();
}

// A path named `name` inside this process's temporary folder; nothing is created.
inline std::filesystem::path temp_path(const std::filesystem::path& name) { return temp_dir() / name; }

}  // namespace kz4ap::test
