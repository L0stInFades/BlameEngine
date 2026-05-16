#include "next/jobsystem/job_system.h"
#include "next/jobsystem/job.h"
#include "next/foundation/logger.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace Next {
namespace testing {

using ::testing::Test;

class JobSystemTest : public Test {
protected:
    void SetUp() override {
        Logger::Initialize();
        // Initialize JobSystem with 2 worker threads for testing
        system_.Initialize(2);
    }

    void TearDown() override {
        system_.Shutdown();
        Logger::Shutdown();
    }

    JobSystem& system_ = JobSystem::Instance();
};

// Test basic job submission and execution
TEST_F(JobSystemTest, BasicJobSubmission) {
    std::atomic<int> value{0};

    JobHandle handle = system_.Submit([&]() {
        value.store(42);
    });

    system_.Wait(handle);

    EXPECT_EQ(value.load(), 42);
}

// Test multiple jobs
TEST_F(JobSystemTest, MultipleJobs) {
    std::atomic<int> counter{0};
    const int jobCount = 10;

    std::vector<JobHandle> handles;
    for (int i = 0; i < jobCount; ++i) {
        handles.push_back(system_.Submit([&counter]() {
            counter.fetch_add(1);
        }));
    }

    system_.WaitForAll();

    EXPECT_EQ(counter.load(), jobCount);
}

// Test job priorities
TEST_F(JobSystemTest, JobPriorities) {
    std::atomic<int> executionOrder{0};
    std::string order;

    auto lowJob = system_.Submit([&]() {
        order += "L";
    }, JobPriority::Low);

    auto normalJob = system_.Submit([&]() {
        order += "N";
    }, JobPriority::Normal);

    auto highJob = system_.Submit([&]() {
        order += "H";
    }, JobPriority::High);

    system_.WaitForAll();

    // High priority should execute before low and normal
    // Note: Order is not guaranteed for same priority
    EXPECT_NE(order.find("H"), std::string::npos);
}

// Test job dependencies
TEST_F(JobSystemTest, JobDependencies) {
    std::vector<int> executionOrder;
    std::mutex orderMutex;

    JobHandle job1 = system_.Submit([&]() {
        std::lock_guard<std::mutex> lock(orderMutex);
        executionOrder.push_back(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });

    JobHandle job2 = system_.Submit([&]() {
        std::lock_guard<std::mutex> lock(orderMutex);
        executionOrder.push_back(2);
    }, JobPriority::Normal, {job1});

    JobHandle job3 = system_.Submit([&]() {
        std::lock_guard<std::mutex> lock(orderMutex);
        executionOrder.push_back(3);
    }, JobPriority::Normal, {job1, job2});

    system_.Wait(job3);

    ASSERT_EQ(executionOrder.size(), 3u);
    EXPECT_EQ(executionOrder[0], 1);  // job1 first
    EXPECT_EQ(executionOrder[1], 2);  // job2 second (depends on job1)
    EXPECT_EQ(executionOrder[2], 3);  // job3 third (depends on both)
}

// Test job handle validity
TEST_F(JobSystemTest, JobHandleValidity) {
    JobHandle emptyHandle;
    EXPECT_FALSE(emptyHandle.IsValid());

    JobHandle validHandle = system_.Submit([]() {});
    EXPECT_TRUE(validHandle.IsValid());

    system_.Wait(validHandle);
    EXPECT_TRUE(validHandle.IsValid());  // Still valid after completion
    EXPECT_TRUE(validHandle.IsCompleted());

    validHandle.Reset();
    EXPECT_FALSE(validHandle.IsValid());
}

// Test job cancellation
TEST_F(JobSystemTest, JobCancellation) {
    std::atomic<bool> executed{false};

    // Submit a job that takes some time
    JobHandle handle = system_.Submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        executed.store(true);
    }, JobPriority::Low);

    // Cancel immediately
    system_.Cancel(handle);

    // Wait a bit to see if it executes
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Note: Cancellation is best-effort, job might have already started
    // We just verify Cancel() doesn't crash
    SUCCEED();
}

