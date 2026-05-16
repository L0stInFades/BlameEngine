#include "next/jobsystem/job_system.h"
#include "next/foundation/logger.h"
#include "next/profiler/cpu_scope.h"
#include <chrono>
#include <algorithm>
#include <exception>

namespace Next {

namespace detail {
struct JobState {
    JobTask task;
    JobPriority priority = JobPriority::Normal;
    std::string name;
    std::atomic<uint32_t> remainingDeps{0};
    std::vector<std::weak_ptr<JobState>> dependents;

    std::mutex mutex;
    std::condition_variable cv;

    std::atomic<bool> queued{false};
    std::atomic<bool> started{false};
    std::atomic<bool> completed{false};
    std::atomic<bool> cancelled{false};
};
} // namespace detail

bool JobHandle::IsCompleted() const {
    if (!state) return false;
    return state->completed.load(std::memory_order_acquire);
}

static int PriorityIndex(JobPriority p) {
    switch (p) {
        case JobPriority::High: return 0;
        case JobPriority::Normal: return 1;
        case JobPriority::Low: return 2;
        default: return 1;
    }
}

static const char* JobNameOrFallback(const std::shared_ptr<detail::JobState>& job) {
    return job && !job->name.empty() ? job->name.c_str() : "<unnamed>";
}

static void LogJobException(const std::shared_ptr<detail::JobState>& job, const char* reason) {
    NEXT_LOG_ERROR("JobSystem job '%s' threw an exception: %s",
                   JobNameOrFallback(job),
                   reason ? reason : "unknown");
}

JobSystem& JobSystem::Instance() {
    static JobSystem instance;
    return instance;
}

bool JobSystem::Initialize(uint32_t workerCount) {
    if (running_) {
        NEXT_LOG_WARNING("JobSystem already initialized");
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        for (auto& queue : queues_) {
            queue.clear();
        }
    }

    submittedCount_.store(0, std::memory_order_release);
    completedCount_.store(0, std::memory_order_release);
    activeWorkers_.store(0, std::memory_order_release);
    totalDurationUs_.store(0, std::memory_order_release);
    maxDurationUs_.store(0, std::memory_order_release);
    cancelledCount_.store(0, std::memory_order_release);
    failedCount_.store(0, std::memory_order_release);

    if (workerCount == 0) {
        uint32_t hw = std::thread::hardware_concurrency();
        workerCount = hw > 1 ? hw - 1 : 1;
    }

    running_ = true;
    workers_.reserve(workerCount);
    for (uint32_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back([this, i]() { WorkerLoop(i); });
    }

    NEXT_LOG_INFO("JobSystem initialized with %u worker threads", workerCount);
    return true;
}

void JobSystem::Shutdown() {
    if (!running_) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        running_ = false;
    }
    workAvailable_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    // Clear queues
    for (auto& q : queues_) {
        q.clear();
    }

    submittedCount_.store(0, std::memory_order_release);
    completedCount_.store(0, std::memory_order_release);
    activeWorkers_.store(0, std::memory_order_release);
    totalDurationUs_.store(0, std::memory_order_release);
    maxDurationUs_.store(0, std::memory_order_release);
    cancelledCount_.store(0, std::memory_order_release);
    failedCount_.store(0, std::memory_order_release);

    NEXT_LOG_INFO("JobSystem shutdown");
}

JobHandle JobSystem::Submit(JobTask task,
                            JobPriority priority,
                            const std::vector<JobHandle>& dependencies,
                            const char* name) {
    if (!task) {
        return {};
    }

    auto job = std::make_shared<detail::JobState>();
    job->task = std::move(task);
    job->priority = priority;
    if (name) {
        job->name = name;
    }

    uint32_t depCount = 0;
    for (const auto& depHandle : dependencies) {
        if (!depHandle.state) {
            continue;
        }
        auto dep = depHandle.state;
        if (dep->completed.load(std::memory_order_acquire)) {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(dep->mutex);
            dep->dependents.push_back(job);
        }
        ++depCount;
    }
    job->remainingDeps.store(depCount, std::memory_order_release);

    submittedCount_.fetch_add(1, std::memory_order_relaxed);

    if (depCount == 0) {
        EnqueueIfReady(job);
    }

    return {job};
}

