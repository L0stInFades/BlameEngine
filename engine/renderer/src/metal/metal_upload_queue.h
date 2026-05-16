#pragma once

#include "metal_device.h"
#include "metal_resource_pool.h"
#include "next/jobsystem/job.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#import <Metal/Metal.h>

namespace Next {
namespace MetalBackend {

struct MetalUploadHandle {
    uint64_t serial = 0;

    explicit operator bool() const { return serial != 0; }
};

enum class MetalUploadStatus : uint8_t {
    Unknown = 0,
    Pending,
    Completed,
    Failed,
};

using MetalUploadCompletion = std::function<void(MetalUploadStatus status)>;

const char* MetalUploadStatusName(MetalUploadStatus status);

class MetalUploadQueue final {
public:
    bool Initialize(MetalDevice& device, MetalBufferPool& bufferPool);
    void Shutdown();

    bool EnqueueBufferUpload(MetalBuffer& buffer,
                             const void* data,
                             size_t dataBytes,
                             size_t destinationOffset = 0);
    bool EnqueueBufferUpload(id<MTLBuffer> buffer,
                             const void* data,
                             size_t dataBytes,
                             size_t destinationOffset = 0);
    bool EnqueueTexture2DUpload(MetalTexture& texture,
                                const void* data,
                                size_t dataBytes,
                                uint32_t width,
                                uint32_t height,
                                uint32_t bytesPerPixel);
    bool EnqueueTexture2DUpload(id<MTLTexture> texture,
                                const void* data,
                                size_t dataBytes,
                                uint32_t width,
                                uint32_t height,
                                uint32_t bytesPerPixel);
    MetalUploadHandle SubmitUploads();
    bool SubmitUploadsAndWait();

    MetalUploadHandle QueueBuffer(MetalDevice& device,
                                  MetalBuffer& buffer,
                                  const void* data,
                                  size_t dataBytes,
                                  size_t destinationOffset = 0);
    MetalUploadHandle QueueBuffer(MetalDevice& device,
                                  id<MTLBuffer> buffer,
                                  const void* data,
                                  size_t dataBytes,
                                  size_t destinationOffset = 0);
    bool UploadBuffer(MetalDevice& device,
                      MetalBuffer& buffer,
                      const void* data,
                      size_t dataBytes,
                      size_t destinationOffset = 0);
    bool UploadBuffer(MetalDevice& device,
                      id<MTLBuffer> buffer,
                      const void* data,
                      size_t dataBytes,
                      size_t destinationOffset = 0);
    MetalUploadHandle QueueTexture2D(MetalDevice& device,
                                     MetalTexture& texture,
                                     const void* data,
                                     size_t dataBytes,
                                     uint32_t width,
                                     uint32_t height,
                                     uint32_t bytesPerPixel);
    MetalUploadHandle QueueTexture2D(MetalDevice& device,
                                     id<MTLTexture> texture,
                                     const void* data,
                                     size_t dataBytes,
                                     uint32_t width,
                                     uint32_t height,
                                     uint32_t bytesPerPixel);
    bool UploadTexture2D(MetalDevice& device,
                         MetalTexture& texture,
                         const void* data,
                         size_t dataBytes,
                         uint32_t width,
                         uint32_t height,
                         uint32_t bytesPerPixel);
    bool UploadTexture2D(MetalDevice& device,
                         id<MTLTexture> texture,
                         const void* data,
                         size_t dataBytes,
                         uint32_t width,
                         uint32_t height,
                         uint32_t bytesPerPixel);
    bool IsUploadComplete(const MetalUploadHandle& handle) const;
    MetalUploadStatus GetUploadStatus(const MetalUploadHandle& handle) const;
    bool DidUploadFail(const MetalUploadHandle& handle) const;
    MetalUploadStatus WaitForUploadStatus(const MetalUploadHandle& handle);
    bool WaitForUpload(const MetalUploadHandle& handle);
    JobHandle WaitForUploadAsync(const MetalUploadHandle& handle,
                                 MetalUploadCompletion completion = {});

