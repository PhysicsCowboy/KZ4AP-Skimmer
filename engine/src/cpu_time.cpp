#include "kz4ap/cpu_time.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <intrin.h>
#include <windows.h>
#else
#include <ctime>
#endif

namespace kz4ap {

double process_cpu_seconds() {
#if defined(_WIN32)
    FILETIME creation, exit, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return 0.0;
    const auto ticks = [](const FILETIME& f) {
        return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    };
    return static_cast<double>(ticks(kernel) + ticks(user)) * 1e-7;  // FILETIME counts 100 ns
#else
    timespec ts{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) return 0.0;
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
#endif
}

// The calling thread's CPU, s, at a precision good enough to time one channel block.
double thread_cpu_precise_seconds() {
#if defined(_WIN32)
    // QueryThreadCycleTime counts the time stamp counter's ticks while the thread runs (Windows 10 and later), so its rate is
    // that counter's, measured once against the performance counter over 10 ms.
    static const double seconds_per_tick = [] {
        LARGE_INTEGER frequency, start, now;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
        const unsigned long long ticks_start = __rdtsc();
        do {
            QueryPerformanceCounter(&now);
        } while (static_cast<double>(now.QuadPart - start.QuadPart) < 0.01 * static_cast<double>(frequency.QuadPart));
        const unsigned long long ticks = __rdtsc() - ticks_start;
        const double seconds = static_cast<double>(now.QuadPart - start.QuadPart) / static_cast<double>(frequency.QuadPart);
        return seconds / static_cast<double>(ticks);
    }();
    ULONG64 cycles = 0;
    if (!QueryThreadCycleTime(GetCurrentThread(), &cycles)) return 0.0;
    return static_cast<double>(cycles) * seconds_per_tick;
#else
    return thread_cpu_seconds();
#endif
}

double thread_cpu_seconds() {
#if defined(_WIN32)
    FILETIME creation, exit, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user)) return 0.0;
    const auto ticks = [](const FILETIME& f) {
        return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    };
    return static_cast<double>(ticks(kernel) + ticks(user)) * 1e-7;  // FILETIME counts 100 ns
#else
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return 0.0;
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
#endif
}

}  // namespace kz4ap