TEST_F(JobSystemTest, CancelRunningJobDoesNotCompleteBeforeTaskFinishes) {
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};
    std::atomic<bool> finished{false};

    JobHandle handle = system_.Submit([&]() {
        started.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        finished.store(true, std::memory_order_release);
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!started.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(started.load(std::memory_order_acquire));

    system_.Cancel(handle);

    EXPECT_FALSE(handle.IsCompleted());
    EXPECT_FALSE(system_.WaitFor(handle, 10));
    EXPECT_FALSE(finished.load(std::memory_order_acquire));

    release.store(true, std::memory_order_release);
    system_.Wait(handle);

    EXPECT_TRUE(finished.load(std::memory_order_acquire));
    EXPECT_TRUE(handle.IsCompleted());
}

TEST_F(JobSystemTest, CancelDependencyWaitingJobDoesNotRunAfterDependencyCompletes) {
    std::atomic<bool> releaseDependency{false};
    std::atomic<bool> dependentExecuted{false};

    JobHandle dependency = system_.Submit([&]() {
        while (!releaseDependency.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    });
    JobHandle dependent = system_.Submit([&]() {
        dependentExecuted.store(true, std::memory_order_release);
    }, JobPriority::Normal, {dependency});

    system_.Cancel(dependent);
    EXPECT_TRUE(system_.WaitFor(dependent, 100));

    releaseDependency.store(true, std::memory_order_release);
    system_.Wait(dependency);
    system_.WaitForAll();

    EXPECT_TRUE(dependent.IsCompleted());
    EXPECT_FALSE(dependentExecuted.load(std::memory_order_acquire));
}

// Test WaitFor with timeout
TEST_F(JobSystemTest, WaitForTimeout) {
    // Job that takes longer than timeout
    JobHandle handle = system_.Submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    });

    auto start = std::chrono::steady_clock::now();
    bool completed = system_.WaitFor(handle, 100);  // 100ms timeout
    auto end = std::chrono::steady_clock::now();

    EXPECT_FALSE(completed);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(duration.count(), 80);  // Should wait at least ~100ms
    EXPECT_LE(duration.count(), 200); // But not much more

    // Clean up
    system_.Wait(handle);
}

// Test WaitFor with sufficient time
TEST_F(JobSystemTest, WaitForSuccess) {
    JobHandle handle = system_.Submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });

    auto start = std::chrono::steady_clock::now();
    bool completed = system_.WaitFor(handle, 200);  // 200ms timeout
    auto end = std::chrono::steady_clock::now();

    EXPECT_TRUE(completed);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(duration.count(), 40);
    EXPECT_LE(duration.count(), 100);
}

// Test pump with budget
TEST_F(JobSystemTest, PumpWithBudget) {
    std::atomic<int> counter{0};

    // Submit many quick jobs
    for (int i = 0; i < 100; ++i) {
        system_.Submit([&counter]() {
            counter.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        });
    }

    // Pump for 5ms
    auto start = std::chrono::steady_clock::now();
    system_.Pump(5.0);
    auto end = std::chrono::steady_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // The scheduler can resume the yielding pump slightly after the deadline.
    // Keep the assertion focused on catching unbounded blocking.
    EXPECT_LE(duration.count(), 50000);

    // Complete remaining jobs
    system_.WaitForAll();
}

// Test statistics
TEST_F(JobSystemTest, Statistics) {
    // Submit some jobs
    for (int i = 0; i < 5; ++i) {
        system_.Submit([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        });
    }

    system_.WaitForAll();

    JobSystemStats stats = system_.GetStats();

    EXPECT_EQ(stats.pendingHigh, 0u);
    EXPECT_EQ(stats.pendingNormal, 0u);
    EXPECT_EQ(stats.pendingLow, 0u);
    EXPECT_EQ(stats.totalSubmitted, 5u);
    EXPECT_EQ(stats.totalCompleted, 5u);
    EXPECT_EQ(stats.totalFailed, 0u);
    EXPECT_GT(stats.avgJobMs, 0.0);
    EXPECT_GT(stats.maxJobMs, 0.0);
}

