#pragma once

#include <cstdint>

#include "next/gameapi/capability.h"

// Game API stable flat C-ABI (ADR-0007). This is the FROZEN contract: version, error codes,
// the opaque EntityId, the host-call id table, and one POD args/result struct per call. No
// pointers, no STL — every struct may live directly inside a sandbox guest's linear memory
// and (eventually) travel cross-process / over the wire unchanged. The typed GameApi facade
// is the single implementation; AbiDispatch decodes these PODs and forwards to it.
//
// Evolution rule: CallId values and struct layouts are append-only. Breaking a layout bumps
// kGameApiVersionMajor; additive fields/calls bump the minor.

namespace Next::gameapi {

constexpr uint16_t kGameApiVersionMajor = 1;
constexpr uint16_t kGameApiVersionMinor = 1;
constexpr uint32_t kGameApiAbiVersion = (static_cast<uint32_t>(kGameApiVersionMajor) << 16) | kGameApiVersionMinor;

// Opaque stable id = the ECS Entity's 64-bit packed form. 0 is always invalid.
using EntityId = uint64_t;
constexpr EntityId kInvalidEntity = 0;

// Gameplay tags are bit indices 0..63 over a per-entity GameTag bitset (see game_world.h).
constexpr uint32_t kMaxTagIndex = 63;

// Result of every Game API call. Ok == success; everything else is a typed, recoverable error.
enum class Status : int32_t {
    Ok = 0,
    InvalidArgument,   // malformed/NaN/out-of-domain argument
    NotFound,          // entity / component / objective does not exist
    PermissionDenied,  // caller lacks the required Capability
    Unsupported,       // call not supported in this configuration
    OutOfRange,        // value outside the permitted range
    RateLimited,       // per-tick host-call or domain quota exhausted
    BufferTooSmall,    // output buffer could not hold the full result (count still reported)
    Internal,          // host-side invariant violation (should never reach a guest)
};

// Host-call id table. Append-only; ids are never reused or renumbered.
enum class CallId : uint32_t {
    GetTick = 1,     // Time
    GetTimeSeconds,  // Time
    Self,            // Observe
    IsValid,         // Observe
    GetPosition,     // Observe
    QueryByTag,      // Observe (variable-length result)
    SenseRadius,     // Sense   (variable-length result)
    SenseNearest,    // Sense
    MoveTo,          // Actuate (intent)
    Stop,            // Actuate (intent)
    SetActionFlag,   // Actuate (intent)
    SendSignal,      // Comms   (intent)
    GetObjective,    // Tasks
    ReportProgress,  // Tasks   (intent)
    Log,             // Log
    Raycast,         // Sense   (spatial query against the physical world)
    GetWaterState,   // Sense   (water surface/submersion/flow at a world point)

