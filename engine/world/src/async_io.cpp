#include "next/streaming/async_io.h"
#include "next/compression/compression.h"
#include "next/foundation/logger.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <filesystem>
#include <fstream>
#include <ios>
#endif

namespace Next {
namespace Streaming {

// ===== Async IO System Implementation =====

AsyncIOSystem::AsyncIOSystem()
    : nextRequestId_(1)
    , outstandingReads_(0)
    , outstandingWrites_(0)
    , outstandingDecompressions_(0)
    , totalBytesRead_(0)
    , totalBytesWritten_(0)
    , totalBytesDecompressed_(0)
    , failedOperations_(0)
    , initialized_(false)
    , shuttingDown_(false)
{
}

AsyncIOSystem::~AsyncIOSystem() {
    Shutdown();
}

bool AsyncIOSystem::Initialize(const AsyncIOConfig& config) {
    if (initialized_) {
        NEXT_LOG_WARNING("AsyncIOSystem already initialized");
        return true;
    }

    config_ = config;

    // Initialize statistics
    stats_ = IOStatistics();
    totalBytesRead_.store(0, std::memory_order_relaxed);
    totalBytesWritten_.store(0, std::memory_order_relaxed);
    totalBytesDecompressed_.store(0, std::memory_order_relaxed);
    failedOperations_.store(0, std::memory_order_relaxed);
    shuttingDown_.store(false, std::memory_order_release);

    ClearPendingQueues();
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        activeRequests_.clear();
    }
    ResetOutstandingRequests();

    // Initialize worker threads / IOCP backend.
    if (!InitializeIOCP()) {
        NEXT_LOG_ERROR("Failed to initialize async IO backend");
        return false;
    }

#ifdef _WIN32
    // Initialize DirectStorage (optional, Windows 10 Build 20348+)
    if (config_.enableDirectStorage) {
        if (!InitializeDirectStorage()) {
            NEXT_LOG_WARNING("DirectStorage not available, falling back to IOCP");
            config_.enableDirectStorage = false;
        }
    }
#endif

    // Reserve space for active request tracking.
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        activeRequests_.reserve(config_.maxPendingRequests);
    }

    // NEXT_LOG_INFO("AsyncIOSystem initialized (CP7: World Streaming)");
    // NEXT_LOG_INFO("  IO threads: %u", config_.ioThreads);
    // NEXT_LOG_INFO("  Decompression threads: %u", config_.decompressionThreads);
    // NEXT_LOG_INFO("  Max pending requests: %u", config_.maxPendingRequests);
    // NEXT_LOG_INFO("  DirectStorage: %s", config_.enableDirectStorage ? "enabled" : "disabled");

    initialized_ = true;
    return true;
}

bool AsyncIOSystem::SetConfig(const AsyncIOConfig& config) {
    if (initialized_) {
        NEXT_LOG_WARNING("AsyncIOSystem config changes require Shutdown() before reinitialization");
        return false;
    }

    config_ = config;
    return true;
}

uint64_t AsyncIOSystem::SubmitRequest(const IORequest& request) {
    if (!initialized_) {
        return 0;
    }

    switch (request.type) {
        case IOOperationType::Read:
        case IOOperationType::Write:
        case IOOperationType::Decompress:
            break;

        case IOOperationType::UploadGPU:
            NEXT_LOG_WARNING("AsyncIOSystem does not accept GPU upload requests");
            return 0;
    }

    InternalRequest internalReq;
    internalReq.request = request;
    internalReq.submitTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    internalReq.retryCount = 0;
    internalReq.inFlight = false;

    uint64_t requestId = 0;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::lock_guard<std::mutex> requestLock(requestMutex_);

        const uint64_t outstandingRequests = OutstandingRequestCount();
        if (outstandingRequests >= config_.maxPendingRequests) {
            NEXT_LOG_WARNING("AsyncIOSystem max pending requests reached (%llu/%u)",
                             static_cast<unsigned long long>(outstandingRequests),
                             config_.maxPendingRequests);
            return 0;
        }
        const uint32_t outstandingReads = outstandingReads_.load(std::memory_order_relaxed);
        if (request.type == IOOperationType::Read && outstandingReads >= config_.maxConcurrentReads) {
            NEXT_LOG_WARNING("AsyncIOSystem max outstanding reads reached (%u/%u)",
                             outstandingReads, config_.maxConcurrentReads);
            return 0;
        }

        requestId = GenerateRequestId();
        internalReq.request.requestId = requestId;
        activeRequests_[requestId] = internalReq;
        IncrementOutstandingRequest(internalReq.request.type);

        switch (request.type) {
            case IOOperationType::Read:
            case IOOperationType::Write:
                EnqueueRequestByPriority(ioQueue_, internalReq);
                break;

            case IOOperationType::Decompress:
                EnqueueRequestByPriority(decompressionQueue_, internalReq);
                break;

            case IOOperationType::UploadGPU:
                break;
        }
    }

    queueCondition_.notify_all();

    return requestId;
}

