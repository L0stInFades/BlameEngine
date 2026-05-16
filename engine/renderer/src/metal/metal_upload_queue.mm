#include "metal_upload_queue.h"

#include "next/foundation/logger.h"
#include "next/jobsystem/job_system.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstring>
#include <limits>
#include <thread>
#include <utility>

namespace Next {
namespace MetalBackend {
namespace {

constexpr size_t kDefaultUploadStagingBytes = 4 * 1024 * 1024;
constexpr size_t kUploadStagingAlignment = 256;

size_t AlignUp(size_t value, size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

bool IsCommandBufferFinished(id<MTLCommandBuffer> commandBuffer) {
    if (!commandBuffer) {
        return true;
    }
    return commandBuffer.status == MTLCommandBufferStatusCompleted ||
        commandBuffer.status == MTLCommandBufferStatusError;
}

bool IsFinishedUploadStatus(MetalUploadStatus status) {
    return status == MetalUploadStatus::Completed || status == MetalUploadStatus::Failed;
}

void WaitForCommandBufferStatus(id<MTLCommandBuffer> commandBuffer) {
    // Avoid waitUntilCompleted here; it also waits for completion handlers that re-enter MetalUploadQueue.
    while (!IsCommandBufferFinished(commandBuffer)) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

} // namespace

const char* MetalUploadStatusName(MetalUploadStatus status) {
    switch (status) {
        case MetalUploadStatus::Pending:
            return "pending";
        case MetalUploadStatus::Completed:
            return "completed";
        case MetalUploadStatus::Failed:
            return "failed";
        case MetalUploadStatus::Unknown:
        default:
            return "unknown";
    }
}

bool MetalUploadQueue::Initialize(MetalDevice& device, MetalBufferPool& bufferPool) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Shutdown();

    dedicatedQueue_ = device.HasDedicatedQueueForClass(RHI::QueueClass::Copy);
    queue_ = device.QueueForClass(RHI::QueueClass::Copy);

    if (!queue_) {
        NEXT_LOG_ERROR("Metal upload queue initialization failed: no command queue available");
        return false;
    }

    bufferPool_ = &bufferPool;
    const uint32_t frameCount = std::max<uint32_t>(1, device.GetFeatures().maxFramesInFlight);
    stagingCapacityBytes_ = kDefaultUploadStagingBytes;
    framePackets_.resize(frameCount);

    RHI::BufferDesc stagingDesc;
    stagingDesc.sizeBytes = stagingCapacityBytes_;
    stagingDesc.memory = RHI::ResourceMemory::Shared;
    stagingDesc.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::CopySource);
    stagingDesc.initialState = RHI::ResourceState::CopySource;
    stagingDesc.debugName = "NEXT upload frame packet";

    for (FramePacket& packet : framePackets_) {
        packet.stagingBuffer = std::make_unique<MetalBuffer>();
        if (!bufferPool.CreateBuffer(*packet.stagingBuffer, stagingDesc)) {
            NEXT_LOG_ERROR("Failed to allocate Metal upload frame packet (%zu bytes)", stagingCapacityBytes_);
            Shutdown();
            return false;
        }
        packet.capacityBytes = stagingCapacityBytes_;
    }

    return true;
}

void MetalUploadQueue::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    ResetPendingUploads();
    for (FramePacket& packet : framePackets_) {
        WaitForPacket(packet);
        packet.commandBuffer = nil;
        if (packet.stagingBuffer) {
            if (bufferPool_) {
                bufferPool_->ReleaseBuffer(*packet.stagingBuffer);
            } else {
                packet.stagingBuffer->Shutdown();
            }
            packet.stagingBuffer.reset();
        }
        packet.serial = 0;
        packet.capacityBytes = 0;
    }
    framePackets_.clear();
    bufferPool_ = nullptr;
    queue_ = nil;
    stagingCapacityBytes_ = 0;
    nextFramePacket_ = 0;
    dedicatedQueue_ = false;
    submittedUploadCount_ = 0;
    lastSubmittedUpload_ = {};
    completedUploadCount_.store(0);
    failedUploadCount_.store(0);
    uploadStatusHistory_.clear();
}

size_t MetalUploadQueue::GetRetainedStatusCount() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return uploadStatusHistory_.size();
}