void JobSystem::Wait(const JobHandle& handle) {
    auto job = handle.state;
    if (!job) return;
    std::unique_lock<std::mutex> lock(job->mutex);
    job->cv.wait(lock, [&]() { return job->completed.load(std::memory_order_acquire); });
}

bool JobSystem::WaitFor(const JobHandle& handle, uint32_t timeoutMs) {
    auto job = handle.state;
    if (!job) return true;
    std::unique_lock<std::mutex> lock(job->mutex);
    if (timeoutMs == 0) {
        return job->completed.load(std::memory_order_acquire);
    }
    return job->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&]() {
        return job->completed.load(std::memory_order_acquire);
    });
}

void JobSystem::Cancel(const JobHandle& handle) {
    auto job = handle.state;
    if (!job) return;
    if (job->completed.load(std::memory_order_acquire)) {
        return;
    }
    if (job->started.load(std::memory_order_acquire)) {
        return;
    }

    job->cancelled.store(true, std::memory_order_release);

    // Try to remove from queues to avoid work
    bool removedFromQueue = false;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        for (auto& queue : queues_) {
            auto it = std::find(queue.begin(), queue.end(), job);
            if (it != queue.end()) {
                queue.erase(it);
                removedFromQueue = true;
                break;
            }
        }
    }

    if (removedFromQueue || !job->started.load(std::memory_order_acquire)) {
        FinishJob(job, 0, true, false);
    }
}

void JobSystem::Pump(double budgetMs) {
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();

    if (budgetMs >= 0.0 && !workers_.empty()) {
        auto deadline = start + std::chrono::duration<double, std::milli>(budgetMs);
        while (Clock::now() < deadline) {
            bool hasJobs = false;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                hasJobs = !queues_[0].empty() || !queues_[1].empty() || !queues_[2].empty();
            }

            if (!hasJobs) {
                break;
            }

            std::this_thread::yield();
        }
        return;
    }

    while (true) {
        auto job = this->TryPopJob();
        if (!job) {
            break;
        }

        auto jobStart = Clock::now();
        job->started.store(true, std::memory_order_release);
        const bool shouldRun = !job->cancelled.load(std::memory_order_acquire) && job->task;
        bool failed = false;
        if (shouldRun) {
            try {
                NEXT_CPU_SCOPE("JobTask");
                job->task();
            } catch (const std::exception& e) {
                failed = true;
                LogJobException(job, e.what());
            } catch (...) {
                failed = true;
                LogJobException(job, "unknown");
            }
        }
        auto jobEnd = Clock::now();
        auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(jobEnd - jobStart).count();
        this->FinishJob(job, static_cast<uint64_t>(durationUs), !shouldRun, failed);

        if (budgetMs >= 0.0) {
            double elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            if (elapsedMs >= budgetMs) {
                break;
            }
        }
    }
}

JobSystemStats JobSystem::GetStats() const {
    JobSystemStats stats;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stats.pendingHigh = queues_[0].size();
        stats.pendingNormal = queues_[1].size();
        stats.pendingLow = queues_[2].size();
    }
    stats.totalSubmitted = submittedCount_.load(std::memory_order_acquire);
    stats.totalCompleted = completedCount_.load(std::memory_order_acquire);
    stats.activeWorkers = activeWorkers_.load(std::memory_order_acquire);
    stats.totalCancelled = cancelledCount_.load(std::memory_order_acquire);
    stats.totalFailed = failedCount_.load(std::memory_order_acquire);

    uint64_t completed = stats.totalCompleted;
    uint64_t totalDuration = totalDurationUs_.load(std::memory_order_acquire);
    if (completed > 0) {
        stats.avgJobMs = static_cast<double>(totalDuration) / static_cast<double>(completed) / 1000.0;
    }
    stats.maxJobMs = static_cast<double>(maxDurationUs_.load(std::memory_order_acquire)) / 1000.0;
    return stats;
}

