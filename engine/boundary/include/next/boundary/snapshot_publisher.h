#pragma once

#include <cstdint>
#include <map>

#include "next/boundary/snapshot.h"
#include "next/boundary/transport.h"

namespace Next {
class World;
}

namespace Next::boundary {

// Turns the authoritative ECS state into a per-tick render-view delta (ADR-0006 §8). It walks
// the entities that carry a TransformComponent + RenderableComponent (their transform column is
// contiguous in the archetype, so this is a near-linear scan), diffs against what it published
// last tick, and emits spawn / update / despawn records. Static entities produce nothing. Output
// is deterministic: records come out in ascending EntityId order.
class SnapshotPublisher {
public:
    // Build the delta since the previous BuildDelta/PublishTo into `out`.
    void BuildDelta(World& world, uint64_t tick, double seconds, SnapshotBlock& out);

    // Build straight into the transport's write slot and publish it (the common sim path).
    void PublishTo(ISnapshotTransport& transport, World& world, uint64_t tick, double seconds);

    size_t TrackedCount() const { return prev_.size(); }
    void Reset() { prev_.clear(); }

private:
    struct PrevState {
        VisualStateId visual = 0;
        uint32_t animState = 0;
        TransformPacked xform{};
    };
    std::map<EntityId, PrevState> prev_;  // ordered -> deterministic diff and emission order
};

}  // namespace Next::boundary
