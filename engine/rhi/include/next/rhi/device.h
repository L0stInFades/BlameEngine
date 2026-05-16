#pragma once

#include "next/rhi/types.h"

#include <cstdint>

namespace Next {
namespace RHI {

enum class ArgumentBufferTier : uint8_t {
    Unsupported = 0,
    Tier1,
    Tier2,
};

inline const char* ArgumentBufferTierName(ArgumentBufferTier tier) {
    switch (tier) {
        case ArgumentBufferTier::Tier1:
            return "tier1";
        case ArgumentBufferTier::Tier2:
            return "tier2";
        case ArgumentBufferTier::Unsupported:
        default:
            return "unsupported";
    }
}

struct DeviceFeatures {
    bool unifiedMemory = false;
    bool argumentBuffers = false;
    bool bindlessResources = false;
    bool asyncUploadQueue = false;
    QueueClassFlags supportedQueueClasses = 0;
    QueueClassFlags dedicatedQueueClasses = 0;
    uint32_t maxFramesInFlight = 2;
    ArgumentBufferTier argumentBufferTier = ArgumentBufferTier::Unsupported;

    bool SupportsQueueClass(QueueClass queueClass) const {
        return HasQueueClass(supportedQueueClasses, queueClass);
    }

    bool HasDedicatedQueueClass(QueueClass queueClass) const {
        return HasQueueClass(dedicatedQueueClasses, queueClass);
    }

    bool SupportsArgumentBufferTier(ArgumentBufferTier tier) const {
        return static_cast<uint8_t>(argumentBufferTier) >= static_cast<uint8_t>(tier);
    }
};

class Device {
public:
    virtual ~Device() = default;

    virtual Backend GetBackend() const = 0;
    virtual const char* GetDeviceName() const = 0;
    virtual const DeviceFeatures& GetFeatures() const = 0;
};

} // namespace RHI
} // namespace Next