size_t MetalUploadQueue::GetRetainedStatusCapacity() const {
    return kUploadStatusHistoryLimit;
}

bool MetalUploadQueue::EnqueueBufferUpload(MetalBuffer& buffer,
                                           const void* data,
                                           size_t dataBytes,
                                           size_t destinationOffset) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return EnqueueBufferUpload(buffer.NativeBuffer(), data, dataBytes, destinationOffset);
}

bool MetalUploadQueue::EnqueueBufferUpload(id<MTLBuffer> buffer,
                                           const void* data,
                                           size_t dataBytes,
                                           size_t destinationOffset) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!queue_ || !buffer || !data || dataBytes == 0) {
        return false;
    }

    const size_t bufferLength = static_cast<size_t>([buffer length]);
    if (destinationOffset > bufferLength || dataBytes > bufferLength - destinationOffset) {
        NEXT_LOG_ERROR("Metal buffer upload rejected: offset %zu plus %zu bytes exceeds buffer length %zu",
                       destinationOffset,
                       dataBytes,
                       bufferLength);
        return false;
    }

    StagingAllocation allocation = ReserveStaging(dataBytes, kUploadStagingAlignment);
    if (!allocation.data) {
        NEXT_LOG_ERROR("Metal buffer upload rejected: %zu bytes do not fit in pending staging packet (%zu bytes)",
                       dataBytes,
                       stagingCapacityBytes_);
        return false;
    }

    std::memcpy(allocation.data, data, dataBytes);

    UploadRequest request;
    request.type = UploadRequestType::Buffer;
    request.buffer.destinationBuffer = buffer;
    request.buffer.sourceOffset = allocation.offset;
    request.buffer.destinationOffset = destinationOffset;
    request.buffer.dataBytes = dataBytes;
    pendingUploads_.push_back(request);
    return true;
}

bool MetalUploadQueue::EnqueueTexture2DUpload(MetalTexture& texture,
                                              const void* data,
                                              size_t dataBytes,
                                              uint32_t width,
                                              uint32_t height,
                                              uint32_t bytesPerPixel) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return EnqueueTexture2DUpload(texture.NativeTexture(), data, dataBytes, width, height, bytesPerPixel);
}

