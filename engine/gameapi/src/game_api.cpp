#include "next/gameapi/game_api.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "next/foundation/logger.h"
#include "next/gameapi/components.h"
#include "next/gameapi/world_query.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"

namespace Next::gameapi {
namespace {

bool IsFinite(float v) {
    return std::isfinite(v);
}
bool IsFinite(const Vec3Abi& v) {
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

float DistanceSquared(const Vec3Abi& a, const float (&b)[3]) {
    const float dx = a.x - b[0];
    const float dy = a.y - b[1];
    const float dz = a.z - b[2];
    return dx * dx + dy * dy + dz * dz;
}

}  // namespace

GameApi::GameApi(const GameApiConfig& config)
    : world_(config.world),
      clock_(config.clock),
      objectives_(config.objectives),
      worldQuery_(config.worldQuery),
      waterQuery_(config.waterQuery),
      self_(config.self),
      caps_(config.capabilities),
      maxHostCallsPerTick_(config.maxHostCallsPerTick),
      maxCommsPerTick_(config.maxCommsPerTick),
      maxLogsPerTick_(config.maxLogsPerTick),
      logRingCapacity_(config.logRingCapacity) {}

void GameApi::BeginTick() {
    hostCallsThisTick_ = 0;
    commsThisTick_ = 0;
    logsThisTick_ = 0;
}

bool GameApi::ChargeHostCall() {
    if (hostCallsThisTick_ >= maxHostCallsPerTick_) {
        return false;
    }
    ++hostCallsThisTick_;
    return true;
}

Status GameApi::Enter(Capability c) {
    if (!caps_.Has(c)) {
        return Status::PermissionDenied;  // checked first -> a denied call never spends budget
    }
    if (!ChargeHostCall()) {
        return Status::RateLimited;
    }
    return Status::Ok;
}

Status GameApi::WriteEntityList(std::vector<EntityId>& hits, EntityId* outIds, uint32_t capacity,
                                uint32_t& outCount) const {
    std::sort(hits.begin(), hits.end());  // deterministic ascending order
    outCount = static_cast<uint32_t>(hits.size());
    const uint32_t writeN = std::min<uint32_t>(capacity, outCount);
    for (uint32_t i = 0; i < writeN; ++i) {
        outIds[i] = hits[i];
    }
    return outCount > capacity ? Status::BufferTooSmall : Status::Ok;
}

// ---- Time ----

Status GameApi::GetTick(uint64_t& outTick) {
    if (Status s = Enter(Capability::Time); s != Status::Ok)
        return s;
    if (Status s = RequireClock(); s != Status::Ok)
        return s;
    outTick = clock_->tick;
    return Status::Ok;
}

Status GameApi::GetTimeSeconds(double& outSeconds) {
    if (Status s = Enter(Capability::Time); s != Status::Ok)
        return s;
    if (Status s = RequireClock(); s != Status::Ok)
        return s;
    outSeconds = clock_->seconds;
    return Status::Ok;
}

// ---- Observe ----

Status GameApi::GetSelf(EntityId& outEntity) {
    if (Status s = Enter(Capability::Observe); s != Status::Ok)
        return s;
    outEntity = self_;
    return Status::Ok;
}

Status GameApi::IsValid(EntityId entity, bool& outValid) {
    if (Status s = Enter(Capability::Observe); s != Status::Ok)
        return s;
    if (Status s = RequireWorld(); s != Status::Ok)
        return s;
    outValid = world_->IsEntityValid(ToEntity(entity));
    return Status::Ok;
}

Status GameApi::GetPosition(EntityId entity, Vec3Abi& outPosition) {
    if (Status s = Enter(Capability::Observe); s != Status::Ok)
        return s;
    if (Status s = RequireWorld(); s != Status::Ok)
        return s;
    const TransformComponent* t = world_->GetComponent<TransformComponent>(ToEntity(entity));
    if (t == nullptr)
        return Status::NotFound;
    outPosition = {t->position[0], t->position[1], t->position[2]};
    return Status::Ok;
}

Status GameApi::QueryByTag(uint32_t tag, EntityId* outIds, uint32_t capacity, uint32_t& outCount) {
    if (Status s = Enter(Capability::Observe); s != Status::Ok)
        return s;
    if (Status s = RequireWorld(); s != Status::Ok)
        return s;
    if (tag > kMaxTagIndex)
        return Status::InvalidArgument;
    if (capacity > 0 && outIds == nullptr)
        return Status::InvalidArgument;

    std::vector<EntityId> hits;
    world_->Each<GameTag>([&](Entity e, const GameTag& g) {
        if (g.Has(tag))
            hits.push_back(ToEntityId(e));
    });
    return WriteEntityList(hits, outIds, capacity, outCount);
}

// ---- Sense (relative to the controlled entity) ----

Status GameApi::SenseRadius(float radius, EntityId* outIds, uint32_t capacity, uint32_t& outCount) {
    if (Status s = Enter(Capability::Sense); s != Status::Ok)
        return s;
    if (Status s = RequireWorld(); s != Status::Ok)
        return s;
    if (!IsFinite(radius) || radius < 0.0f)
        return Status::InvalidArgument;
    if (capacity > 0 && outIds == nullptr)
        return Status::InvalidArgument;

    const TransformComponent* selfT = world_->GetComponent<TransformComponent>(ToEntity(self_));
    if (selfT == nullptr)
        return Status::NotFound;
    const Vec3Abi origin{selfT->position[0], selfT->position[1], selfT->position[2]};
    const float r2 = radius * radius;
    const EntityId selfId = self_;

    std::vector<EntityId> hits;
    world_->Each<TransformComponent>([&](Entity e, const TransformComponent& t) {
        const EntityId id = ToEntityId(e);
        if (id == selfId)
            return;
        if (DistanceSquared(origin, t.position) <= r2)
            hits.push_back(id);
    });
    return WriteEntityList(hits, outIds, capacity, outCount);
}

Status GameApi::SenseNearest(float radius, uint32_t tag, EntityId& outEntity, float& outDistance) {
    if (Status s = Enter(Capability::Sense); s != Status::Ok)
        return s;
    if (Status s = RequireWorld(); s != Status::Ok)
        return s;
    if (!IsFinite(radius) || radius < 0.0f || tag > kMaxTagIndex)
        return Status::InvalidArgument;

    const TransformComponent* selfT = world_->GetComponent<TransformComponent>(ToEntity(self_));
    if (selfT == nullptr)
        return Status::NotFound;
    const Vec3Abi origin{selfT->position[0], selfT->position[1], selfT->position[2]};
    const float r2 = radius * radius;
    const EntityId selfId = self_;

    EntityId best = kInvalidEntity;
    float bestD2 = 0.0f;
    // Iterate the GameTag column joined with transforms. Radius is inclusive (consistent with
    // SenseRadius); deterministic tie-break on equal distance is the smallest EntityId.
    world_->Each<TransformComponent, GameTag>([&](Entity e, const TransformComponent& t, const GameTag& g) {
        const EntityId id = ToEntityId(e);
        if (id == selfId || !g.Has(tag))
            return;
        const float d2 = DistanceSquared(origin, t.position);
        if (!(d2 <= r2))
            return;  // outside radius OR non-finite (NaN/Inf) — same acceptance as SenseRadius
        if (best == kInvalidEntity || d2 < bestD2 || (d2 == bestD2 && id < best)) {
            bestD2 = d2;
            best = id;
        }
    });

    outEntity = best;
    outDistance = (best == kInvalidEntity) ? 0.0f : std::sqrt(bestD2);
    return best == kInvalidEntity ? Status::NotFound : Status::Ok;
}

Status GameApi::Raycast(const Vec3Abi& origin, const Vec3Abi& direction, float maxDistance, RaycastResult& out) {
    if (Status s = Enter(Capability::Sense); s != Status::Ok)
        return s;
    if (!IsFinite(origin) || !IsFinite(direction) || !IsFinite(maxDistance) || maxDistance < 0.0f) {
        return Status::InvalidArgument;
    }
    if (worldQuery_ == nullptr) {
        return Status::Unsupported;  // no spatial query wired (e.g. a sim with no physics)
    }
    const float o[3] = {origin.x, origin.y, origin.z};
    const float d[3] = {direction.x, direction.y, direction.z};
    out = worldQuery_->Raycast(o, d, maxDistance);
    return Status::Ok;
}

Status GameApi::GetWaterState(const Vec3Abi& point, WaterStateResult& out) {
    if (Status s = Enter(Capability::Sense); s != Status::Ok)
        return s;
    if (!IsFinite(point)) {
        return Status::InvalidArgument;
    }
    if (waterQuery_ == nullptr) {
        return Status::Unsupported;  // no water query wired (e.g. a sim with no water)
    }
    const float p[3] = {point.x, point.y, point.z};
    out = waterQuery_->QueryWater(p);
    return Status::Ok;
}

// ---- Actuate (intents) ----

Status GameApi::MoveTo(const Vec3Abi& target, float maxSpeed) {
    if (Status s = Enter(Capability::Actuate); s != Status::Ok)
        return s;
    if (!IsFinite(target) || !IsFinite(maxSpeed) || maxSpeed < 0.0f)
        return Status::InvalidArgument;
    Intent intent;
    intent.type = IntentType::MoveTo;
    intent.source = self_;
    intent.vec = target;
    intent.scalar = maxSpeed;
    intents_.Push(intent);
    return Status::Ok;
}

Status GameApi::Stop() {
    if (Status s = Enter(Capability::Actuate); s != Status::Ok)
        return s;
    Intent intent;
    intent.type = IntentType::Stop;
    intent.source = self_;
    intents_.Push(intent);
    return Status::Ok;
}

Status GameApi::SetActionFlag(uint32_t action, bool on) {
    if (Status s = Enter(Capability::Actuate); s != Status::Ok)
        return s;
    if (action >= 32)
        return Status::OutOfRange;
    Intent intent;
    intent.type = IntentType::SetActionFlag;
    intent.source = self_;
    intent.a = action;
    intent.b = on ? 1 : 0;
    intents_.Push(intent);
    return Status::Ok;
}

// ---- Comms (intent) ----

Status GameApi::SendSignal(uint32_t channel, int32_t code, float payload) {
    if (Status s = Enter(Capability::Comms); s != Status::Ok)
        return s;
    if (!IsFinite(payload))
        return Status::InvalidArgument;
    if (commsThisTick_ >= maxCommsPerTick_)
        return Status::RateLimited;
    ++commsThisTick_;
    Intent intent;
    intent.type = IntentType::SendSignal;
    intent.source = self_;
    intent.a = channel;
    intent.b = code;
    intent.scalar = payload;
    intents_.Push(intent);
    return Status::Ok;
}

// ---- Tasks ----

Status GameApi::GetObjective(uint32_t objectiveId, int32_t& outState) {
    if (Status s = Enter(Capability::Tasks); s != Status::Ok)
        return s;
    if (objectives_ == nullptr)
        return Status::NotFound;
    return objectives_->Get(objectiveId, outState) ? Status::Ok : Status::NotFound;
}

Status GameApi::ReportProgress(uint32_t objectiveId, int32_t delta) {
    if (Status s = Enter(Capability::Tasks); s != Status::Ok)
        return s;
    Intent intent;
    intent.type = IntentType::ReportProgress;
    intent.source = self_;
    intent.a = objectiveId;
    intent.b = delta;
    intents_.Push(intent);
    return Status::Ok;
}

// ---- Log ----

Status GameApi::Log(int32_t level, const char* message, uint32_t length) {
    if (Status s = Enter(Capability::Log); s != Status::Ok)
        return s;
    if (length > 0 && message == nullptr)
        return Status::InvalidArgument;
    if (logsThisTick_ >= maxLogsPerTick_)
        return Status::RateLimited;
    ++logsThisTick_;

    std::string line(message, message + length);
    // %.*s: the guest's bytes are data, never a format string (no injection). The width is an int,
    // so clamp length to INT_MAX to avoid a negative (implementation-defined) precision.
    const int clampedLevel = std::clamp(level, 0, static_cast<int>(LogLevel::Fatal));
    const int width = length > static_cast<uint32_t>(std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max()
                                                                                      : static_cast<int>(length);
    Logger::Log(static_cast<LogLevel>(clampedLevel), "[guest] %.*s", width, line.c_str());

    if (logRingCapacity_ > 0) {
        logRing_.push_back(std::move(line));
        while (logRing_.size() > logRingCapacity_)
            logRing_.pop_front();
    }
    return Status::Ok;
}

}  // namespace Next::gameapi
