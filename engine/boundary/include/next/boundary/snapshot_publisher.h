#pragma once

#include <cstddef>
#include <cstdint>
#include <map>

#include "next/boundary/snapshot.h"
#include "next/boundary/transport.h"

namespace Next {
class World;
}

namespace Next::boundary {

// How the publisher chooses the baseline each delta is computed against:
enum class DeltaMode {
    // Each delta is against the last ACKNOWLEDGED snapshot (Acknowledge()), NOT the last published
    // one. This is the B1 fix: the in-process / network transport DROPS intermediate snapshots
    // (latest-wins triple buffer), so a delta against "last published" loses the dropped frames'
    // spawn/despawn/move records forever and the mirror desyncs permanently. Against the acked
    // baseline, every delta is self-sufficient relative to what the consumer actually holds, so a
    // dropped snapshot costs nothing — the next one carries the full change since the ack. If the
    // consumer falls further behind than the bounded history, the next delta degrades to a KEYFRAME.
    Reliable,
    // Legacy: the baseline auto-advances every BuildDelta (i.e. "every frame is implicitly acked").
    // Correct ONLY on a lossless channel. Kept for the pure diff-logic tests and lossless callers.
    PerFrame,
};

// Turns the authoritative ECS state into a per-tick render-view delta (ADR-0006 §8). It walks the
// entities that carry a TransformComponent + RenderableComponent, diffs against the baseline, and
// emits spawn / update / despawn records in ascending EntityId order (deterministic).
class SnapshotPublisher {
public:
    explicit SnapshotPublisher(DeltaMode mode = DeltaMode::Reliable) : mode_(mode) {}

    // Build the delta against the current baseline into `out`, stamping sequence/baselineSequence.
    void BuildDelta(World& world, uint64_t tick, double seconds, SnapshotBlock& out);

    // Build straight into the transport's write slot and publish it (the common sim path).
    void PublishTo(ISnapshotTransport& transport, World& world, uint64_t tick, double seconds);

    // The consumer applied snapshot `seq`: advance the baseline to that snapshot's state so future
    // deltas compress against it. Stale (<= current baseline) or unknown (trimmed) acks are ignored.
    // No-op meaning in PerFrame mode (the baseline already auto-advances). Returns true if it advanced.
    bool Acknowledge(SnapshotSequence seq);

    // The acknowledged baseline sequence (kNoBaseline until the first ack / keyframe).
    SnapshotSequence AckedSequence() const { return baselineSeq_; }
    // Count of built-but-unacknowledged snapshots retained for delta compression (bounded).
    size_t InFlightCount() const { return inFlight_.size(); }

    size_t TrackedCount() const { return lastBuiltCount_; }
    void Reset();

private:
    using StateMap = std::map<EntityId, EntityRenderState>;

    // The most snapshots kept for delta compression (bounded memory).
    static constexpr size_t kMaxInFlight = 256;
    // If this many snapshots go unacknowledged, the consumer's ack stream has stalled (e.g. its acks are
    // being dropped, or it advanced past our baseline and keeps skipping our deltas). Fall back to a
    // KEYFRAME — always applicable on the consumer regardless of what it currently holds — to break the
    // stall WITHOUT relying on the consumer re-acking. A keyframe's own ack then re-establishes the
    // baseline. Must be < kMaxInFlight so it fires before history is trimmed.
    static constexpr size_t kKeyframeFallbackThreshold = 32;

    void Diff(const StateMap& baseline, const StateMap& current, SnapshotBlock& out) const;

    DeltaMode mode_;
    StateMap baseline_;  // the acknowledged state future deltas are computed against
    SnapshotSequence baselineSeq_ = kNoBaseline;
    std::map<SnapshotSequence, StateMap> inFlight_;  // seq -> built state, awaiting ack (Reliable mode)
    SnapshotSequence nextSeq_ = 1;                   // 0 is reserved (kNoBaseline)
    size_t lastBuiltCount_ = 0;
};

}  // namespace Next::boundary