bool MetalUploadQueue::EnqueueTexture2DUpload(id<MTLTexture> texture,
                                              const void* data,
                                              size_t dataBytes,
                                              uint32_t width,
                                              uint32_t height,
                                              uint32_t bytesPerPixel) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!queue_ || !texture || !data || width == 0 || height == 0 || bytesPerPixel == 0) {
        return false;
    }

    if (texture.textureType != MTLTextureType2D) {
        NEXT_LOG_ERROR("Metal texture upload rejected: expected 2D texture, got type %lu",
                       static_cast<unsigned long>(texture.textureType));
        return false;
    }

    if (width > texture.width || height > texture.height) {
        NEXT_LOG_ERROR("Metal texture upload rejected: %ux%u exceeds texture extent %lux%lu",
                       width,
                       height,
                       static_cast<unsigned long>(texture.width),
                       static_cast<unsigned long>(texture.height));
        return false;
    }

    if (width > std::numeric_limits<size_t>::max() / bytesPerPixel) {
        NEXT_LOG_ERROR("Metal texture upload rejected: row size overflows for %ux%u", width, height);
        return false;
    }

    const size_t bytesPerRow = static_cast<size_t>(width) * bytesPerPixel;
    if (height > std::numeric_limits<size_t>::max() / bytesPerRow) {
        NEXT_LOG_ERROR("Metal texture upload rejected: image size overflows for %ux%u", width, height);
        return false;
    }

    const size_t expectedBytes = bytesPerRow * static_cast<size_t>(height);
    if (dataBytes < expectedBytes) {
        NEXT_LOG_ERROR("Metal upload rejected: %zu bytes provided, %zu required", dataBytes, expectedBytes);
        return false;
    }

    const size_t stagingBytesPerRow = AlignUp(bytesPerRow, kUploadStagingAlignment);
    if (height > std::numeric_limits<size_t>::max() / stagingBytesPerRow) {
        NEXT_LOG_ERROR("Metal texture upload rejected: staged image size overflows for %ux%u", width, height);
        return false;
    }

    const size_t stagingBytes = stagingBytesPerRow * static_cast<size_t>(height);
    StagingAllocation allocation = ReserveStaging(stagingBytes, kUploadStagingAlignment);
    if (!allocation.data) {
        NEXT_LOG_ERROR("Metal texture upload rejected: %zu bytes do not fit in pending staging packet (%zu bytes)",
                       stagingBytes,
                       stagingCapacityBytes_);
        return false;
    }

    uint8_t* staging = static_cast<uint8_t*>(allocation.data);
    std::memset(staging, 0, stagingBytes);
    const uint8_t* source = static_cast<const uint8_t*>(data);
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(staging + static_cast<size_t>(row) * stagingBytesPerRow,
                    source + static_cast<size_t>(row) * bytesPerRow,
                    bytesPerRow);
    }

    UploadRequest request;
    request.type = UploadRequestType::Texture2D;
    request.texture.destinationTexture = texture;
    request.texture.sourceOffset = allocation.offset;
    request.texture.sourceBytesPerRow = stagingBytesPerRow;
    request.texture.sourceBytesPerImage = stagingBytes;
    request.texture.width = width;
    request.texture.height = height;
    pendingUploads_.push_back(request);
    return true;
}

MetalUploadHandle MetalUploadQueue::SubmitUploads() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!pendingPacket_ || pendingUploads_.empty()) {
        return {};
    }

    @autoreleasepool {
        id<MTLCommandBuffer> commandBuffer = [queue_ commandBuffer];
        if (!commandBuffer) {
            NEXT_LOG_ERROR("Failed to allocate Metal upload command buffer");
            return {};
        }

        commandBuffer.label = @"NEXT upload batch";
        id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
        if (!blit) {
            NEXT_LOG_ERROR("Failed to allocate Metal upload blit encoder");
            return {};
        }

        id<MTLBuffer> stagingBuffer = pendingPacket_->stagingBuffer->NativeBuffer();
        for (const UploadRequest& request : pendingUploads_) {
            switch (request.type) {
                case UploadRequestType::Buffer:
                    [blit copyFromBuffer:stagingBuffer
                            sourceOffset:request.buffer.sourceOffset
                                toBuffer:request.buffer.destinationBuffer
                       destinationOffset:request.buffer.destinationOffset
                                    size:request.buffer.dataBytes];
                    break;
                case UploadRequestType::Texture2D:
                    [blit copyFromBuffer:stagingBuffer
                            sourceOffset:request.texture.sourceOffset
                       sourceBytesPerRow:request.texture.sourceBytesPerRow
                     sourceBytesPerImage:request.texture.sourceBytesPerImage
                              sourceSize:MTLSizeMake(request.texture.width, request.texture.height, 1)
                               toTexture:request.texture.destinationTexture
                        destinationSlice:0
                        destinationLevel:0
                       destinationOrigin:MTLOriginMake(0, 0, 0)];
                    break;
            }
        }
        [blit endEncoding];

        FramePacket& packet = *pendingPacket_;
        ResetPendingUploads();
        return SubmitPacket(packet, commandBuffer);
    }
}

bool MetalUploadQueue::SubmitUploadsAndWait() {
    const MetalUploadHandle handle = SubmitUploads();
    return WaitForUpload(handle);
}

MetalUploadHandle MetalUploadQueue::QueueBuffer(MetalDevice& device,
                                                MetalBuffer& buffer,
                                                const void* data,
                                                size_t dataBytes,
                                                size_t destinationOffset) {
    return QueueBuffer(device, buffer.NativeBuffer(), data, dataBytes, destinationOffset);
}