    Count_  // sentinel; not a callable id
};

// Maps a call to the single capability domain that gates it. Used by the facade and re-checked
// by the sandbox gateway. Returns Capability::Count_ for unknown/invalid ids (always denied).
constexpr Capability CapabilityFor(CallId id) {
    switch (id) {
        case CallId::GetTick:
        case CallId::GetTimeSeconds:
            return Capability::Time;
        case CallId::Self:
        case CallId::IsValid:
        case CallId::GetPosition:
        case CallId::QueryByTag:
            return Capability::Observe;
        case CallId::SenseRadius:
        case CallId::SenseNearest:
        case CallId::Raycast:
        case CallId::GetWaterState:
            return Capability::Sense;
        case CallId::MoveTo:
        case CallId::Stop:
        case CallId::SetActionFlag:
            return Capability::Actuate;
        case CallId::SendSignal:
            return Capability::Comms;
        case CallId::GetObjective:
        case CallId::ReportProgress:
            return Capability::Tasks;
        case CallId::Log:
            return Capability::Log;
        case CallId::Count_:
            break;
    }
    return Capability::Count_;  // unknown id -> no capability satisfies it -> denied
}

// ---- POD argument / result structs (natural alignment; layout locked by static_assert) ----

struct Vec3Abi {
    float x, y, z;
};

struct EntityArg {
    EntityId entity;
};
struct EntityResult {
    EntityId entity;
};
struct TickResult {
    uint64_t tick;
};
struct SecondsResult {
    double seconds;
};
struct BoolResult {
    uint32_t value;  // 0 / 1
};
struct PositionResult {
    Vec3Abi position;
};

struct QueryByTagArgs {
    uint32_t tag;  // bit index 0..63
};
struct SenseRadiusArgs {
    float radius;
};
struct SenseNearestArgs {
    float radius;
    uint32_t tag;  // bit index 0..63
};
struct SenseNearestResult {
    EntityId entity;  // kInvalidEntity if none within radius
    float distance;
};

struct RaycastArgs {
    Vec3Abi origin;
    Vec3Abi direction;  // need not be normalized
    float maxDistance;
};
// Result of a Game API raycast. `hit` is 0/1 (POD-friendly); `entity` is the ECS entity struck
// (kInvalidEntity when hit==0). Doubles as the IWorldQuery return type (see world_query.h).
struct RaycastResult {
    uint32_t hit;
    uint32_t reserved;  // padding to 8-align the EntityId that follows
    EntityId entity;
    Vec3Abi point;
    Vec3Abi normal;
    float distance;
};

// Water state at a world point (ADR-0015 W10): the Sense-domain query that lets player code / AI react
// to water — am I (or that point) submerged, how deep, where is the surface, is there a current, is the
// water conductive/lethal. A pure read of the authoritative water sim (no intent). The host evaluates
// the wavy surface at the shared SimClock, so what a guest reads matches what buoyancy applied.
struct GetWaterStateArgs {
    Vec3Abi point;
};
struct WaterStateResult {
    uint32_t inWater;       // 0/1: a water body governs this XZ (you are over/in water)
    uint32_t submerged;     // 0/1: the point is at/below the surface AND above the body floor
    uint32_t flags;         // WaterFlags of the governing body (conductive / lethal / breaks-sight / ...)
    uint32_t reserved;      // padding / future use (kept 0)
    float surfaceHeight;    // world Y of the water surface at the point's XZ (valid when inWater)
    float submersionDepth;  // surfaceHeight - point.y (> 0 when submerged; <= 0 otherwise)
    Vec3Abi flowVelocity;   // current m/s (rivers); zero for still water
};

// Variable-length entity-list result header. The output buffer is interpreted as this header
// followed by EntityId[capacity], where capacity = (retLen - sizeof(EntityListHeader)) / 8.
// `count` is always set to the TOTAL number of matches (so the caller learns it even when the
// buffer was too small); at most `capacity` ids are written, in ascending EntityId order.
struct EntityListHeader {
    uint32_t count;
    uint32_t capacity;
};

struct MoveToArgs {
    Vec3Abi target;
    float maxSpeed;
};
struct SetActionFlagArgs {
    uint32_t action;
    uint32_t on;  // 0 / 1
};
struct SendSignalArgs {
    uint32_t channel;
    int32_t code;
    float payload;
};
struct ObjectiveArg {
    uint32_t objectiveId;
};
struct ObjectiveResult {
    int32_t state;
};
struct ReportProgressArgs {
    uint32_t objectiveId;
    int32_t delta;
};
// Log args: header followed by `len` message bytes inside the same args blob.
struct LogArgs {
    int32_t level;  // maps to Next::LogLevel
    uint32_t len;   // message length in bytes following this header
};

static_assert(sizeof(Vec3Abi) == 12, "Vec3Abi ABI layout drift");
static_assert(sizeof(MoveToArgs) == 16, "MoveToArgs ABI layout drift");
static_assert(sizeof(SenseNearestResult) == 16, "SenseNearestResult ABI layout drift");
static_assert(sizeof(EntityListHeader) == 8, "EntityListHeader ABI layout drift");
static_assert(sizeof(SendSignalArgs) == 12, "SendSignalArgs ABI layout drift");
static_assert(sizeof(RaycastArgs) == 28, "RaycastArgs ABI layout drift");
static_assert(sizeof(RaycastResult) == 48, "RaycastResult ABI layout drift");
static_assert(sizeof(GetWaterStateArgs) == 12, "GetWaterStateArgs ABI layout drift");
static_assert(sizeof(WaterStateResult) == 36, "WaterStateResult ABI layout drift");

}  // namespace Next::gameapi
