#include "metal_release_queue.h"

#include <algorithm>
#include <limits>

namespace Next {
namespace MetalBackend {

namespace {

void IncrementCounter(uint64_t& counter) {
    if (counter < std::numeric_limits<uint64_t>::max()) {
        ++counter;
    }
}

void AddCounter(uint64_t& counter, size_t value) {
    const uint64_t maxValue = std::numeric_limits<uint64_t>::max();
    const uint64_t addend = value > maxValue ? maxValue : static_cast<uint64_t>(value);
    counter = addend > maxValue - counter ? maxValue : counter + addend;
}

} // namespace

MetalReleaseQueue::~MetalReleaseQueue() {
    Shutdown();
}

void MetalReleaseQueue::Initialize(uint32_t collectLatency) {
    Shutdown();
    collectLatency_ = std::max<uint32_t>(1, collectLatency);
    stats_ = {};
}

void MetalReleaseQueue::Shutdown() {
    Collect(0, true);
    pending_.clear();
}

void MetalReleaseQueue::Queue(id object, uint64_t submittedFrameIndex) {
    if (!object) {
        return;
    }

    PendingObject pending;
    pending.object = CFBridgingRetain(object);
    pending.releaseAfterFrame = submittedFrameIndex + collectLatency_;
    pending_.push_back(pending);
    IncrementCounter(stats_.queuedObjectCount);
    stats_.peakPendingObjectCount = std::max(stats_.peakPendingObjectCount, pending_.size());
}

void MetalReleaseQueue::Collect(uint64_t submittedFrameIndex, bool force) {
    IncrementCounter(stats_.collectPassCount);
    if (force) {
        IncrementCounter(stats_.forcedCollectPassCount);
    }

    size_t collectedCount = 0;
    const auto firstLive = std::remove_if(
        pending_.begin(),
        pending_.end(),
        [submittedFrameIndex, force, &collectedCount](PendingObject& pending) {
            if (!force && pending.releaseAfterFrame > submittedFrameIndex) {
                return false;
            }

            if (pending.object) {
                CFRelease(pending.object);
                pending.object = nullptr;
            }
            ++collectedCount;
            return true;
        });

    pending_.erase(firstLive, pending_.end());
    AddCounter(stats_.collectedObjectCount, collectedCount);
}

MetalReleaseQueueStats MetalReleaseQueue::GetStats() const {
    MetalReleaseQueueStats stats = stats_;
    stats.pendingObjectCount = pending_.size();
    stats.collectLatency = collectLatency_;
    return stats;
}

} // namespace MetalBackend
} // namespace Next