uint64_t AsyncIOSystem::SubmitReadRequest(
    const std::wstring& filePath,
    uint64_t offset,
    uint64_t size,
    void* outputBuffer,
    CompressionType compression,
    std::function<void(bool, uint64_t)> callback,
    uint32_t priority
) {
    IORequest request;
    request.type = IOOperationType::Read;
    request.filePath = filePath;
    request.offset = offset;
    request.size = size;
    request.outputBuffer = outputBuffer;
    request.outputSize = size;
    request.compressionType = compression;
    request.callback = callback;
    request.priority = priority;

    return SubmitRequest(request);
}

uint64_t AsyncIOSystem::SubmitWriteRequest(
    const std::wstring& filePath,
    uint64_t offset,
    const void* inputData,
    uint64_t size,
    std::function<void(bool, uint64_t)> callback,
    uint32_t priority
) {
    IORequest request;
    request.type = IOOperationType::Write;
    request.filePath = filePath;
    request.offset = offset;
    request.size = size;
    request.inputData = inputData;
    request.inputSize = size;
    request.callback = callback;
    request.priority = priority;

    return SubmitRequest(request);
}

uint64_t AsyncIOSystem::SubmitDecompressRequest(
    const void* inputData,
    uint64_t inputSize,
    void* outputBuffer,
    uint64_t outputSize,
    CompressionType compression,
    std::function<void(bool, uint64_t)> callback,
    uint32_t priority
) {
    IORequest request;
    request.type = IOOperationType::Decompress;
    request.size = inputSize;
    request.inputData = inputData;
    request.inputSize = inputSize;
    request.outputBuffer = outputBuffer;
    request.outputSize = outputSize;
    request.compressionType = compression;
    request.callback = callback;
    request.priority = priority;

    return SubmitRequest(request);
}

bool AsyncIOSystem::CancelRequest(uint64_t requestId) {
    bool removedQueued = false;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        removedQueued = RemoveQueuedRequest(ioQueue_, requestId);
        removedQueued = RemoveQueuedRequest(decompressionQueue_, requestId) || removedQueued;
    }
    {
        std::lock_guard<std::mutex> lock(completionMutex_);
        removedQueued = RemoveQueuedRequest(completionQueue_, requestId) || removedQueued;
    }

    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        auto it = activeRequests_.find(requestId);
        if (it != activeRequests_.end()) {
            const IOOperationType requestType = it->second.request.type;
            activeRequests_.erase(it);
            DecrementOutstandingRequest(requestType);
            return true;
        }
    }
    return removedQueued;
}

bool AsyncIOSystem::CancelAllRequests() {
    ClearPendingQueues();
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        activeRequests_.clear();
    }
    ResetOutstandingRequests();
    return true;
}

void AsyncIOSystem::Update() {
    if (!initialized_) {
        return;
    }

    PumpSynchronousQueues();

    // Process completions
    ProcessCompletions();
}

IOStatistics AsyncIOSystem::GetStatistics() const {
    IOStatistics snapshot = stats_;
    snapshot.pendingReads = outstandingReads_.load(std::memory_order_relaxed);
    snapshot.pendingWrites = outstandingWrites_.load(std::memory_order_relaxed);
    snapshot.pendingDecompressions = outstandingDecompressions_.load(std::memory_order_relaxed);

    snapshot.totalBytesRead = totalBytesRead_.load(std::memory_order_relaxed);
    snapshot.totalBytesWritten = totalBytesWritten_.load(std::memory_order_relaxed);
    snapshot.totalBytesDecompressed = totalBytesDecompressed_.load(std::memory_order_relaxed);
    snapshot.failedOperations = failedOperations_.load(std::memory_order_relaxed);
    return snapshot;
}

