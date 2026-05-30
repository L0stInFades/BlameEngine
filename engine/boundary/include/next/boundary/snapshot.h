#pragma once

#include <cstdint>
#include <vector>

// sim→UE5 render-view data model (ADR-0006, docs/design/sim-ue5-boundary.md). Flat POD, SoA,
// no STL/virtuals/pointers in the wire records — so the same bytes work in-process today and as
// shared-memory / network replication later, with only the transport swapped. UE5 holds these
// read-only and maintains its own id→Actor map; the boundary owns no game logic.

namespace Next::boundary {

// Stable entity id = the ECS Entity's 64-bit packed form. 0 is invalid. Defined here so the
// boundary stays independent of the Game API and the ECS internals.
using EntityId = uint64_t;
constexpr EntityId kInvalidEntity = 0;

// Index into the UE5-side registry of mesh/skeleton/material variants. The sim never interprets
// it — it only forwards the id the game assigned.
using VisualStateId = uint32_t;

// Quantized transform (bandwidth-friendly; matters once this goes over the wire). Position stays
// float for precision near the camera; rotation is a 16-bit-per-component quaternion; scale is a
// single quantized uniform factor.
struct TransformPacked {
    float pos[3];
    int16_t rotQuat[4];  // each component * 32767, clamped to [-1, 1]
    uint16_t scale;      // uniform scale * kScaleQuant, clamped
};

constexpr float kRotQuant = 32767.0f;
constexpr float kScaleQuant = 1024.0f;
constexpr float kMaxScale = 65535.0f / kScaleQuant;

// Field-wise equality (NOT memcmp — TransformPacked has trailing padding whose bytes are
// unspecified, so memcmp would report spurious differences).
inline bool TransformPackedEqual(const TransformPacked& a, const TransformPacked& b) {
    return a.pos[0] == b.pos[0] && a.pos[1] == b.pos[1] && a.pos[2] == b.pos[2] && a.rotQuat[0] == b.rotQuat[0] &&
           a.rotQuat[1] == b.rotQuat[1] && a.rotQuat[2] == b.rotQuat[2] && a.rotQuat[3] == b.rotQuat[3] &&
           a.scale == b.scale;
}

// ECS component (boundary-owned) marking an entity as part of the render view. Attach it next to
// a TransformComponent; the publisher emits a Spawn/Update only for entities that have it.
struct RenderableComponent {
    VisualStateId visual = 0;
    uint32_t animState = 0;
};

struct SpawnRecord {
    EntityId id;
    VisualStateId visual;
    TransformPacked xform;
};
struct UpdateRecord {
    EntityId id;
    TransformPacked xform;
    uint32_t animState;
};

// A one-shot cosmetic trigger (sim→UE5): VFX/audio cue, carries no authority.
struct GameEvent {
    uint32_t type;
    EntityId subject;
    float params[4];
};

// Input / command (UE5→sim): player input, camera interest, submitted code handle, etc.
struct InputCmd {
    uint32_t type;
    float a[4];
};

// Natural (host) layout is locked here to catch accidental drift. The cross-process / network
// transport will define an explicitly packed wire encoding (no padding); in-process these travel
// as SoA vectors where padding is irrelevant.
static_assert(sizeof(TransformPacked) == 24, "TransformPacked host layout drift");
static_assert(sizeof(SpawnRecord) == 40, "SpawnRecord host layout drift");
static_assert(sizeof(UpdateRecord) == 40, "UpdateRecord host layout drift");

// One tick's render-view delta. In-process this uses SoA vectors (capacity is reused across
// frames, so the steady state is allocation-free); the wire form packs the same records
// contiguously after a header. Static entities never enter `updates` — zero cost.
//
// APPLY ORDER CONTRACT (consumers MUST honor): apply despawns, THEN spawns, THEN updates. A
// visual change is emitted as despawn(id) + spawn(id, newVisual) in the SAME block, so the same
// id appears in both lists; draining despawns first lets the re-spawn re-create the proxy with
// the new visual. Applying spawns before despawns would tear down the just-created proxy.
struct SnapshotBlock {
    uint64_t tick = 0;
    double simTimeSeconds = 0.0;
    std::vector<SpawnRecord> spawns;
    std::vector<UpdateRecord> updates;
    std::vector<EntityId> despawns;

    void Reset(uint64_t t, double seconds) {
        tick = t;
        simTimeSeconds = seconds;
        spawns.clear();
        updates.clear();
        despawns.clear();
    }
    size_t RecordCount() const { return spawns.size() + updates.size() + despawns.size(); }
};

}  // namespace Next::boundary