MetalUploadHandle MetalUploadQueue::QueueBuffer(MetalDevice& device,
                                                id<MTLBuffer> buffer,
                                                const void* data,
                                                size_t dataBytes,
                                                size_t destinationOffset) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!device.NativeDevice() || !EnqueueBufferUpload(buffer, data, dataBytes, destinationOffset)) {
        return {};
    }
    return SubmitUploads();
}

bool MetalUploadQueue::UploadBuffer(MetalDevice& device,
                                    MetalBuffer& buffer,
                                    const void* data,
                                    size_t dataBytes,
                                    size_t destinationOffset) {
    return UploadBuffer(device, buffer.NativeBuffer(), data, dataBytes, destinationOffset);
}

bool MetalUploadQueue::UploadBuffer(MetalDevice& device,
                                    id<MTLBuffer> buffer,
                                    const void* data,
                                    size_t dataBytes,
                                    size_t destinationOffset) {
    const MetalUploadHandle handle = QueueBuffer(device, buffer, data, dataBytes, destinationOffset);
    return WaitForUpload(handle);
}

MetalUploadHandle MetalUploadQueue::QueueTexture2D(MetalDevice& device,
                                                   MetalTexture& texture,
                                                   const void* data,
                                                   size_t dataBytes,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   uint32_t bytesPerPixel) {
    return QueueTexture2D(device, texture.NativeTexture(), data, dataBytes, width, height, bytesPerPixel);
}

MetalUploadHandle MetalUploadQueue::QueueTexture2D(MetalDevice& device,
                                                   id<MTLTexture> texture,
                                                   const void* data,
                                                   size_t dataBytes,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   uint32_t bytesPerPixel) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!device.NativeDevice() || !EnqueueTexture2DUpload(texture, data, dataBytes, width, height, bytesPerPixel)) {
        return {};
    }
    return SubmitUploads();
}

bool MetalUploadQueue::UploadTexture2D(MetalDevice& device,
                                       MetalTexture& texture,
                                       const void* data,
                                       size_t dataBytes,
                                       uint32_t width,
                                       uint32_t height,
                                       uint32_t bytesPerPixel) {
    return UploadTexture2D(device, texture.NativeTexture(), data, dataBytes, width, height, bytesPerPixel);
}

bool MetalUploadQueue::UploadTexture2D(MetalDevice& device,
                                       id<MTLTexture> texture,
                                       const void* data,
                                       size_t dataBytes,
                                       uint32_t width,
                                       uint32_t height,
                                       uint32_t bytesPerPixel) {
    const MetalUploadHandle handle = QueueTexture2D(device, texture, data, dataBytes, width, height, bytesPerPixel);
    return WaitForUpload(handle);
}

bool MetalUploadQueue::IsUploadComplete(const MetalUploadHandle& handle) const {
    return IsFinishedUploadStatus(GetUploadStatus(handle));
}

MetalUploadStatus MetalUploadQueue::GetUploadStatus(const MetalUploadHandle& handle) const {
    if (!handle) {
        return MetalUploadStatus::Unknown;
    }

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const FramePacket& packet : framePackets_) {
        if (packet.serial != handle.serial) {
            continue;
        }

        if (!packet.commandBuffer || !IsCommandBufferFinished(packet.commandBuffer)) {
            return MetalUploadStatus::Pending;
        }

        return packet.commandBuffer.status == MTLCommandBufferStatusError
            ? MetalUploadStatus::Failed
            : MetalUploadStatus::Completed;
    }

    for (const UploadStatusRecord& record : uploadStatusHistory_) {
        if (record.serial == handle.serial) {
            return record.status;
        }
    }

    return MetalUploadStatus::Unknown;
}

bool MetalUploadQueue::DidUploadFail(const MetalUploadHandle& handle) const {
    return GetUploadStatus(handle) == MetalUploadStatus::Failed;
}