void AsyncIOSystem::Shutdown() {
    if (!initialized_) {
        return;
    }

    shuttingDown_.store(true, std::memory_order_release);
    queueCondition_.notify_all();

#ifdef _WIN32
    // Signal shutdown event
    if (shutdownEvent_) {
        SetEvent(reinterpret_cast<HANDLE>(shutdownEvent_));
    }

    // Wait for threads to finish
    for (void* threadPtr : ioThreads_) {
        HANDLE thread = reinterpret_cast<HANDLE>(threadPtr);
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    for (void* threadPtr : decompressionThreads_) {
        HANDLE thread = reinterpret_cast<HANDLE>(threadPtr);
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }

    ShutdownIOCP();
    ShutdownDirectStorage();

    if (shutdownEvent_) {
        CloseHandle(reinterpret_cast<HANDLE>(shutdownEvent_));
        shutdownEvent_ = nullptr;
    }
#else
    for (void* threadPtr : ioThreads_) {
        auto* thread = static_cast<std::thread*>(threadPtr);
        if (thread) {
            if (thread->joinable()) thread->join();
            delete thread;
        }
    }
    for (void* threadPtr : decompressionThreads_) {
        auto* thread = static_cast<std::thread*>(threadPtr);
        if (thread) {
            if (thread->joinable()) thread->join();
            delete thread;
        }
    }
#endif

    ioThreads_.clear();
    decompressionThreads_.clear();

    ClearPendingQueues();
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        activeRequests_.clear();
    }
    ResetOutstandingRequests();
    initialized_ = false;

    // NEXT_LOG_INFO("AsyncIOSystem shutdown complete");
}

// ===== Platform-specific Initialization =====

bool AsyncIOSystem::InitializeIOCP() {
#ifdef _WIN32
    // Create IO completion port
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, config_.ioThreads);
    ioCompletionPort_ = port;
    if (!ioCompletionPort_) {
        NEXT_LOG_ERROR("Failed to create IO completion port: %lu", GetLastError());
        return false;
    }

    // Create shutdown event
    HANDLE ev = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    shutdownEvent_ = ev;
    if (!shutdownEvent_) {
        NEXT_LOG_ERROR("Failed to create shutdown event: %lu", GetLastError());
        CloseHandle(reinterpret_cast<HANDLE>(ioCompletionPort_));
        return false;
    }

    // Spawn IO worker threads
    for (uint32_t i = 0; i < config_.ioThreads; ++i) {
        HANDLE thread = CreateThread(nullptr, 0,
            (LPTHREAD_START_ROUTINE)IOThreadProc, this, 0, nullptr);
        if (thread) {
            ioThreads_.push_back(reinterpret_cast<void*>(thread));
        } else {
            NEXT_LOG_ERROR("Failed to create IO thread %u", i);
        }
    }

    // Spawn decompression worker threads
    for (uint32_t i = 0; i < config_.decompressionThreads; ++i) {
        HANDLE thread = CreateThread(nullptr, 0,
            (LPTHREAD_START_ROUTINE)DecompressionThreadProc, this, 0, nullptr);
        if (thread) {
            decompressionThreads_.push_back(reinterpret_cast<void*>(thread));
        } else {
            NEXT_LOG_ERROR("Failed to create decompression thread %u", i);
        }
    }

    return true;
#else
    // POSIX path: spawn portable std::thread workers that share the same
    // mutex/cv-driven queue infrastructure as the Windows path. We don't go
    // through io_uring/kqueue here — a thread pool with std::ifstream gives
    // us cross-platform parity for streaming-scale loads (cell IO is
    // bandwidth-bound, not syscall-bound). Heap-allocated std::thread
    // pointers are stored in the shared void* vectors so Shutdown() can
    // join+free them uniformly.
    for (uint32_t i = 0; i < config_.ioThreads; ++i) {
        auto* thread = new std::thread(IOThreadProc, this);
        ioThreads_.push_back(static_cast<void*>(thread));
    }
    for (uint32_t i = 0; i < config_.decompressionThreads; ++i) {
        auto* thread = new std::thread(DecompressionThreadProc, this);
        decompressionThreads_.push_back(static_cast<void*>(thread));
    }
    return true;
#endif
}

bool AsyncIOSystem::InitializeDirectStorage() {
    // DirectStorage is an optional Windows-only fast-path that requires the
    // DirectStorage SDK (Microsoft.Direct3D.DirectStorage NuGet) and Windows
    // 10 Build 20348 or later. The SDK is intentionally not linked here so
    // the engine builds without it; callers fall back to the IOCP path
    // automatically via the early-out in Initialize(). When the SDK is
    // wired up, populate directStorageFactory_ via DStorageGetFactory and
    // configure queues here.
    return false;
}

void AsyncIOSystem::ShutdownIOCP() {
#ifdef _WIN32
    if (ioCompletionPort_) {
        CloseHandle(reinterpret_cast<HANDLE>(ioCompletionPort_));
        ioCompletionPort_ = nullptr;
    }
#endif
}

void AsyncIOSystem::ShutdownDirectStorage() {
#ifdef _WIN32
    if (directStorageFactory_) {
        // When InitializeDirectStorage() is fleshed out to acquire an
        // IDStorageFactory, this branch must invoke ->Release() on it
        // before clearing the pointer. Today the factory is never set,
        // so the conditional is a structural placeholder for that future
        // call site.
        directStorageFactory_ = nullptr;
    }
#endif
}

// ===== Worker Thread Procs =====

