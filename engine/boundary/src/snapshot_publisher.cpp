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

void SnapshotPublisher::BuildDelta(World& world, uint64_t tick, double seconds, SnapshotBlock& out) {
    out.Reset(tick, seconds);

    // Snapshot the current render set, ordered by id for deterministic emission.
    std::map<EntityId, PrevState> current;
    world.Each<TransformComponent, RenderableComponent>([&](Entity e, TransformComponent& t, RenderableComponent& r) {
        current.emplace(static_cast<EntityId>(e), PrevState{r.visual, r.animState, PackTransform(t)});
    });

    // Spawns and updates (and visual swaps -> despawn+spawn so UE5 can change the proxy mesh).
    for (const auto& [id, cur] : current) {
        auto prevIt = prev_.find(id);
        if (prevIt == prev_.end()) {
            out.spawns.push_back(SpawnRecord{id, cur.visual, cur.xform});
            continue;
        }
        const PrevState& prev = prevIt->second;
        if (prev.visual != cur.visual) {
            out.despawns.push_back(id);
            out.spawns.push_back(SpawnRecord{id, cur.visual, cur.xform});
        } else if (prev.animState != cur.animState || !TransformPackedEqual(prev.xform, cur.xform)) {
            out.updates.push_back(UpdateRecord{id, cur.xform, cur.animState});
        }
    }

    // Despawns: previously published, gone now.
    for (const auto& [id, unused] : prev_) {
        (void)unused;
        if (current.find(id) == current.end()) {
            out.despawns.push_back(id);
        }
    }
    // Spawns/updates come out ascending for free (current is a std::map). Despawns merge two
    // sources — the visual-swap branch above (current order) and this gone-entity loop (prev_
    // order) — so this single sort is load-bearing to keep the channel deterministic.
    std::sort(out.despawns.begin(), out.despawns.end());

    prev_.swap(current);
}

void SnapshotPublisher::PublishTo(ISnapshotTransport& transport, World& world, uint64_t tick, double seconds) {
    BuildDelta(world, tick, seconds, transport.BeginWrite());
    transport.PublishSnapshot();
}

}  // namespace Next::boundary
