#pragma once

#include "kz4ap/cpu_time.hpp"

namespace kz4ap::bench {

// The engine's, since the stream runner needs them too.
using kz4ap::process_cpu_seconds;
using kz4ap::thread_cpu_seconds;
using kz4ap::thread_cpu_precise_seconds;

}  // namespace kz4ap::bench