void AsyncIOSystem::IOThreadProc(void* parameter) {
    AsyncIOSystem* system = static_cast<AsyncIOSystem*>(parameter);

    while (!system->shuttingDown_.load(std::memory_order_acquire)) {
        system->ProcessIOQueue();
    }
}

void AsyncIOSystem::DecompressionThreadProc(void* parameter) {
    AsyncIOSystem* system = static_cast<AsyncIOSystem*>(parameter);

    while (!system->shuttingDown_.load(std::memory_order_acquire)) {
        system->ProcessDecompressionQueue();
    }
}

// ===== Queue Processing =====

bool AsyncIOSystem::WaitAndPopQueuedRequest(std::queue<InternalRequest>& queue, InternalRequest& request) {
    std::unique_lock<std::mutex> lock(queueMutex_);
    queueCondition_.wait(lock, [this, &queue]() {
        return shuttingDown_.load(std::memory_order_acquire) || !queue.empty();
    });

    if (shuttingDown_.load(std::memory_order_acquire)) {
        return false;
    }

    request = queue.front();
    queue.pop();
    return true;
}

bool AsyncIOSystem::TryPopQueuedRequest(std::queue<InternalRequest>& queue, InternalRequest& request) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (queue.empty()) {
        return false;
    }

    request = queue.front();
    queue.pop();
    return true;
}

void AsyncIOSystem::PumpSynchronousQueues() {
    if (shuttingDown_.load(std::memory_order_acquire)) {
        return;
    }

    InternalRequest request;
    if (config_.ioThreads == 0) {
        while (TryPopQueuedRequest(ioQueue_, request)) {
            ProcessIORequest(request);
        }
    }

    if (config_.decompressionThreads == 0) {
        while (TryPopQueuedRequest(decompressionQueue_, request)) {
            ProcessDecompressionRequest(request);
        }
    }
}

void AsyncIOSystem::ProcessIOQueue() {
    InternalRequest request;
    if (!WaitAndPopQueuedRequest(ioQueue_, request)) {
        return;
    }

    ProcessIORequest(request);
}

void AsyncIOSystem::ProcessIORequest(InternalRequest request) {
    bool success = false;
    uint64_t bytesProcessed = 0;
    uint32_t errorCode = 0;

#ifdef _WIN32
    if (request.request.type == IOOperationType::Read) {
        HANDLE h = CreateFileW(
            request.request.filePath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (h == INVALID_HANDLE_VALUE) {
            errorCode = static_cast<uint32_t>(GetLastError());
        } else {
            LARGE_INTEGER li;
            li.QuadPart = static_cast<LONGLONG>(request.request.offset);
            if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) {
                errorCode = static_cast<uint32_t>(GetLastError());
            } else {
                if (!request.request.outputBuffer || request.request.size == 0) {
                    errorCode = ERROR_INVALID_PARAMETER;
                } else {
                    DWORD toRead = static_cast<DWORD>(std::min<uint64_t>(request.request.size, 0xFFFFFFFFull));
                    DWORD readBytes = 0;
                    if (ReadFile(h, request.request.outputBuffer, toRead, &readBytes, nullptr)) {
                        bytesProcessed = static_cast<uint64_t>(readBytes);
                        success = (bytesProcessed == toRead);
                    } else {
                        errorCode = static_cast<uint32_t>(GetLastError());
                    }
                }
            }

            CloseHandle(h);
        }
    } else if (request.request.type == IOOperationType::Write) {
        HANDLE h = CreateFileW(
            request.request.filePath.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (h == INVALID_HANDLE_VALUE) {
            errorCode = static_cast<uint32_t>(GetLastError());
        } else {
            LARGE_INTEGER li;
            li.QuadPart = static_cast<LONGLONG>(request.request.offset);
            if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) {
                errorCode = static_cast<uint32_t>(GetLastError());
            } else {
                if (!request.request.inputData || request.request.size == 0) {
                    errorCode = ERROR_INVALID_PARAMETER;
                } else {
                    DWORD toWrite = static_cast<DWORD>(std::min<uint64_t>(request.request.size, 0xFFFFFFFFull));
                    DWORD written = 0;
                    if (WriteFile(h, request.request.inputData, toWrite, &written, nullptr)) {
                        bytesProcessed = static_cast<uint64_t>(written);
                        success = (bytesProcessed == toWrite);
                    } else {
                        errorCode = static_cast<uint32_t>(GetLastError());
                    }
                }
            }
            CloseHandle(h);
        }
    } else {
        // Unsupported in this queue.
        errorCode = ERROR_INVALID_PARAMETER;
    }
#else
    // POSIX path: use std::filesystem + binary fstreams. Conversion from
    // std::wstring goes through std::filesystem::path so wide paths still
    // work on macOS/Linux.
    constexpr uint32_t kPosixErrOpen   = 1;
    constexpr uint32_t kPosixErrParam  = 2;
    constexpr uint32_t kPosixErrSeek   = 3;
    constexpr uint32_t kPosixErrUnsup  = 4;

    std::filesystem::path path(request.request.filePath);

    if (request.request.type == IOOperationType::Read) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            errorCode = kPosixErrOpen;
        } else if (!request.request.outputBuffer || request.request.size == 0) {
            errorCode = kPosixErrParam;
        } else {
            stream.seekg(static_cast<std::streamoff>(request.request.offset),
                         std::ios::beg);
            if (!stream) {
                errorCode = kPosixErrSeek;
            } else {
                stream.read(static_cast<char*>(request.request.outputBuffer),
                            static_cast<std::streamsize>(request.request.size));
                bytesProcessed = static_cast<uint64_t>(stream.gcount());
                success = (bytesProcessed == request.request.size);
            }
        }
    } else if (request.request.type == IOOperationType::Write) {
        // Open for read+write so seekp on existing files lands at the right
        // offset; fall back to truncating create if the file doesn't exist.
        std::ofstream stream(path,
            std::ios::binary | std::ios::in | std::ios::out);
        if (!stream) {
            stream.open(path, std::ios::binary | std::ios::out);
        }
        if (!stream) {
            errorCode = kPosixErrOpen;
        } else if (!request.request.inputData || request.request.size == 0) {
            errorCode = kPosixErrParam;
        } else {
            stream.seekp(static_cast<std::streamoff>(request.request.offset),
                         std::ios::beg);
            if (!stream) {
                errorCode = kPosixErrSeek;
            } else {
                stream.write(static_cast<const char*>(request.request.inputData),
                             static_cast<std::streamsize>(request.request.size));
                if (stream.good()) {
                    bytesProcessed = request.request.size;
                    success = true;
                }
            }
        }
    } else {
        errorCode = kPosixErrUnsup;
    }
