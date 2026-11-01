// The channels' worker pool (design section 4.1, step 4): runs a batch of independent jobs and returns when all are done.
// The caller takes part, so `threads` counts it. A job's index says nothing about the thread it runs on, so determinism
// rests on the jobs: each must touch only its own state and what no job writes.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace kz4ap {

class WorkerPool {
public:
    // threads: at least 1, the caller included. Throws std::invalid_argument otherwise.
    explicit WorkerPool(int threads);
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    int threads() const { return static_cast<int>(workers_.size()) + 1; }

    // Calls job(0) ... job(count - 1), each once, on any of the threads, and returns when all have returned. Called from
    // one thread at a time. If jobs throw, every job still runs and the exception of the lowest index is rethrown, so
    // that which error is reported does not depend on the threads' timing.
    void run(std::size_t count, const std::function<void(std::size_t)>& job);

    // CPU the pool's own threads have used, user plus kernel, s, as of the end of their last batch. The caller's own CPU
    // is not in it: it is the caller's thread's.
    double worker_cpu_seconds() const;

private:
    void drain();

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable start_;
    std::condition_variable finished_;
    std::uint64_t batch_ = 0;  // under mutex_
    int running_ = 0;          // workers in the current batch, under mutex_
    bool stopping_ = false;    // under mutex_
    const std::function<void(std::size_t)>* job_ = nullptr;
    std::size_t count_ = 0;
    std::atomic<std::size_t> next_{0};
    std::mutex failure_mutex_;
    std::size_t failed_index_ = 0;  // under failure_mutex_
    std::exception_ptr failure_;    // under failure_mutex_
    std::unique_ptr<std::atomic<double>[]> cpu_s_;  // one per worker
};

}  // namespace kz4ap
