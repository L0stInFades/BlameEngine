#include "next/boundary/snapshot_publisher.h"

#include <algorithm>
#include <cmath>

#include "next/runtime/transform.h"
#include "next/runtime/world.h"

namespace Next::boundary {
namespace {

float ClampF(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Non-finite (NaN/Inf) values must never reach quantization: NaN survives ClampF (every ordered
// compare is false), and std::lround(NaN) is unspecified — two hosts could emit different bytes,
// breaking render determinism. A raw NaN position would also make TransformPackedEqual always
// false (NaN != NaN), emitting a spurious update every tick. Snap any non-finite field to 0.
float Finite(float v) {
    return std::isfinite(v) ? v : 0.0f;
}

TransformPacked PackTransform(const TransformComponent& t) {
    TransformPacked p{};
    p.pos[0] = Finite(t.position[0]);
    p.pos[1] = Finite(t.position[1]);
    p.pos[2] = Finite(t.position[2]);
    for (int i = 0; i < 4; ++i) {
        const float c = ClampF(Finite(t.rotation[i]), -1.0f, 1.0f);
        p.rotQuat[i] = static_cast<int16_t>(std::lround(c * kRotQuant));
    }
    const float s = ClampF(Finite(t.scale[0]), 0.0f, kMaxScale);
    p.scale = static_cast<uint16_t>(std::lround(s * kScaleQuant));
    return p;
}

}  // namespace

void SnapshotPublisher::Diff(const StateMap& baseline, const StateMap& current, SnapshotBlock& out) const {
    // Spawns and updates (and visual swaps -> despawn+spawn so UE5 can change the proxy mesh).
    for (const auto& [id, cur] : current) {
        auto prevIt = baseline.find(id);
        if (prevIt == baseline.end()) {
            out.spawns.push_back(SpawnRecord{id, cur.visual, cur.xform, cur.animState});
            continue;
        }
        const EntityRenderState& prev = prevIt->second;
        if (prev.visual != cur.visual) {
            out.despawns.push_back(id);
            out.spawns.push_back(SpawnRecord{id, cur.visual, cur.xform, cur.animState});
        } else if (prev.animState != cur.animState || !TransformPackedEqual(prev.xform, cur.xform)) {
            out.updates.push_back(UpdateRecord{id, cur.xform, cur.animState});
        }
    }
    // Despawns: in the baseline, gone now.
    for (const auto& [id, unused] : baseline) {
        (void)unused;
        if (current.find(id) == current.end()) {
            out.despawns.push_back(id);
        }
    }
    // Spawns/updates come out ascending for free (current is a std::map). Despawns merge two sources
    // (the visual-swap branch + the gone-entity loop) so this single sort keeps the channel ordered.
    std::sort(out.despawns.begin(), out.despawns.end());
}

void SnapshotPublisher::BuildDelta(World& world, uint64_t tick, double seconds, SnapshotBlock& out) {
    out.Reset(tick, seconds);

    // Snapshot the current render set, ordered by id for deterministic emission.
    StateMap current;
    world.Each<TransformComponent, RenderableComponent>([&](Entity e, TransformComponent& t, RenderableComponent& r) {
        current.emplace(static_cast<EntityId>(e), EntityRenderState{r.visual, r.animState, PackTransform(t)});
    });

    // Keyframe-fallback backstop (Reliable): if the ack stream has stalled (too many unacked snapshots),
    // the consumer may have advanced past our baseline and be skipping every vs-baseline delta we send —
    // a permanent desync/livelock if it never re-acks. A KEYFRAME is applicable no matter what the
    // consumer holds, so it breaks the stall; its own ack then re-establishes the baseline.
    static const StateMap kEmptyBaseline;
    const bool forceKeyframe =
        mode_ == DeltaMode::Reliable && baselineSeq_ != kNoBaseline && inFlight_.size() >= kKeyframeFallbackThreshold;
    out.baselineSequence = forceKeyframe ? kNoBaseline : baselineSeq_;
    Diff(forceKeyframe ? kEmptyBaseline : baseline_, current, out);
    out.sequence = nextSeq_++;
    lastBuiltCount_ = current.size();

    if (mode_ == DeltaMode::PerFrame) {
        // Lossless channel: the baseline auto-advances to what we just built (implicit ack).
        baseline_ = std::move(current);
        baselineSeq_ = out.sequence;
        return;
    }

    // Reliable: retain the built state so Acknowledge(seq) can promote it to the baseline. Bound the
    // history; if the consumer is further behind than this, the oldest in-flight states are dropped
    // (acking them later is ignored -> the next delta keeps using the current baseline, still correct).
    inFlight_.emplace(out.sequence, std::move(current));
    while (inFlight_.size() > kMaxInFlight) {
        inFlight_.erase(inFlight_.begin());
    }
}

bool SnapshotPublisher::Acknowledge(SnapshotSequence seq) {
    if (seq == kNoBaseline) {
        return false;
    }
    if (baselineSeq_ != kNoBaseline && seq <= baselineSeq_) {
        return false;  // stale / duplicate ack
    }
    const auto it = inFlight_.find(seq);
    if (it == inFlight_.end()) {
        return false;  // unknown / trimmed: keep the current baseline (next delta may keyframe)
    }
    baseline_ = it->second;
    baselineSeq_ = seq;
    // Everything at or before the acked seq is now useless for delta compression.
    inFlight_.erase(inFlight_.begin(), std::next(it));
    return true;
}

void SnapshotPublisher::PublishTo(ISnapshotTransport& transport, World& world, uint64_t tick, double seconds) {
    BuildDelta(world, tick, seconds, transport.BeginWrite());
    transport.PublishSnapshot();
}

void SnapshotPublisher::Reset() {
    baseline_.clear();
    baselineSeq_ = kNoBaseline;
    inFlight_.clear();
    nextSeq_ = 1;
    lastBuiltCount_ = 0;
}

}  // namespace Next::boundary
