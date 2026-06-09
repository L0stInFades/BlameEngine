#include "next/boundary/snapshot_receiver.h"

namespace Next::boundary {
namespace {

// Monotonic follow: client time tracks the server's authoritative tick/seconds and never regresses.
void FollowServerTime(const SnapshotBlock& block, uint64_t& tick, double& seconds) {
    if (block.tick > tick) {
        tick = block.tick;
    }
    if (block.simTimeSeconds > seconds) {
        seconds = block.simTimeSeconds;
    }
}

}  // namespace

SnapshotReceiver::Result SnapshotReceiver::Apply(const SnapshotBlock& block) {
    // Stale / reordered (applies to keyframes too): never re-apply an older-or-equal sequence — that
    // would reset the mirror or move authoritative time backward on a late-arriving frame.
    if (appliedSeq_ != kNoBaseline && block.sequence <= appliedSeq_) {
        return Result::SkippedStale;
    }
    if (block.IsKeyframe()) {
        // Full state: reset and rebuild. (A keyframe carries only spawns, but honor the contract order
        // defensively in case despawns/updates are ever present.)
        mirror_.clear();
        for (const SpawnRecord& s : block.spawns) {
            mirror_[s.id] = EntityRenderState{s.visual, s.animState, s.xform};
        }
        for (const UpdateRecord& u : block.updates) {
            auto it = mirror_.find(u.id);
            if (it != mirror_.end()) {
                it->second.xform = u.xform;
                it->second.animState = u.animState;
            }
        }
        appliedSeq_ = block.sequence;
        FollowServerTime(block, serverTick_, serverSeconds_);
        return Result::AppliedKeyframe;
    }

    if (block.baselineSequence != appliedSeq_) {
        return Result::SkippedNoBaseline;  // not against our held state; wait for a matching one
    }

    // Apply in the contract order: despawns, then spawns, then updates.
    for (EntityId id : block.despawns) {
        mirror_.erase(id);
    }
    for (const SpawnRecord& s : block.spawns) {
        mirror_[s.id] = EntityRenderState{s.visual, s.animState, s.xform};
    }
    for (const UpdateRecord& u : block.updates) {
        auto it = mirror_.find(u.id);
        if (it != mirror_.end()) {
            it->second.xform = u.xform;
            it->second.animState = u.animState;
        }
    }
    appliedSeq_ = block.sequence;
    FollowServerTime(block, serverTick_, serverSeconds_);
    return Result::AppliedDelta;
}

void SnapshotReceiver::Reset() {
    mirror_.clear();
    appliedSeq_ = kNoBaseline;
    serverTick_ = 0;
    serverSeconds_ = 0.0;
}

}  // namespace Next::boundary
