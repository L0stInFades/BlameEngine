#pragma once

#include <cstdint>
#include <map>

#include "next/boundary/snapshot.h"

namespace Next::boundary {

// The consumer (UE5) side of the reliable snapshot protocol (ADR-0006 / B1 fix). It maintains the
// render-view MIRROR by applying snapshots from the transport, and reports which sequence to ACK back
// to the publisher (which then compresses future deltas against it). This is the headless reference
// for what the UE5 ISnapshotTransport consumer does; UE5 keeps its own id->Actor map on top.
//
// Apply rule (the safety property that makes dropped snapshots harmless):
//   * a KEYFRAME (baselineSequence == kNoBaseline) RESETS the mirror and is always applicable;
//   * a DELTA applies ONLY if its baselineSequence == the sequence we currently hold; otherwise it is
//     SKIPPED (the publisher hasn't yet seen our ack and is still diffing against an older baseline —
//     the next snapshot, once our ack lands, will be against our state). Skipping never loses data:
//     each delta is cumulative since its baseline, so we converge as soon as a matching one arrives.
class SnapshotReceiver {
public:
    enum class Result {
        AppliedKeyframe,   // mirror was reset and rebuilt from a full keyframe
        AppliedDelta,      // delta applied on top of the held baseline
        SkippedStale,      // sequence <= what we already hold (a re-delivered older frame)
        SkippedNoBaseline  // delta's baseline != our held sequence (wait for keyframe / matching delta)
    };

    // Apply one snapshot. After AppliedKeyframe/AppliedDelta, the caller should PushAck(AckSequence()).
    Result Apply(const SnapshotBlock& block);

    // The sequence to acknowledge (the last one applied; kNoBaseline before anything applied).
    SnapshotSequence AckSequence() const { return appliedSeq_; }

    // SERVER-AUTHORITATIVE TIME (W14). The client does NOT run its own sim clock; the authoritative
    // tick/seconds come from the applied snapshot. These are monotonic non-decreasing — a stale or
    // reordered snapshot can never move client time backward — so client logic and the render clock
    // always follow the server, never extrapolate past it.
    uint64_t ServerTick() const { return serverTick_; }
    double ServerSeconds() const { return serverSeconds_; }

    const std::map<EntityId, EntityRenderState>& Mirror() const { return mirror_; }
    size_t EntityCount() const { return mirror_.size(); }
    void Reset();

private:
    std::map<EntityId, EntityRenderState> mirror_;
    SnapshotSequence appliedSeq_ = kNoBaseline;
    uint64_t serverTick_ = 0;
    double serverSeconds_ = 0.0;
};

}  // namespace Next::boundary