#endif

    // Update statistics
    if (success) {
        if (request.request.type == IOOperationType::Read) {
            totalBytesRead_.fetch_add(bytesProcessed, std::memory_order_relaxed);
        } else if (request.request.type == IOOperationType::Write) {
            totalBytesWritten_.fetch_add(bytesProcessed, std::memory_order_relaxed);
        }
    } else {
        failedOperations_.fetch_add(1, std::memory_order_relaxed);
    }

    // Enqueue completion (callbacks are executed on main thread in Update()).
    request.completedSuccess = success;
    request.bytesProcessed = bytesProcessed;
    request.errorCode = errorCode;
    {
        std::lock_guard<std::mutex> completionLock(completionMutex_);
        completionQueue_.push(request);
    }
}

void AsyncIOSystem::ProcessDecompressionQueue() {
    InternalRequest request;
    if (!WaitAndPopQueuedRequest(decompressionQueue_, request)) {
        return;
    }

    ProcessDecompressionRequest(request);
}

void AsyncIOSystem::ProcessDecompressionRequest(InternalRequest request) {
    // Process decompression
    bool success = false;
    uint64_t bytesProcessed = 0;
    uint32_t errorCode = 0;

    if (request.request.compressionType == CompressionType::Zstd) {
        bytesProcessed = request.request.outputSize;
        success = DecompressZstd(request.request.inputData, request.request.inputSize,
                                 request.request.outputBuffer, bytesProcessed);
    } else if (request.request.compressionType == CompressionType::LZ4) {
        bytesProcessed = request.request.outputSize;
        success = DecompressLZ4(request.request.inputData, request.request.inputSize,
                                request.request.outputBuffer, bytesProcessed);
    } else {
        // No compression
        if (request.request.outputBuffer && request.request.inputData) {
            // Security: Validate buffer sizes to prevent overflow
            if (request.request.outputSize >= request.request.inputSize) {
                memcpy(request.request.outputBuffer, request.request.inputData, request.request.inputSize);
                bytesProcessed = request.request.inputSize;
                success = true;
            } else {
                // Buffer too small - this is a critical error
                NEXT_LOG_ERROR("Buffer overflow prevented: outputSize=%llu < inputSize=%llu",
                               static_cast<unsigned long long>(request.request.outputSize),
                               static_cast<unsigned long long>(request.request.inputSize));
                success = false;
                errorCode = 1;
            }
        }
    }

    // Update statistics
    if (success) {
        totalBytesDecompressed_.fetch_add(bytesProcessed, std::memory_order_relaxed);
    } else {
        failedOperations_.fetch_add(1, std::memory_order_relaxed);
    }

    // Add to completion queue
    request.completedSuccess = success;
    request.bytesProcessed = bytesProcessed;
    request.errorCode = errorCode;
    {
        std::lock_guard<std::mutex> completionLock(completionMutex_);
        completionQueue_.push(request);
    }
}

