// A live source's stream start: the system clock's time of its first sample, stamped once from its first block of samples.
#pragma once

#include "kz4ap/utc_time.hpp"

#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>

namespace kz4ap {

// One writer (the radio's callback thread), any readers. The stamp is taken at the first block that holds samples, as
// first_sample_unix_ms(arrival, count, rate) (utc_time.hpp: late by the radio's and its driver's buffering, a heuristic few ms to
// a few tens of ms, not measured); an empty block stamps nothing. It is taken once only: a reset of the radio's stream later does
// not stamp again, because stream time counts on from the first sample (so after a reset, or a gap not counted, a character's UTC
// from this start is early by the gap).
class LiveStart {
public:
    // A block of `count` samples at `sample_rate` has arrived; now_unix_ms() gives the system clock (ms since 1970 UTC) and is
    // called only when this block is stamped. True when it stamped.
    template <typename Now>
    bool on_block(std::uint64_t count, int sample_rate, Now now_unix_ms) {
        if (count == 0 || ms_.load(std::memory_order_relaxed) != kNone) return false;
        ms_.store(first_sample_unix_ms(now_unix_ms(), count, sample_rate), std::memory_order_release);
        return true;
    }
    std::optional<std::int64_t> unix_ms() const {
        const std::int64_t ms = ms_.load(std::memory_order_acquire);
        return ms == kNone ? std::nullopt : std::optional<std::int64_t>(ms);
    }

private:
    static constexpr std::int64_t kNone = std::numeric_limits<std::int64_t>::min();
    std::atomic<std::int64_t> ms_{kNone};
};

}  // namespace kz4ap