void JobSystem::WorkerLoop(uint32_t workerIndex) {
    NEXT_LOG_INFO("JobSystem worker %u started", workerIndex);
    while (running_) {
        auto job = PopJobBlocking();
        if (!job) {
            continue;
        }

        activeWorkers_.fetch_add(1, std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        job->started.store(true, std::memory_order_release);
        const bool shouldRun = !job->cancelled.load(std::memory_order_acquire) && job->task;
        bool failed = false;
        if (shouldRun) {
            try {
                NEXT_CPU_SCOPE("JobTask");
                job->task();
            } catch (const std::exception& e) {
                failed = true;
                LogJobException(job, e.what());
            } catch (...) {
                failed = true;
                LogJobException(job, "unknown");
            }
        }
        auto end = std::chrono::steady_clock::now();
        auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        this->FinishJob(job, static_cast<uint64_t>(durationUs), !shouldRun, failed);
        activeWorkers_.fetch_sub(1, std::memory_order_relaxed);
    }
    NEXT_LOG_INFO("JobSystem worker %u exiting", workerIndex);
}

void JobSystem::EnqueueIfReady(const std::shared_ptr<detail::JobState>& job) {
    if (!job) return;
    if (job->completed.load(std::memory_order_acquire)) {
        return;
    }
    if (job->queued.exchange(true, std::memory_order_acq_rel)) {
        return; // already queued
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queues_[PriorityIndex(job->priority)].push_back(job);
    }
    workAvailable_.notify_one();
}

std::shared_ptr<detail::JobState> JobSystem::PopJobBlocking() {
    std::unique_lock<std::mutex> lock(queueMutex_);
    workAvailable_.wait(lock, [&]() {
        if (!running_) return true;
        return !queues_[0].empty() || !queues_[1].empty() || !queues_[2].empty();
    });

    if (!running_) {
        return nullptr;
    }

    for (auto& queue : queues_) {
        if (!queue.empty()) {
            auto job = queue.front();
            queue.pop_front();
            return job;
        }
    }
    return nullptr;
}

std::shared_ptr<detail::JobState> JobSystem::TryPopJob() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    for (auto& queue : queues_) {
        if (!queue.empty()) {
            auto job = queue.front();
            queue.pop_front();
            return job;
        }
    }
    return nullptr;
}

void JobSystem::FinishJob(const std::shared_ptr<detail::JobState>& job,
                          uint64_t durationUs,
                          bool cancelled,
                          bool failed) {
    if (!TryMarkCompleted(job)) {
        return;
    }

    if (!cancelled) {
        totalDurationUs_.fetch_add(durationUs, std::memory_order_relaxed);
        uint64_t prevMax = maxDurationUs_.load(std::memory_order_relaxed);
        while (durationUs > prevMax && !maxDurationUs_.compare_exchange_weak(prevMax, durationUs)) {
            // loop until updated
        }
    } else {
        cancelledCount_.fetch_add(1, std::memory_order_relaxed);
    }
    if (failed) {
        failedCount_.fetch_add(1, std::memory_order_relaxed);
    }

    completedCount_.fetch_add(1, std::memory_order_release);

    // Wake dependents
    std::vector<std::weak_ptr<detail::JobState>> dependentsCopy;
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        dependentsCopy = job->dependents;
    }

    for (auto& weakDep : dependentsCopy) {
        if (auto dep = weakDep.lock()) {
            uint32_t remaining = dep->remainingDeps.fetch_sub(1, std::memory_order_acq_rel);
            if (remaining == 1) { // becomes zero after decrement
                EnqueueIfReady(dep);
            }
        }
    }

    job->cv.notify_all();
}

bool JobSystem::TryMarkCompleted(const std::shared_ptr<detail::JobState>& job) {
    bool expected = false;
    return job->completed.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
}

} // namespace Next
