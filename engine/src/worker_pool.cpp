#include "worker_pool.hpp"

#include "kz4ap/cpu_time.hpp"

#include <stdexcept>

namespace kz4ap {

WorkerPool::WorkerPool(int threads) {
    if (threads < 1) throw std::invalid_argument("worker pool: at least 1 thread");
    const auto count = static_cast<std::size_t>(threads - 1);
    cpu_s_ = std::make_unique<std::atomic<double>[]>(count);
    workers_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        cpu_s_[i].store(0.0);
        workers_.emplace_back([this, i] {
            std::uint64_t seen = 0;
            for (;;) {
                {
                    std::unique_lock lock(mutex_);
                    start_.wait(lock, [&] { return stopping_ || batch_ != seen; });
                    if (stopping_) return;
                    seen = batch_;
                }
                drain();
                cpu_s_[i].store(thread_cpu_seconds(), std::memory_order_relaxed);
                std::lock_guard lock(mutex_);
                if (--running_ == 0) finished_.notify_one();
            }
        });
    }
}

WorkerPool::~WorkerPool() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    start_.notify_all();
    for (std::thread& worker : workers_) worker.join();
}

void WorkerPool::drain() {
    for (;;) {
        const std::size_t index = next_.fetch_add(1, std::memory_order_relaxed);
        if (index >= count_) return;
        try {
            (*job_)(index);
        } catch (...) {
            std::lock_guard lock(failure_mutex_);
            if (!failure_ || index < failed_index_) {
                failure_ = std::current_exception();
                failed_index_ = index;
            }
        }
    }
}

void WorkerPool::run(std::size_t count, const std::function<void(std::size_t)>& job) {
    if (count == 0) return;
    job_ = &job;
    count_ = count;
    next_.store(0, std::memory_order_relaxed);
    failure_ = nullptr;
    if (!workers_.empty() && count > 1) {
        {
            std::lock_guard lock(mutex_);
            ++batch_;
            running_ = static_cast<int>(workers_.size());
        }
        start_.notify_all();
        drain();
        std::unique_lock lock(mutex_);
        finished_.wait(lock, [&] { return running_ == 0; });
    } else {
        drain();
    }
    if (failure_) std::rethrow_exception(failure_);
}

double WorkerPool::worker_cpu_seconds() const {
    double total = 0.0;
    for (std::size_t i = 0; i < workers_.size(); ++i) total += cpu_s_[i].load(std::memory_order_relaxed);
    return total;
}

}  // namespace kz4ap