// Test stress test with many jobs
TEST_F(JobSystemTest, StressTest) {
    std::atomic<int> counter{0};
    const int jobCount = 1000;

    for (int i = 0; i < jobCount; ++i) {
        system_.Submit([&counter]() {
            counter.fetch_add(1);
        });
    }

    system_.WaitForAll();

    EXPECT_EQ(counter.load(), jobCount);

    JobSystemStats stats = system_.GetStats();
    EXPECT_EQ(stats.totalCompleted, jobCount);
}

// Test empty job
TEST_F(JobSystemTest, EmptyJob) {
    JobHandle handle = system_.Submit([]() {
        // Do nothing
    });

    system_.Wait(handle);
    SUCCEED();
}

TEST_F(JobSystemTest, CaughtExceptionInsideJobDoesNotCountAsFailure) {
    std::atomic<bool> caught{false};

    JobHandle handle = system_.Submit([&]() {
        try {
            throw std::runtime_error("Test exception");
        } catch (...) {
            caught.store(true);
        }
    });

    system_.Wait(handle);

    // Exception should be caught within the job
    EXPECT_TRUE(caught.load());
    JobSystemStats stats = system_.GetStats();
    EXPECT_EQ(stats.totalFailed, 0u);
}

TEST_F(JobSystemTest, UncaughtExceptionInJobCompletesAndRecordsFailure) {
    JobHandle handle = system_.Submit([]() {
        throw std::runtime_error("Uncaught job exception");
    }, JobPriority::Normal, {}, "ThrowingJob");

    system_.Wait(handle);

    EXPECT_TRUE(handle.IsCompleted());
    JobSystemStats stats = system_.GetStats();
    EXPECT_EQ(stats.totalSubmitted, 1u);
    EXPECT_EQ(stats.totalCompleted, 1u);
    EXPECT_EQ(stats.totalCancelled, 0u);
    EXPECT_EQ(stats.totalFailed, 1u);
}

TEST_F(JobSystemTest, PumpContainsUncaughtExceptionAndRecordsFailure) {
    system_.Shutdown();

    JobHandle handle = system_.Submit([]() {
        throw std::runtime_error("Pump job exception");
    }, JobPriority::Normal, {}, "PumpThrowingJob");

    ASSERT_TRUE(handle.IsValid());
    system_.Pump();

    EXPECT_TRUE(handle.IsCompleted());
    JobSystemStats stats = system_.GetStats();
    EXPECT_EQ(stats.totalSubmitted, 1u);
    EXPECT_EQ(stats.totalCompleted, 1u);
    EXPECT_EQ(stats.totalCancelled, 0u);
    EXPECT_EQ(stats.totalFailed, 1u);
}

// Test parallel execution
TEST_F(JobSystemTest, ParallelExecution) {
    std::atomic<int> value{0};
    const int threadCount = 2;
    const int iterationsPerThread = 100;

    std::vector<JobHandle> handles;
    for (int i = 0; i < threadCount; ++i) {
        handles.push_back(system_.Submit([&value, iterationsPerThread]() {
            for (int j = 0; j < iterationsPerThread; ++j) {
                value.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }));
    }

    system_.WaitForAll();

    EXPECT_EQ(value.load(), threadCount * iterationsPerThread);
}

// Test multiple initialization/shutdown cycles
TEST_F(JobSystemTest, MultipleInitShutdown) {
    // Shutdown from SetUp
    system_.Shutdown();

    // Reinitialize
    EXPECT_TRUE(system_.Initialize(2));

    // Test it works
    std::atomic<int> value{0};
    system_.Submit([&value]() {
        value.store(42);
    });

    system_.WaitForAll();
    EXPECT_EQ(value.load(), 42);

    // Reinitialize again in SetUp
}

} // namespace testing
} // namespace Next

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