void AsyncIOSystem::ProcessCompletions() {
    // Drain completions and execute callbacks on the main thread.
    std::queue<InternalRequest> completions;
    {
        std::lock_guard<std::mutex> lock(completionMutex_);
        std::swap(completions, completionQueue_);
    }

    while (!completions.empty()) {
        InternalRequest completed = completions.front();
        completions.pop();

        const uint64_t requestId = completed.request.requestId;
        std::function<void(bool, uint64_t)> callback;
        {
            std::lock_guard<std::mutex> lock(requestMutex_);
            auto it = activeRequests_.find(requestId);
            if (it == activeRequests_.end()) {
                // Possibly canceled.
                continue;
            }
            callback = it->second.request.callback;
        }

        // Execute callback if provided.
        if (callback) {
            try {
                callback(completed.completedSuccess, completed.bytesProcessed);
            } catch (const std::exception& e) {
                NEXT_LOG_ERROR("AsyncIO callback threw an exception: %s", e.what());
            } catch (...) {
                NEXT_LOG_ERROR("AsyncIO callback threw an unknown exception");
            }
        }

        {
            std::lock_guard<std::mutex> lock(requestMutex_);
            auto eraseIt = activeRequests_.find(requestId);
            if (eraseIt != activeRequests_.end()) {
                const IOOperationType requestType = eraseIt->second.request.type;
                activeRequests_.erase(eraseIt);
                DecrementOutstandingRequest(requestType);
            }
        }
    }
}

void AsyncIOSystem::ClearPendingQueues() {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::queue<InternalRequest> emptyIO;
        std::queue<InternalRequest> emptyDecompression;
        ioQueue_.swap(emptyIO);
        decompressionQueue_.swap(emptyDecompression);
    }
    {
        std::lock_guard<std::mutex> lock(completionMutex_);
        std::queue<InternalRequest> emptyCompletions;
        completionQueue_.swap(emptyCompletions);
    }
}

void AsyncIOSystem::EnqueueRequestByPriority(std::queue<InternalRequest>& queue, const InternalRequest& request) {
    std::queue<InternalRequest> reordered;
    bool inserted = false;

    while (!queue.empty()) {
        InternalRequest queued = queue.front();
        queue.pop();

        if (!inserted && request.request.priority < queued.request.priority) {
            reordered.push(request);
            inserted = true;
        }

        reordered.push(queued);
    }

    if (!inserted) {
        reordered.push(request);
    }

    queue.swap(reordered);
}

bool AsyncIOSystem::RemoveQueuedRequest(std::queue<InternalRequest>& queue, uint64_t requestId) {
    bool removed = false;
    std::queue<InternalRequest> retained;
    while (!queue.empty()) {
        InternalRequest request = queue.front();
        queue.pop();
        if (request.request.requestId == requestId) {
            removed = true;
            continue;
        }
        retained.push(request);
    }
    queue.swap(retained);
    return removed;
}

void AsyncIOSystem::IncrementOutstandingRequest(IOOperationType type) {
    switch (type) {
        case IOOperationType::Read:
            outstandingReads_.fetch_add(1, std::memory_order_relaxed);
            break;
        case IOOperationType::Write:
            outstandingWrites_.fetch_add(1, std::memory_order_relaxed);
            break;
        case IOOperationType::Decompress:
            outstandingDecompressions_.fetch_add(1, std::memory_order_relaxed);
            break;
        case IOOperationType::UploadGPU:
            break;
    }
}

void AsyncIOSystem::DecrementOutstandingRequest(IOOperationType type) {
    auto decrement = [](std::atomic<uint32_t>& counter) {
        uint32_t value = counter.load(std::memory_order_relaxed);
        while (value != 0 &&
               !counter.compare_exchange_weak(
                   value,
                   value - 1,
                   std::memory_order_relaxed,
                   std::memory_order_relaxed)) {
        }
    };

    switch (type) {
        case IOOperationType::Read:
            decrement(outstandingReads_);
            break;
        case IOOperationType::Write:
            decrement(outstandingWrites_);
            break;
        case IOOperationType::Decompress:
            decrement(outstandingDecompressions_);
            break;
        case IOOperationType::UploadGPU:
            break;
    }
}

void AsyncIOSystem::ResetOutstandingRequests() {
    outstandingReads_.store(0, std::memory_order_relaxed);
    outstandingWrites_.store(0, std::memory_order_relaxed);
    outstandingDecompressions_.store(0, std::memory_order_relaxed);
}

uint64_t AsyncIOSystem::OutstandingRequestCount() const {
    return static_cast<uint64_t>(outstandingReads_.load(std::memory_order_relaxed)) +
           static_cast<uint64_t>(outstandingWrites_.load(std::memory_order_relaxed)) +
           static_cast<uint64_t>(outstandingDecompressions_.load(std::memory_order_relaxed));
}

