#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <cstddef>
#include <cstdint>
#include <vector>

#import <Foundation/Foundation.h>

namespace Next {
namespace MetalBackend {

struct MetalReleaseQueueStats {
    size_t pendingObjectCount = 0;
    size_t peakPendingObjectCount = 0;
    uint64_t queuedObjectCount = 0;
    uint64_t collectedObjectCount = 0;
    uint64_t collectPassCount = 0;
    uint64_t forcedCollectPassCount = 0;
    uint32_t collectLatency = 0;
};

class MetalReleaseQueue final {
public:
    MetalReleaseQueue() = default;
    ~MetalReleaseQueue();

    MetalReleaseQueue(const MetalReleaseQueue&) = delete;
    MetalReleaseQueue& operator=(const MetalReleaseQueue&) = delete;

    void Initialize(uint32_t collectLatency);
    void Shutdown();

    void Queue(id object, uint64_t submittedFrameIndex);
    void Collect(uint64_t submittedFrameIndex, bool force = false);

    size_t PendingCount() const { return pending_.size(); }
    uint32_t GetCollectLatency() const { return collectLatency_; }
    MetalReleaseQueueStats GetStats() const;

private:
    struct PendingObject {
        CFTypeRef object = nullptr;
        uint64_t releaseAfterFrame = 0;
    };

    std::vector<PendingObject> pending_;
    MetalReleaseQueueStats stats_;
    uint32_t collectLatency_ = 3;
};

} // namespace MetalBackend
} // namespace Next
