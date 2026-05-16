#pragma once

#include "next/streaming/world_partition.h"
#include <cstdint>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <unordered_map>

// IMPORTANT: Do not include <windows.h> in public headers.
// It pollutes the global macro namespace (e.g. CreateWindow) and breaks engine code.
// Windows APIs are included and used only from the .cpp translation unit.

namespace Next {
namespace Streaming {

// ===== Compression Algorithms =====

enum class CompressionType : uint32_t {
    None = 0,        // No compression
    Zstd = 1,        // Zstandard (fast, good compression)
    LZ4 = 2,         // LZ4 (extremely fast, decent compression)
    Draco = 3,       // Draco for mesh geometry
    Custom = 4       // Custom compression
};

// ===== IO Operation Type =====

enum class IOOperationType : uint32_t {
    Read = 0,        // Read from disk
    Write = 1,       // Write to disk (for saves/caching)
    Decompress = 2,  // Decompress data
    UploadGPU = 3    // Upload to GPU (copy queue)
};

// ===== IO Request =====

struct IORequest {
    uint64_t requestId;
    IOOperationType type;

    // File information
    std::wstring filePath;
    uint64_t offset;
    uint64_t size;

    // Data buffers
    void* outputBuffer;
    uint64_t outputSize;
    const void* inputData;
    uint64_t inputSize;

    // Compression
    CompressionType compressionType;
    uint64_t compressedSize;
    uint64_t decompressedSize;

    // Priority (0 = highest, higher = lower)
    uint32_t priority;

    // Callback
    std::function<void(bool success, uint64_t bytesProcessed)> callback;

    // User data
    void* userData;

    // Internal state
    uint64_t asyncHandle;

    IORequest()
        : requestId(0)
        , type(IOOperationType::Read)
        , offset(0)
        , size(0)
        , outputBuffer(nullptr)
        , outputSize(0)
        , inputData(nullptr)
        , inputSize(0)
        , compressionType(CompressionType::None)
        , compressedSize(0)
        , decompressedSize(0)
        , priority(0)
        , userData(nullptr)
        , asyncHandle(0)
    {}
};

// ===== IO Statistics =====

struct IOStatistics {
    // Throughput
    uint64_t totalBytesRead;
    uint64_t totalBytesWritten;
    uint64_t totalBytesDecompressed;

    // Performance
    float averageReadSpeedMBps;      // Megabytes per second
    float averageWriteSpeedMBps;
    float averageDecompressSpeedMBps;

    // Outstanding operation counts (queued, in-flight, or waiting for main-thread callback dispatch).
    uint32_t pendingReads;
    uint32_t pendingWrites;
    uint32_t pendingDecompressions;

    // Timing
    float averageReadTime;
    float averageWriteTime;
    float averageDecompressTime;

    // Errors
    uint32_t failedOperations;

    IOStatistics()
        : totalBytesRead(0)
        , totalBytesWritten(0)
        , totalBytesDecompressed(0)
        , averageReadSpeedMBps(0.0f)
        , averageWriteSpeedMBps(0.0f)
        , averageDecompressSpeedMBps(0.0f)
        , pendingReads(0)
        , pendingWrites(0)
        , pendingDecompressions(0)
        , averageReadTime(0.0f)
        , averageWriteTime(0.0f)
        , averageDecompressTime(0.0f)
        , failedOperations(0)
    {}

