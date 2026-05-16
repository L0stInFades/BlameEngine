#include "metal_device.h"

#include "next/foundation/logger.h"

#include <cstdio>
#include <cstring>

namespace Next {
namespace MetalBackend {

bool MetalDevice::Initialize() {
    @autoreleasepool {
        Shutdown();

        device_ = MTLCreateSystemDefaultDevice();
        if (!device_) {
            NEXT_LOG_ERROR("Metal is not supported on this device");
            return false;
        }

        graphicsQueue_ = [device_ newCommandQueue];
        if (!graphicsQueue_) {
            NEXT_LOG_ERROR("Failed to create Metal graphics command queue");
            Shutdown();
            return false;
        }

        uploadQueue_ = [device_ newCommandQueue];
        if (!uploadQueue_) {
            NEXT_LOG_WARNING("Failed to create dedicated Metal upload queue; uploads will use the graphics queue");
        }

        const char* name = device_.name ? [device_.name UTF8String] : "Apple GPU";
        std::snprintf(deviceName_, sizeof(deviceName_), "%s", name ? name : "Apple GPU");
        PopulateFeatures();
        releaseQueue_.Initialize(features_.maxFramesInFlight);

        NEXT_LOG_INFO("Metal device initialized: %s", deviceName_);
        return true;
    }
}

void MetalDevice::Shutdown() {
    releaseQueue_.Shutdown();
    uploadQueue_ = nil;
    graphicsQueue_ = nil;
    device_ = nil;
    features_ = {};
    std::memset(deviceName_, 0, sizeof(deviceName_));
}

void MetalDevice::PopulateFeatures() {
    features_.maxFramesInFlight = 3;
    features_.supportedQueueClasses =
        RHI::QueueClass::Graphics | RHI::QueueClass::Compute | RHI::QueueClass::Copy;
    features_.dedicatedQueueClasses = RHI::QueueClassFlag(RHI::QueueClass::Graphics);
    if (uploadQueue_) {
        features_.dedicatedQueueClasses =
            features_.dedicatedQueueClasses | RHI::QueueClass::Copy;
    }
    features_.asyncUploadQueue = features_.HasDedicatedQueueClass(RHI::QueueClass::Copy);

    if (!device_) {
        return;
    }

    if ([device_ respondsToSelector:@selector(hasUnifiedMemory)]) {
        features_.unifiedMemory = device_.hasUnifiedMemory;
    }

    if (device_.argumentBuffersSupport >= MTLArgumentBuffersTier2) {
        features_.argumentBufferTier = RHI::ArgumentBufferTier::Tier2;
    } else if (device_.argumentBuffersSupport >= MTLArgumentBuffersTier1) {
        features_.argumentBufferTier = RHI::ArgumentBufferTier::Tier1;
    }
    features_.argumentBuffers = features_.SupportsArgumentBufferTier(RHI::ArgumentBufferTier::Tier1);
    features_.bindlessResources = features_.SupportsArgumentBufferTier(RHI::ArgumentBufferTier::Tier2);
}

id<MTLCommandQueue> MetalDevice::QueueForClass(RHI::QueueClass queueClass) const {
    if (!features_.SupportsQueueClass(queueClass)) {
        return nil;
    }

    switch (queueClass) {
        case RHI::QueueClass::Graphics:
        case RHI::QueueClass::Compute:
            return graphicsQueue_;
        case RHI::QueueClass::Copy:
            return uploadQueue_ ? uploadQueue_ : graphicsQueue_;
        default:
            return nil;
    }
}

bool MetalDevice::HasDedicatedQueueForClass(RHI::QueueClass queueClass) const {
    return features_.HasDedicatedQueueClass(queueClass);
}

void MetalDevice::QueueForRelease(id object, uint64_t submittedFrameIndex) {
    releaseQueue_.Queue(object, submittedFrameIndex);
}

void MetalDevice::CollectReleasedResources(uint64_t submittedFrameIndex, bool force) {
    releaseQueue_.Collect(submittedFrameIndex, force);
}

} // namespace MetalBackend
} // namespace Next
