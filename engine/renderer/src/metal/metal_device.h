#pragma once

#include "metal_release_queue.h"
#include "next/rhi/device.h"

#include <cstddef>

#import <Metal/Metal.h>

namespace Next {
namespace MetalBackend {

class MetalDevice final : public RHI::Device {
public:
    bool Initialize();
    void Shutdown();

    RHI::Backend GetBackend() const override { return RHI::Backend::Metal; }
    const char* GetDeviceName() const override { return deviceName_; }
    const RHI::DeviceFeatures& GetFeatures() const override { return features_; }

    id<MTLDevice> NativeDevice() const { return device_; }
    id<MTLCommandQueue> QueueForClass(RHI::QueueClass queueClass) const;
    bool HasDedicatedQueueForClass(RHI::QueueClass queueClass) const;

    void QueueForRelease(id object, uint64_t submittedFrameIndex);
    void CollectReleasedResources(uint64_t submittedFrameIndex, bool force = false);
    size_t PendingReleaseCount() const { return releaseQueue_.PendingCount(); }
    MetalReleaseQueueStats GetReleaseQueueStats() const { return releaseQueue_.GetStats(); }

private:
    void PopulateFeatures();

    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> graphicsQueue_ = nil;
    id<MTLCommandQueue> uploadQueue_ = nil;
    RHI::DeviceFeatures features_;
    MetalReleaseQueue releaseQueue_;
    char deviceName_[128] = {};
};

} // namespace MetalBackend
} // namespace Next