// ===== Compression Utilities =====

bool AsyncIOSystem::CompressData(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize, CompressionType type) {
    switch (type) {
        case CompressionType::Zstd:
            return CompressZstd(input, inputSize, output, outputSize);
        case CompressionType::LZ4:
            return CompressLZ4(input, inputSize, output, outputSize);
        default:
            return false;
    }
}

bool AsyncIOSystem::DecompressData(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize, CompressionType type) {
    switch (type) {
        case CompressionType::Zstd:
            return DecompressZstd(input, inputSize, output, outputSize);
        case CompressionType::LZ4:
            return DecompressLZ4(input, inputSize, output, outputSize);
        default:
            return false;
    }
}

bool AsyncIOSystem::CompressZstd(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize) {
    const auto result = Next::Compression::Compress(
        Next::Compression::Algorithm::Zstd, input, inputSize, output, outputSize, config_.compressionLevel);
    outputSize = result.bytesWritten;
    if (!result.Succeeded()) {
        NEXT_LOG_WARNING("Zstd compression failed: %s", result.message.c_str());
    }
    return result.Succeeded();
}

bool AsyncIOSystem::DecompressZstd(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize) {
    const auto result = Next::Compression::Decompress(
        Next::Compression::Algorithm::Zstd, input, inputSize, output, outputSize);
    outputSize = result.bytesWritten;
    if (!result.Succeeded()) {
        NEXT_LOG_WARNING("Zstd decompression failed: %s", result.message.c_str());
    }
    return result.Succeeded();
}

bool AsyncIOSystem::CompressLZ4(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize) {
    const auto result = Next::Compression::Compress(
        Next::Compression::Algorithm::LZ4, input, inputSize, output, outputSize, config_.compressionLevel);
    outputSize = result.bytesWritten;
    if (!result.Succeeded()) {
        NEXT_LOG_WARNING("LZ4 compression failed: %s", result.message.c_str());
    }
    return result.Succeeded();
}

bool AsyncIOSystem::DecompressLZ4(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize) {
    const auto result = Next::Compression::Decompress(
        Next::Compression::Algorithm::LZ4, input, inputSize, output, outputSize);
    outputSize = result.bytesWritten;
    if (!result.Succeeded()) {
        NEXT_LOG_WARNING("LZ4 decompression failed: %s", result.message.c_str());
    }
    return result.Succeeded();
}

// ===== Request Management =====

uint64_t AsyncIOSystem::GenerateRequestId() {
    return nextRequestId_.fetch_add(1);
}

// ===== Streaming Memory Pool =====

StreamingMemoryPool::StreamingMemoryPool()
    : poolBase_(nullptr)
    , poolSize_(0)
    , usedSize_(0)
    , allocationCount_(0)
    , maxAllocations_(1024)
    , initialized_(false)
{
}

StreamingMemoryPool::~StreamingMemoryPool() {
    Shutdown();
}

bool StreamingMemoryPool::Initialize(size_t poolSizeBytes, uint32_t maxAllocations) {
    if (initialized_) {
        return true;
    }
    if (poolSizeBytes == 0 || maxAllocations == 0) {
        NEXT_LOG_ERROR("Invalid streaming memory pool config: size=%zu maxAllocations=%u",
                       poolSizeBytes, maxAllocations);
        return false;
    }

    poolSize_ = poolSizeBytes;
    maxAllocations_ = maxAllocations;

    // Allocate memory pool
#ifdef _WIN32
    poolBase_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, poolSize_,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#else
    poolBase_ = static_cast<uint8_t*>(malloc(poolSize_));
#endif

    if (!poolBase_) {
        NEXT_LOG_ERROR("Failed to allocate streaming memory pool (%zu MB)", poolSize_ / (1024 * 1024));
        return false;
    }

    allocations_.reserve(maxAllocations_);

    // NEXT_LOG_INFO("StreamingMemoryPool initialized: %zu MB", poolSize_ / (1024 * 1024));
    initialized_ = true;
    return true;
}

