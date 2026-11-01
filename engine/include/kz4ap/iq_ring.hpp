// A lock-free ring for one producer and one consumer (design section 4.1, step 1): a driver's thread puts blocks in and
// returns at once, whatever the reader is doing. What does not fit is not written: the caller counts it.
#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kz4ap {

template <class T>
class SpscRing {
public:
    // At least min_capacity items (rounded up to a power of two).
    explicit SpscRing(std::size_t min_capacity)
        : storage_(std::bit_ceil(std::max<std::size_t>(min_capacity, 2))), mask_(storage_.size() - 1) {}

    std::size_t capacity() const { return storage_.size(); }

    // The producer: copies as many items as fit and returns how many.
    std::size_t push(std::span<const T> items) {
        const std::uint64_t head = head_.load(std::memory_order_relaxed);
        const std::uint64_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t room = storage_.size() - static_cast<std::size_t>(head - tail);
        const std::size_t count = std::min(room, items.size());
        const std::size_t first = static_cast<std::size_t>(head) & mask_;
        const std::size_t to_end = std::min(count, storage_.size() - first);
        std::copy_n(items.begin(), to_end, storage_.begin() + static_cast<std::ptrdiff_t>(first));
        std::copy_n(items.begin() + static_cast<std::ptrdiff_t>(to_end), count - to_end, storage_.begin());
        head_.store(head + count, std::memory_order_release);
        return count;
    }

    // The consumer: copies as many items as there are, up to out.size(), and returns how many.
    std::size_t pop(std::span<T> out) {
        const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
        const std::uint64_t head = head_.load(std::memory_order_acquire);
        const std::size_t count = std::min(static_cast<std::size_t>(head - tail), out.size());
        const std::size_t first = static_cast<std::size_t>(tail) & mask_;
        const std::size_t to_end = std::min(count, storage_.size() - first);
        std::copy_n(storage_.begin() + static_cast<std::ptrdiff_t>(first), to_end, out.begin());
        std::copy_n(storage_.begin(), count - to_end, out.begin() + static_cast<std::ptrdiff_t>(to_end));
        tail_.store(tail + count, std::memory_order_release);
        return count;
    }

    // The consumer: forgets everything in the ring and returns how many items that was.
    std::size_t clear() {
        const std::uint64_t head = head_.load(std::memory_order_acquire);
        const std::uint64_t tail = tail_.exchange(head, std::memory_order_acq_rel);
        return static_cast<std::size_t>(head - tail);
    }

    // The consumer: the items waiting.
    std::size_t size() const {
        return static_cast<std::size_t>(head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_relaxed));
    }

private:
    std::vector<T> storage_;
    std::size_t mask_;
    std::atomic<std::uint64_t> head_{0};  // items ever pushed
    char apart_[64] = {};                 // the two counters on separate cache lines: two threads write them
    std::atomic<std::uint64_t> tail_{0};  // items ever popped
};

}  // namespace kz4ap