MetalUploadStatus MetalUploadQueue::WaitForUploadStatus(const MetalUploadHandle& handle) {
    if (!handle) {
        return MetalUploadStatus::Unknown;
    }

    id<MTLCommandBuffer> commandBuffer = nil;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (FramePacket& packet : framePackets_) {
            if (packet.serial == handle.serial) {
                commandBuffer = packet.commandBuffer;
                break;
            }
        }
    }

    if (commandBuffer) {
        return WaitForRecordedUploadStatus(handle.serial);
    }

    return GetUploadStatus(handle);
}

bool MetalUploadQueue::WaitForUpload(const MetalUploadHandle& handle) {
    return WaitForUploadStatus(handle) == MetalUploadStatus::Completed;
}

JobHandle MetalUploadQueue::WaitForUploadAsync(const MetalUploadHandle& handle,
                                               MetalUploadCompletion completion) {
    if (!handle) {
        return {};
    }

    id<MTLCommandBuffer> commandBuffer = nil;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (FramePacket& packet : framePackets_) {
            if (packet.serial == handle.serial) {
                commandBuffer = packet.commandBuffer;
                break;
            }
        }
    }

    if (!commandBuffer) {
        const MetalUploadStatus status = GetUploadStatus(handle);
        if (completion && IsFinishedUploadStatus(status)) {
            completion(status);
        }
        return {};
    }

    JobSystem& jobSystem = JobSystem::Instance();
    if (!jobSystem.IsInitialized()) {
        const MetalUploadStatus status = WaitForRecordedUploadStatus(handle.serial);
        if (completion) {
            completion(status);
        }
        return {};
    }

    MetalUploadQueue* owner = this;
    const uint64_t serial = handle.serial;
    return jobSystem.Submit([owner, serial, completion = std::move(completion)]() mutable {
        const MetalUploadStatus status = owner->WaitForRecordedUploadStatus(serial);
        if (completion) {
            completion(status);
        }
    }, JobPriority::Low, {}, "Metal upload wait");
}

MetalUploadQueue::StagingAllocation MetalUploadQueue::ReserveStaging(size_t requiredBytes, size_t alignment) {
    if (requiredBytes == 0 || requiredBytes > stagingCapacityBytes_ || framePackets_.empty()) {
        return {};
    }

    if (!pendingPacket_) {
        pendingPacket_ = BeginFramePacket(requiredBytes);
        pendingOffset_ = 0;
    }

    if (!pendingPacket_ || !pendingPacket_->stagingBuffer || !pendingPacket_->stagingBuffer->IsReady()) {
        return {};
    }

    const size_t alignedOffset = AlignUp(pendingOffset_, alignment);
    if (alignedOffset > pendingPacket_->capacityBytes ||
        requiredBytes > pendingPacket_->capacityBytes - alignedOffset) {
        if (pendingUploads_.empty()) {
            return {};
        }

        if (!SubmitUploads()) {
            return {};
        }

        pendingPacket_ = BeginFramePacket(requiredBytes);
        pendingOffset_ = 0;
        if (!pendingPacket_ || !pendingPacket_->stagingBuffer || !pendingPacket_->stagingBuffer->IsReady()) {
            return {};
        }
    }

    const size_t allocationOffset = AlignUp(pendingOffset_, alignment);
    if (allocationOffset > pendingPacket_->capacityBytes ||
        requiredBytes > pendingPacket_->capacityBytes - allocationOffset) {
        return {};
    }

    uint8_t* stagingBytes = static_cast<uint8_t*>(pendingPacket_->stagingBuffer->Contents());
    if (!stagingBytes) {
        return {};
    }

    pendingOffset_ = allocationOffset + requiredBytes;
    return StagingAllocation{pendingPacket_, allocationOffset, stagingBytes + allocationOffset};
}

MetalUploadQueue::FramePacket* MetalUploadQueue::BeginFramePacket(size_t requiredBytes) {
    if (requiredBytes > stagingCapacityBytes_ || framePackets_.empty()) {
        return nullptr;
    }

    FramePacket& packet = framePackets_[nextFramePacket_];
    nextFramePacket_ = (nextFramePacket_ + 1) % framePackets_.size();

    if (!packet.stagingBuffer || !packet.stagingBuffer->IsReady() || !WaitForPacket(packet)) {
        return nullptr;
    }
    packet.commandBuffer = nil;
    packet.serial = 0;
    return &packet;
}

