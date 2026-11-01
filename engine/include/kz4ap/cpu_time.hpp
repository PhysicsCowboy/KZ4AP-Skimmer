#pragma once

namespace kz4ap {

// CPU time this process has used so far, user plus kernel, s. Resolution is about 16 ms on Windows.
double process_cpu_seconds();

// CPU time the calling thread has used so far, user plus kernel, s (the replay tool's per-channel cost, the stream
// runner's pipeline thread; the removed prototype measured it with time.process_time() in a worker process). Resolution
// is about 16 ms on Windows.
double thread_cpu_seconds();

// The same, for timing short stretches: exact to the microsecond or better where the system counts cycles per thread
// (Windows; on Linux the clock above is already this good), about 0.3 us per call. Windows: it counts in time stamp counter ticks, so
// a core running above its base clock reads a little long.
double thread_cpu_precise_seconds();

}  // namespace kz4ap
