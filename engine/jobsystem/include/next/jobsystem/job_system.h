#pragma once

#include "next/jobsystem/job.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <chrono>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

namespace Next {

namespace detail {
struct JobState;
}

/**
 * @brief Statistics snapshot for job system monitoring
 */
struct JobSystemStats {
    size_t pendingHigh = 0;      ///< Number of high priority jobs waiting
    size_t pendingNormal = 0;    ///< Number of normal priority jobs waiting
    size_t pendingLow = 0;       ///< Number of low priority jobs waiting
    uint64_t totalSubmitted = 0; ///< Total jobs submitted since startup
    uint64_t totalCompleted = 0; ///< Total jobs completed since startup
    uint64_t activeWorkers = 0;  ///< Number of worker threads currently active
    double avgJobMs = 0.0;       ///< Average job execution time in milliseconds
    double maxJobMs = 0.0;       ///< Maximum job execution time in milliseconds
    uint64_t totalCancelled = 0; ///< Total jobs cancelled
    uint64_t totalFailed = 0;    ///< Total jobs that threw an exception
};

/**
 * @brief Thread pool-based job execution system
 *
 * Provides asynchronous task execution with dependency tracking,
 * priority scheduling, and work stealing capabilities.
 *
 * Features:
 * - Multi-threaded job execution with configurable worker count
 * - Job dependencies (jobs wait for dependencies before executing)
 * - Priority levels (High, Normal, Low)
 * - Job cancellation
 * - Main-thread cooperative execution (Pump)
 * - Performance statistics
 *
 * Usage:
 *   JobSystem::Instance().Initialize();
 *   auto job = JobSystem::Instance().Submit([] {
 *       // Do work
 *   });
 *   JobSystem::Instance().Wait(job);
 *   JobSystem::Instance().Shutdown();
 */
class JobSystem {
public:
    /**
     * @brief Get the singleton instance
     * @return Reference to the global JobSystem instance
     */
    static JobSystem& Instance();

    /**
     * @brief Initialize the job system
     *
     * Creates worker threads and initializes internal state.
     * Should be called once during engine startup.
     *
     * @param workerCount Number of worker threads (0 = auto-detect based on CPU cores)
     * @return true if initialization succeeded
     */
    bool Initialize(uint32_t workerCount = 0);

    /**
     * @brief Shutdown the job system
     *
     * Waits for all active jobs to complete and destroys worker threads.
     */
    void Shutdown();

    /**
     * @brief Submit a job for execution
     *
     * Jobs with unmet dependencies will wait until all dependencies complete.
     *
     * @param task Function to execute
     * @param priority Job priority level
     * @param dependencies List of jobs that must complete before this one runs
     * @param name Optional debug name for the job
     * @return Handle to the submitted job
     */
    JobHandle Submit(JobTask task,
                     JobPriority priority = JobPriority::Normal,
                     const std::vector<JobHandle>& dependencies = {},
                     const char* name = nullptr);

    /**
     * @brief Wait indefinitely for a job to complete
     *
     * Blocks the calling thread until the job finishes.
     * The calling thread may assist with job execution while waiting.
     *
     * @param handle Job to wait for
     */
    void Wait(const JobHandle& handle);

    /**
     * @brief Wait for a job with a timeout
     *
     * @param handle Job to wait for
     * @param timeoutMs Maximum time to wait in milliseconds
     * @return true if job completed, false if timeout elapsed
     */
    bool WaitFor(const JobHandle& handle, uint32_t timeoutMs);

    /**
     * @brief Wait for all submitted jobs to complete
     *
     * Blocks until all jobs that were submitted before this call finish.
     */
    void WaitForAll() {
        uint64_t target = submittedCount_.load(std::memory_order_acquire);
        while (completedCount_.load(std::memory_order_acquire) < target) {
            Pump(0.5);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    /**
     * @brief Cancel a pending job
     *
     * If the job is already running or completed, this is a no-op.
     *
     * @param handle Handle to the job to cancel
     */
    void Cancel(const JobHandle& handle);

    /**
     * @brief Execute jobs on the main thread with a time budget
     *
     * Allows the main thread to assist with job execution.
     * Useful for reducing latency by keeping workers fed with work.
     *
     * @param budgetMs Maximum time to spend executing jobs (negative = unlimited)
     */
    void Pump(double budgetMs = -1.0);

    /**
     * @brief Get current job system statistics
     * @return Statistics snapshot
     */
    JobSystemStats GetStats() const;

    /**
     * @brief Check whether the job system has active worker infrastructure.
     */
    bool IsInitialized() const { return running_; }

private:
    JobSystem() = default;
    ~JobSystem() = default;
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void WorkerLoop(uint32_t workerIndex);
    void EnqueueIfReady(const std::shared_ptr<detail::JobState>& job);
    std::shared_ptr<detail::JobState> PopJobBlocking();
    std::shared_ptr<detail::JobState> TryPopJob();
    void FinishJob(const std::shared_ptr<detail::JobState>& job,
                   uint64_t durationUs,
                   bool cancelled,
                   bool failed);
    bool TryMarkCompleted(const std::shared_ptr<detail::JobState>& job);

    bool running_ = false;
    std::vector<std::thread> workers_;

    mutable std::mutex queueMutex_;
    std::condition_variable workAvailable_;
    std::array<std::deque<std::shared_ptr<detail::JobState>>, 3> queues_;

    std::atomic<uint64_t> submittedCount_{0};
    std::atomic<uint64_t> completedCount_{0};
    std::atomic<uint64_t> activeWorkers_{0};
    std::atomic<uint64_t> totalDurationUs_{0};
    std::atomic<uint64_t> maxDurationUs_{0};
    std::atomic<uint64_t> cancelledCount_{0};
    std::atomic<uint64_t> failedCount_{0};
};

} // namespace Next