    uint64_t TotalBytesProcessed() const {
        return totalBytesRead + totalBytesWritten + totalBytesDecompressed;
    }
    uint64_t PendingOperationCount() const {
        return static_cast<uint64_t>(pendingReads) +
               static_cast<uint64_t>(pendingWrites) +
               static_cast<uint64_t>(pendingDecompressions);
    }
    bool HasReadBytes() const { return totalBytesRead != 0; }
    bool HasWrittenBytes() const { return totalBytesWritten != 0; }
    bool HasDecompressedBytes() const { return totalBytesDecompressed != 0; }
    bool HasProcessedBytes() const { return TotalBytesProcessed() != 0; }
    bool HasPendingReads() const { return pendingReads != 0; }
    bool HasPendingWrites() const { return pendingWrites != 0; }
    bool HasPendingDecompressions() const { return pendingDecompressions != 0; }
    bool HasPendingOperations() const { return PendingOperationCount() != 0; }
    bool HasReadThroughput() const { return averageReadSpeedMBps != 0.0f; }
    bool HasWriteThroughput() const { return averageWriteSpeedMBps != 0.0f; }
    bool HasDecompressThroughput() const { return averageDecompressSpeedMBps != 0.0f; }
    bool HasThroughput() const {
        return HasReadThroughput() || HasWriteThroughput() || HasDecompressThroughput();
    }
    bool HasReadTiming() const { return averageReadTime != 0.0f; }
    bool HasWriteTiming() const { return averageWriteTime != 0.0f; }
    bool HasDecompressTiming() const { return averageDecompressTime != 0.0f; }
    bool HasTiming() const { return HasReadTiming() || HasWriteTiming() || HasDecompressTiming(); }
    bool HasFailures() const { return failedOperations != 0; }
};

// ===== Async IO Configuration =====

struct AsyncIOConfig {
    // Thread counts
    uint32_t ioThreads = 2;           // Number of IO threads (for IOCP)
    uint32_t decompressionThreads = 2; // Number of decompression threads

    // Queue sizes
    uint32_t maxPendingRequests = 256;
    uint32_t maxConcurrentReads = 32; // Maximum outstanding read requests

    // Buffer sizes
    uint32_t readBufferSize = 1024 * 1024;  // 1MB default buffer
    uint32_t compressionBufferSize = 4 * 1024 * 1024;  // 4MB for decompression

    // Compression settings
    CompressionType defaultCompression = CompressionType::Zstd;
    int32_t compressionLevel = 3;     // Zstd compression level (1-19, default=3)

    // DirectStorage support (Windows only)
    bool enableDirectStorage = false; // Requires Windows 10 Build 20348+
    bool enableBatching = true;       // Batch multiple reads together

    // Performance tuning
    bool prioritizeStreaming = true;  // Prioritize streaming requests over other IO
    uint32_t streamingPriorityBoost = 10; // Priority boost for streaming reads

    AsyncIOConfig() = default;
};

// ===== Async IO System =====

class AsyncIOSystem {
public:
    AsyncIOSystem();
    ~AsyncIOSystem();

    // Initialize with configuration
    bool Initialize(const AsyncIOConfig& config);

    // Submit IO requests
    uint64_t SubmitRequest(const IORequest& request);
    uint64_t SubmitReadRequest(
        const std::wstring& filePath,
        uint64_t offset,
        uint64_t size,
        void* outputBuffer,
        CompressionType compression = CompressionType::None,
        std::function<void(bool, uint64_t)> callback = nullptr,
        uint32_t priority = 0
    );
    uint64_t SubmitWriteRequest(
        const std::wstring& filePath,
        uint64_t offset,
        const void* inputData,
        uint64_t size,
        std::function<void(bool, uint64_t)> callback = nullptr,
        uint32_t priority = 0
    );
    uint64_t SubmitDecompressRequest(
        const void* inputData,
        uint64_t inputSize,
        void* outputBuffer,
        uint64_t outputSize,
        CompressionType compression = CompressionType::None,
        std::function<void(bool, uint64_t)> callback = nullptr,
        uint32_t priority = 0
    );

    // Cancel request
    bool CancelRequest(uint64_t requestId);
    bool CancelAllRequests();

    // Update (called every frame to process completions)
    void Update();

    // Statistics
    IOStatistics GetStatistics() const;

    // Configuration
    bool SetConfig(const AsyncIOConfig& config);
    const AsyncIOConfig& GetConfig() const { return config_; }

    // Cleanup
    void Shutdown();

    // Check if initialized
    bool IsInitialized() const { return initialized_; }

private:
    // Platform-specific initialization
    bool InitializeIOCP();
    bool InitializeDirectStorage();
    void ShutdownIOCP();
    void ShutdownDirectStorage();

    // Worker threads
    static void IOThreadProc(void* parameter);
    static void DecompressionThreadProc(void* parameter);

    // IO processing
    void ProcessIOQueue();
    void ProcessDecompressionQueue();
    void ProcessCompletions();
    void PumpSynchronousQueues();

    // Compression utilities
    bool CompressData(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize, CompressionType type);
    bool DecompressData(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize, CompressionType type);