    bool IsReady() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return queue_ != nil;
    }
    bool UsesDedicatedQueue() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return dedicatedQueue_;
    }
    RHI::QueueClass GetQueueClass() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return dedicatedQueue_ ? RHI::QueueClass::Copy : RHI::QueueClass::Graphics;
    }
    bool HasPendingUploads() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return pendingPacket_ != nullptr && !pendingUploads_.empty();
    }
    size_t GetPendingUploadCount() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return pendingUploads_.size();
    }
    size_t GetPendingUploadBytes() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return pendingOffset_;
    }
    MetalUploadHandle GetLastSubmittedUpload() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return lastSubmittedUpload_;
    }
    uint64_t GetSubmittedUploadCount() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return submittedUploadCount_;
    }
    uint64_t GetCompletedUploadCount() const { return completedUploadCount_.load(); }
    uint64_t GetFailedUploadCount() const { return failedUploadCount_.load(); }
    size_t GetStagingCapacityBytes() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return stagingCapacityBytes_;
    }
    size_t GetRetainedStatusCount() const;
    size_t GetRetainedStatusCapacity() const;

private:
    enum class UploadRequestType : uint8_t {
        Buffer,
        Texture2D,
    };

    struct BufferUploadRequest {
        id<MTLBuffer> destinationBuffer = nil;
        size_t sourceOffset = 0;
        size_t destinationOffset = 0;
        size_t dataBytes = 0;
    };

    struct Texture2DUploadRequest {
        id<MTLTexture> destinationTexture = nil;
        size_t sourceOffset = 0;
        size_t sourceBytesPerRow = 0;
        size_t sourceBytesPerImage = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct UploadRequest {
        UploadRequestType type = UploadRequestType::Buffer;
        BufferUploadRequest buffer;
        Texture2DUploadRequest texture;
    };

    struct FramePacket {
        std::unique_ptr<MetalBuffer> stagingBuffer;
        id<MTLCommandBuffer> commandBuffer = nil;
        uint64_t serial = 0;
        size_t capacityBytes = 0;
    };

    struct UploadStatusRecord {
        uint64_t serial = 0;
        MetalUploadStatus status = MetalUploadStatus::Unknown;
    };

    struct StagingAllocation {
        FramePacket* packet = nullptr;
        size_t offset = 0;
        void* data = nullptr;
    };

    StagingAllocation ReserveStaging(size_t requiredBytes, size_t alignment);
    FramePacket* BeginFramePacket(size_t requiredBytes);
    bool WaitForPacket(FramePacket& packet);
    MetalUploadHandle SubmitPacket(FramePacket& packet, id<MTLCommandBuffer> commandBuffer);
    void MarkUploadComplete(uint64_t serial, id<MTLCommandBuffer> commandBuffer);
    MetalUploadStatus GetRecordedUploadStatusLocked(uint64_t serial) const;
    MetalUploadStatus WaitForRecordedUploadStatus(uint64_t serial);
    bool RecordUploadStatusLocked(uint64_t serial, MetalUploadStatus status);
    void ResetPendingUploads();

    static constexpr size_t kUploadStatusHistoryLimit = 128;

    std::vector<FramePacket> framePackets_;
    std::vector<UploadRequest> pendingUploads_;
    std::vector<UploadStatusRecord> uploadStatusHistory_;
    mutable std::recursive_mutex mutex_;
    std::condition_variable_any uploadStatusChanged_;
    MetalBufferPool* bufferPool_ = nullptr;
    FramePacket* pendingPacket_ = nullptr;
    id<MTLCommandQueue> queue_ = nil;
    size_t stagingCapacityBytes_ = 0;
    size_t pendingOffset_ = 0;
    size_t nextFramePacket_ = 0;
    uint64_t submittedUploadCount_ = 0;
    MetalUploadHandle lastSubmittedUpload_;
    std::atomic<uint64_t> completedUploadCount_{0};
    std::atomic<uint64_t> failedUploadCount_{0};
    bool dedicatedQueue_ = false;
};

} // namespace MetalBackend
} // namespace Next
