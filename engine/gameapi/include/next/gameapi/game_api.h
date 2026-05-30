#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "next/gameapi/abi.h"
#include "next/gameapi/capability.h"
#include "next/gameapi/intent.h"
#include "next/gameapi/objective_store.h"
#include "next/gameapi/sim_clock.h"
#include "next/runtime/entity.h"

namespace Next {
class World;
}

namespace Next::gameapi {

struct IWorldQuery;  // world_query.h — abstract spatial query, implemented by the gameplay layer

// EntityId (ABI) <-> ECS Entity. EntityId is the Entity's 64-bit packed form; 0 stays invalid.
// The bit layout lives once on Entity itself (Entity::FromPacked / operator uint64_t).
inline Entity ToEntity(EntityId id) {
    return Entity::FromPacked(id);
}
inline EntityId ToEntityId(Entity e) {
    return static_cast<EntityId>(e);
}

// One caller context (one player's code, one AI agent, or one engine system). Reads hit the
// shared world/clock directly; writes are recorded as intents on this context's own queue. Each
// context carries its own capabilities, controlled entity, and per-tick quotas — so isolation
// and least-privilege are per-caller, not global.
struct GameApiConfig {
    const World* world = nullptr;  // read-only: the facade never mutates the world (writes = intents)
    const SimClock* clock = nullptr;
    ObjectiveStore* objectives = nullptr;  // optional; Tasks calls return NotFound if null
    IWorldQuery* worldQuery = nullptr;     // optional; Raycast returns Unsupported if null
    EntityId self = kInvalidEntity;
    CapabilitySet capabilities = CapabilitySet::None();

    uint32_t maxHostCallsPerTick = 4096;  // total Game API calls per tick (rate limit)
    uint32_t maxCommsPerTick = 64;        // SendSignal quota per tick
    uint32_t maxLogsPerTick = 64;         // Log quota per tick
    uint32_t logRingCapacity = 256;       // captured log lines retained for replay/debug
};

// The single typed implementation of the Game API contract (ADR-0007). Every method enforces
// the call's capability and validates its arguments; writes append a validated Intent rather
// than mutating the world. AbiDispatch is a thin POD adapter over this class — there is no
// second implementation.
class GameApi {
public:
    explicit GameApi(const GameApiConfig& config);

    // --- lifecycle ---
    // Reset per-tick rate-limit counters. The sim calls this for each context at tick start.
    void BeginTick();

    const CapabilitySet& Capabilities() const { return caps_; }
    EntityId SelfEntity() const { return self_; }

    // Recorded writes for this tick; the resolver consumes them then calls ClearIntents().
    const IntentQueue& Intents() const { return intents_; }
    void ClearIntents() { intents_.Clear(); }

    // Captured log lines (bounded ring), for replay/debugging.
    const std::deque<std::string>& LogRing() const { return logRing_; }

    uint64_t HostCallsThisTick() const { return hostCallsThisTick_; }

    // --- Time domain ---
    Status GetTick(uint64_t& outTick);
    Status GetTimeSeconds(double& outSeconds);

    // --- Observe domain ---
    Status GetSelf(EntityId& outEntity);
    Status IsValid(EntityId entity, bool& outValid);
    Status GetPosition(EntityId entity, Vec3Abi& outPosition);
    // Writes up to `capacity` matching ids (ascending) to outIds; outCount = total matches.
    Status QueryByTag(uint32_t tag, EntityId* outIds, uint32_t capacity, uint32_t& outCount);

    // --- Sense domain (relative to the controlled entity) ---
    Status SenseRadius(float radius, EntityId* outIds, uint32_t capacity, uint32_t& outCount);
    Status SenseNearest(float radius, uint32_t tag, EntityId& outEntity, float& outDistance);
    // Cast a ray against the physical world (via the injected IWorldQuery). Unsupported if none.
    Status Raycast(const Vec3Abi& origin, const Vec3Abi& direction, float maxDistance, RaycastResult& out);

    // --- Actuate domain (intents) ---
    Status MoveTo(const Vec3Abi& target, float maxSpeed);
    Status Stop();
    Status SetActionFlag(uint32_t action, bool on);

    // --- Comms domain (intent) ---
    Status SendSignal(uint32_t channel, int32_t code, float payload);

    // --- Tasks domain ---
    Status GetObjective(uint32_t objectiveId, int32_t& outState);
    Status ReportProgress(uint32_t objectiveId, int32_t delta);

    // --- Log domain ---
    Status Log(int32_t level, const char* message, uint32_t length);

private:
    // Charge one call against the per-tick host-call budget. Returns false (RateLimited) if spent.
    bool ChargeHostCall();

    // Shared entry guard for every call: enforce the capability, then charge one host-call.
    // Capability is checked first, so a denied call never consumes budget. Ok == proceed.
    Status Enter(Capability c);
    // Preconditions reused by several calls (a null world/clock is a host misconfiguration).
    Status RequireWorld() const { return world_ != nullptr ? Status::Ok : Status::Internal; }
    Status RequireClock() const { return clock_ != nullptr ? Status::Ok : Status::Internal; }
    // Sort (deterministic order), report total count, and copy up to `capacity` ids.
    Status WriteEntityList(std::vector<EntityId>& hits, EntityId* outIds, uint32_t capacity, uint32_t& outCount) const;

    const World* world_;
    const SimClock* clock_;
    ObjectiveStore* objectives_;
    IWorldQuery* worldQuery_;
    EntityId self_;
    CapabilitySet caps_;

    uint32_t maxHostCallsPerTick_;
    uint32_t maxCommsPerTick_;
    uint32_t maxLogsPerTick_;
    uint32_t logRingCapacity_;

    IntentQueue intents_;
    std::deque<std::string> logRing_;

    uint64_t hostCallsThisTick_ = 0;
    uint32_t commsThisTick_ = 0;
    uint32_t logsThisTick_ = 0;
};

}  // namespace Next::gameapi