    // Zstd compression (requires lib)
    bool CompressZstd(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize);
    bool DecompressZstd(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize);

    // LZ4 compression (requires lib)
    bool CompressLZ4(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize);
    bool DecompressLZ4(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize);

    // Request management
    struct InternalRequest {
        IORequest request;
        uint64_t submitTime;
        uint32_t retryCount;
        bool inFlight;

        // Completion result (filled by worker threads, consumed on main thread in Update()).
        bool completedSuccess = false;
        uint64_t bytesProcessed = 0;
        uint32_t errorCode = 0;
    };

    bool WaitAndPopQueuedRequest(std::queue<InternalRequest>& queue, InternalRequest& request);
    bool TryPopQueuedRequest(std::queue<InternalRequest>& queue, InternalRequest& request);
    void ProcessIORequest(InternalRequest request);
    void ProcessDecompressionRequest(InternalRequest request);
    void ClearPendingQueues();
    static void EnqueueRequestByPriority(std::queue<InternalRequest>& queue, const InternalRequest& request);
    static bool RemoveQueuedRequest(std::queue<InternalRequest>& queue, uint64_t requestId);
    void IncrementOutstandingRequest(IOOperationType type);
    void DecrementOutstandingRequest(IOOperationType type);
    void ResetOutstandingRequests();
    uint64_t OutstandingRequestCount() const;
    uint64_t GenerateRequestId();

    // Configuration
    AsyncIOConfig config_;

    // Thread handles
    std::vector<void*> ioThreads_;
    std::vector<void*> decompressionThreads_;
#ifdef _WIN32
    void* ioCompletionPort_ = nullptr;
    void* shutdownEvent_ = nullptr;
#endif

    // Request queues
    std::queue<InternalRequest> ioQueue_;
    std::queue<InternalRequest> decompressionQueue_;
    std::queue<InternalRequest> completionQueue_;

    // Active requests
    std::unordered_map<uint64_t, InternalRequest> activeRequests_;
    std::atomic<uint64_t> nextRequestId_;
    std::atomic<uint32_t> outstandingReads_;
    std::atomic<uint32_t> outstandingWrites_;
    std::atomic<uint32_t> outstandingDecompressions_;

    // Synchronization
    mutable std::mutex queueMutex_;
    std::mutex completionMutex_;
    mutable std::mutex requestMutex_;
    std::condition_variable queueCondition_;

    // Statistics
    IOStatistics stats_;
    std::atomic<uint64_t> totalBytesRead_;
    std::atomic<uint64_t> totalBytesWritten_;
    std::atomic<uint64_t> totalBytesDecompressed_;
    std::atomic<uint32_t> failedOperations_;

    // State
    bool initialized_;
    std::atomic<bool> shuttingDown_;

#ifdef _WIN32
    // DirectStorage support (Windows only). See InitializeDirectStorage in
    // async_io.cpp for the SDK integration boundary.
    void* directStorageFactory_ = nullptr;  // IDStorageFactory*
#endif
};

// ===== Memory Pool for Streaming =====

class StreamingMemoryPool {
public:
    StreamingMemoryPool();
    ~StreamingMemoryPool();

    // Initialize with fixed pool size
    bool Initialize(size_t poolSizeBytes, uint32_t maxAllocations = 1024);

    // Allocate/free memory
    void* Allocate(size_t size, size_t alignment = 16);
    void Free(void* ptr);

    // Statistics
    size_t GetTotalSize() const { return poolSize_; }
    size_t GetUsedSize() const { return usedSize_; }
    size_t GetFreeSize() const { return poolSize_ - usedSize_; }
    size_t GetAllocationCount() const { return allocationCount_; }

    // Defragmentation (optional)
    bool Defragment();

    // Cleanup
    void Shutdown();

private:
    struct Allocation {
        void* ptr;
        size_t size;
        size_t padding = 0;
        bool freed = false;  // Tombstone set by Free(); collapsed off the top by Free/Defragment.
    };

    uint8_t* poolBase_;
    size_t poolSize_;
    size_t usedSize_;
    size_t allocationCount_;
    uint32_t maxAllocations_;

    std::vector<Allocation> allocations_;
    std::mutex mutex_;

    bool initialized_;
};

} // namespace Streaming
} // namespace Next