void* StreamingMemoryPool::Allocate(size_t size, size_t alignment) {
    if (!initialized_) {
        return nullptr;
    }
    if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0) {
        NEXT_LOG_WARNING("Streaming memory pool invalid allocation request: size=%zu alignment=%zu",
                         size, alignment);
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const size_t alignmentMask = alignment - 1;
    if (size > std::numeric_limits<size_t>::max() - alignmentMask) {
        NEXT_LOG_WARNING("Streaming memory pool allocation size overflow: size=%zu alignment=%zu",
                         size, alignment);
        return nullptr;
    }

    const std::uintptr_t rawAddress = reinterpret_cast<std::uintptr_t>(poolBase_ + usedSize_);
    if (rawAddress > std::numeric_limits<std::uintptr_t>::max() - alignmentMask) {
        NEXT_LOG_WARNING("Streaming memory pool address alignment overflow: size=%zu alignment=%zu",
                         size, alignment);
        return nullptr;
    }

    const std::uintptr_t alignedAddress =
        (rawAddress + alignmentMask) & ~static_cast<std::uintptr_t>(alignmentMask);
    const size_t padding = static_cast<size_t>(alignedAddress - rawAddress);
    const size_t alignedSize = (size + alignmentMask) & ~alignmentMask;

    // Check if we have enough space
    if (padding > poolSize_ - usedSize_ || alignedSize > poolSize_ - usedSize_ - padding) {
        NEXT_LOG_ERROR("Streaming memory pool exhausted (%zu / %zu MB used)",
                      usedSize_ / (1024 * 1024), poolSize_ / (1024 * 1024));
        return nullptr;
    }

    // Check allocation count
    if (allocationCount_ >= maxAllocations_) {
        NEXT_LOG_ERROR("Streaming memory pool: max allocations reached");
        return nullptr;
    }

    // Allocate from pool
    uint8_t* ptr = reinterpret_cast<uint8_t*>(alignedAddress);

    Allocation alloc;
    alloc.ptr = ptr;
    alloc.size = alignedSize;
    alloc.padding = padding;

    allocations_.push_back(alloc);
    usedSize_ += padding + alignedSize;
    allocationCount_++;

    return ptr;
}

void StreamingMemoryPool::Free(void* ptr) {
    if (!ptr || !initialized_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Mark the matching allocation as freed. We keep the tombstone in place
    // (rather than erasing) so the bump pointer for non-top frees stays
    // correct — erasing+decrementing usedSize_ used to cause the next
    // Allocate() to overlap a still-live allocation higher in the pool.
    auto it = std::find_if(allocations_.begin(), allocations_.end(),
        [ptr](const Allocation& a) { return !a.freed && a.ptr == ptr; });

    if (it == allocations_.end()) {
        NEXT_LOG_WARNING("Attempted to free unknown pointer: %p", ptr);
        return;
    }

    it->freed = true;
    if (allocationCount_ > 0) {
        --allocationCount_;
    }

    // If the freed allocation is at the top, collapse it (and any consecutive
    // freed predecessors) so the bump pointer recovers immediately. Allocations
    // are appended in increasing pointer order by Allocate(), so iterating from
    // the back is correct.
    while (!allocations_.empty() && allocations_.back().freed) {
        const Allocation& top = allocations_.back();
        const size_t topOffset = static_cast<size_t>(
            static_cast<uint8_t*>(top.ptr) - poolBase_);
        usedSize_ = topOffset >= top.padding ? topOffset - top.padding : 0;
        allocations_.pop_back();
    }
}

bool StreamingMemoryPool::Defragment() {
    if (!initialized_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Step 1: discard tombstones. Free() collapses contiguous freed entries
    // off the top eagerly, but interior tombstones can accumulate; Defragment
    // reclaims their vector slots so allocations_.size() reflects live count.
    allocations_.erase(
        std::remove_if(allocations_.begin(), allocations_.end(),
            [](const Allocation& a) { return a.freed; }),
        allocations_.end());

    if (allocations_.empty()) {
        usedSize_ = 0;
        return true;
    }

    // Step 2: we do not relocate live allocations — callers hold raw pointers
    // and there is no API to hand them back updated values, so moving data
    // would silently invalidate them. What we *can* do safely is recompute
    // the bump-allocator's high-water mark from the topmost surviving
    // allocation, which reclaims any trailing space we missed (e.g. if Free
    // ran while a tombstone was nested below the top).
    std::sort(allocations_.begin(), allocations_.end(),
              [](const Allocation& a, const Allocation& b) {
                  return a.ptr < b.ptr;
              });

    const Allocation& top = allocations_.back();
    const size_t topOffset = static_cast<size_t>(
        static_cast<const uint8_t*>(top.ptr) - poolBase_) + top.size;
    usedSize_ = topOffset;
    return true;
}

void StreamingMemoryPool::Shutdown() {
    if (!initialized_) {
        return;
    }

#ifdef _WIN32
    if (poolBase_) {
        VirtualFree(poolBase_, 0, MEM_RELEASE);
        poolBase_ = nullptr;
    }
#else
    free(poolBase_);
    poolBase_ = nullptr;
#endif

    allocations_.clear();
    usedSize_ = 0;
    allocationCount_ = 0;
    initialized_ = false;

    // NEXT_LOG_INFO("StreamingMemoryPool shutdown complete");
}

} // namespace Streaming
} // namespace Next