MetalUploadHandle MetalUploadQueue::SubmitPacket(FramePacket& packet, id<MTLCommandBuffer> commandBuffer) {
    if (!commandBuffer) {
        return {};
    }

    const uint64_t serial = submittedUploadCount_ + 1;
    packet.commandBuffer = commandBuffer;
    packet.serial = serial;

    MetalUploadQueue* owner = this;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedCommandBuffer) {
        owner->MarkUploadComplete(serial, completedCommandBuffer);
    }];

    [commandBuffer commit];
    submittedUploadCount_ = serial;
    lastSubmittedUpload_ = MetalUploadHandle{serial};
    return lastSubmittedUpload_;
}

bool MetalUploadQueue::WaitForPacket(FramePacket& packet) {
    if (!packet.commandBuffer) {
        return true;
    }

    WaitForCommandBufferStatus(packet.commandBuffer);

    MarkUploadComplete(packet.serial, packet.commandBuffer);
    return packet.commandBuffer.status != MTLCommandBufferStatusError;
}

void MetalUploadQueue::MarkUploadComplete(uint64_t serial, id<MTLCommandBuffer> commandBuffer) {
    if (serial == 0 || !commandBuffer || !IsCommandBufferFinished(commandBuffer)) {
        return;
    }

    const MetalUploadStatus status = commandBuffer.status == MTLCommandBufferStatusError
        ? MetalUploadStatus::Failed
        : MetalUploadStatus::Completed;
    NSError* error = nil;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        const bool recorded = RecordUploadStatusLocked(serial, status);
        if (!recorded) {
            return;
        }

        if (status == MetalUploadStatus::Failed) {
            error = commandBuffer.error;
            failedUploadCount_.fetch_add(1);
        } else {
            completedUploadCount_.fetch_add(1);
        }
    }

    if (status == MetalUploadStatus::Failed) {
        NEXT_LOG_ERROR("Metal upload %" PRIu64 " failed: %s",
                       serial,
                       error ? [[error localizedDescription] UTF8String] : "unknown error");
    }
    uploadStatusChanged_.notify_all();
}

MetalUploadStatus MetalUploadQueue::GetRecordedUploadStatusLocked(uint64_t serial) const {
    for (const UploadStatusRecord& record : uploadStatusHistory_) {
        if (record.serial == serial) {
            return record.status;
        }
    }
    return MetalUploadStatus::Unknown;
}

MetalUploadStatus MetalUploadQueue::WaitForRecordedUploadStatus(uint64_t serial) {
    if (serial == 0) {
        return MetalUploadStatus::Unknown;
    }

    std::unique_lock<std::recursive_mutex> lock(mutex_);
    uploadStatusChanged_.wait(lock, [&]() {
        return IsFinishedUploadStatus(GetRecordedUploadStatusLocked(serial));
    });
    return GetRecordedUploadStatusLocked(serial);
}

bool MetalUploadQueue::RecordUploadStatusLocked(uint64_t serial, MetalUploadStatus status) {
    if (serial == 0 ||
        (status != MetalUploadStatus::Completed && status != MetalUploadStatus::Failed)) {
        return false;
    }

    for (UploadStatusRecord& record : uploadStatusHistory_) {
        if (record.serial == serial) {
            if (record.status == status) {
                return false;
            }
            record.status = status;
            return true;
        }
    }

    if (uploadStatusHistory_.size() >= kUploadStatusHistoryLimit) {
        uploadStatusHistory_.erase(uploadStatusHistory_.begin());
    }
    uploadStatusHistory_.push_back(UploadStatusRecord{serial, status});
    return true;
}

void MetalUploadQueue::ResetPendingUploads() {
    pendingUploads_.clear();
    pendingPacket_ = nullptr;
    pendingOffset_ = 0;
}

} // namespace MetalBackend
} // namespace Next
