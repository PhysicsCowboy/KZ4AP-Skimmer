#pragma once

namespace kz4ap::bench {

// CPU time this process has used so far, user plus kernel, s. Resolution is
// about 16 ms on Windows.
double process_cpu_seconds();

// CPU time the calling thread has used so far, user plus kernel, s (the replay tool's per-channel cost, as
// the prototype's time.process_time() in a worker process). Resolution is about 16 ms on Windows.
double thread_cpu_seconds();

}  // namespace kz4ap::bench
