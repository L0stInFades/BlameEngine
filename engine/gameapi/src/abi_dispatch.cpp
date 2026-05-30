#include "next/gameapi/abi_dispatch.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "next/gameapi/game_api.h"

namespace Next::gameapi {
namespace {

// Alignment-safe POD codec: guest memory may be unaligned, so every transfer goes through
// memcpy (byte-wise). Reading fewer bytes than expected -> the arg blob is malformed.
template<typename T>
bool ReadArg(const void* args, uint32_t argsLen, T& out) {
    if (argsLen < sizeof(T)) {
        return false;
    }
    std::memcpy(&out, args, sizeof(T));
    return true;
}

template<typename T>
bool WriteRet(void* ret, uint32_t retLen, const T& in) {
    if (retLen < sizeof(T)) {
        return false;
    }
    std::memcpy(ret, &in, sizeof(T));
    return true;
}

// Encode an entity-list result (QueryByTag / SenseRadius) into [header | EntityId...]. The facade
// has already written up to `capacity` ids into `tmp` and set `count` to the total matches.
Status EncodeEntityList(void* ret, uint32_t retLen, const std::vector<EntityId>& tmp, uint32_t count, uint32_t capacity,
                        Status facadeStatus) {
    if (facadeStatus != Status::Ok && facadeStatus != Status::BufferTooSmall) {
        return facadeStatus;
    }
    if (retLen < sizeof(EntityListHeader)) {
        return Status::BufferTooSmall;
    }
    const EntityListHeader header{count, capacity};
    std::memcpy(ret, &header, sizeof(header));
    const uint32_t writeN = std::min(capacity, count);
    if (writeN > 0) {
        std::memcpy(static_cast<uint8_t*>(ret) + sizeof(header), tmp.data(), writeN * sizeof(EntityId));
    }
    return facadeStatus;
}

uint32_t EntityListCapacity(uint32_t retLen) {
    if (retLen < sizeof(EntityListHeader)) {
        return 0;
    }
    return (retLen - static_cast<uint32_t>(sizeof(EntityListHeader))) / static_cast<uint32_t>(sizeof(EntityId));
}

}  // namespace

Status AbiDispatch::HostCall(GameApi& api, CallId id, const void* args, uint32_t argsLen, void* ret, uint32_t retLen) {
    switch (id) {
        case CallId::GetTick: {
            uint64_t tick = 0;
            const Status s = api.GetTick(tick);
            if (s != Status::Ok)
                return s;
            return WriteRet(ret, retLen, TickResult{tick}) ? Status::Ok : Status::BufferTooSmall;
        }
        case CallId::GetTimeSeconds: {
            double seconds = 0.0;
            const Status s = api.GetTimeSeconds(seconds);
            if (s != Status::Ok)
                return s;
            return WriteRet(ret, retLen, SecondsResult{seconds}) ? Status::Ok : Status::BufferTooSmall;
        }
        case CallId::Self: {
            EntityId e = kInvalidEntity;
            const Status s = api.GetSelf(e);
            if (s != Status::Ok)
                return s;
            return WriteRet(ret, retLen, EntityResult{e}) ? Status::Ok : Status::BufferTooSmall;
        }
        case CallId::IsValid: {
            EntityArg a{};
            if (!ReadArg(args, argsLen, a))
                return Status::InvalidArgument;
            bool valid = false;
            const Status s = api.IsValid(a.entity, valid);
            if (s != Status::Ok)
                return s;
            return WriteRet(ret, retLen, BoolResult{valid ? 1u : 0u}) ? Status::Ok : Status::BufferTooSmall;
        }
        case CallId::GetPosition: {
            EntityArg a{};
            if (!ReadArg(args, argsLen, a))
                return Status::InvalidArgument;
            Vec3Abi pos{};
            const Status s = api.GetPosition(a.entity, pos);
            if (s != Status::Ok)
                return s;
            return WriteRet(ret, retLen, PositionResult{pos}) ? Status::Ok : Status::BufferTooSmall;
        }
        case CallId::QueryByTag: {
            QueryByTagArgs a{};
            if (!ReadArg(args, argsLen, a))
                return Status::InvalidArgument;
            const uint32_t capacity = EntityListCapacity(retLen);
            std::vector<EntityId> tmp(capacity);
            uint32_t count = 0;
            const Status s = api.QueryByTag(a.tag, capacity ? tmp.data() : nullptr, capacity, count);
            return EncodeEntityList(ret, retLen, tmp, count, capacity, s);
        }
        case CallId::SenseRadius: {
            SenseRadiusArgs a{};
            if (!ReadArg(args, argsLen, a))
                return Status::InvalidArgument;
            const uint32_t capacity = EntityListCapacity(retLen);
            std::vector<EntityId> tmp(capacity);
            uint32_t count = 0;
            const Status s = api.SenseRadius(a.radius, capacity ? tmp.data() : nullptr, capacity, count);
            return EncodeEntityList(ret, retLen, tmp, count, capacity, s);
        }
        case CallId::SenseNearest: {
            SenseNearestArgs a{};
            if (!ReadArg(args, argsLen, a))
                return Status::InvalidArgument;
            SenseNearestResult r{kInvalidEntity, 0.0f};
            const Status s = api.SenseNearest(a.radius, a.tag, r.entity, r.distance);
            if (s != Status::Ok)
                return s;
            return WriteRet(ret, retLen, r) ? Status::Ok : Status::BufferTooSmall;
        }
        case CallId::MoveTo: {
            MoveToArgs a{};
            if (!ReadArg(args, argsLen, a))
                return Status::InvalidArgument;
            return api.MoveTo(a.target, a.maxSpeed);
        }
        case CallId::Stop: {
            return api.Stop();
        }
        case CallId::SetActionFlag: {
            SetActionFlagArgs a{};
            if (!ReadArg(args, argsLen, a))
                return Status::InvalidArgument;
            return api.SetActionFlag(a.action, a.on != 0);
        }
        case CallId::SendSignal: {
            SendSignalArgs a{};
            if (!ReadArg(args, argsLen, a))
                return Status::InvalidArgument;
            return api.SendSignal(a.channel, a.code, a.payload);
        }
        case CallId::GetObjective: {
            ObjectiveArg a{};
            if (!ReadArg(args, argsLen, a))
                return Status::InvalidArgument;
            ObjectiveResult r{0};
            const Status s = api.GetObjective(a.objectiveId, r.state);
            if (s != Status::Ok)
                return s;
            return WriteRet(ret, retLen, r) ? Status::Ok : Status::BufferTooSmall;
        }
        case CallId::ReportProgress: {
            ReportProgressArgs a{};
            if (!ReadArg(args, argsLen, a))
                return Status::InvalidArgument;
            return api.ReportProgress(a.objectiveId, a.delta);
        }
        case CallId::Log: {
            LogArgs a{};
            if (!ReadArg(args, argsLen, a))
                return Status::InvalidArgument;
            // The message bytes follow the header inside the same args blob; never read past it.
            if (a.len > argsLen - static_cast<uint32_t>(sizeof(LogArgs))) {
                return Status::InvalidArgument;
            }
            const char* msg = static_cast<const char*>(args) + sizeof(LogArgs);
            return api.Log(a.level, msg, a.len);
        }
        case CallId::Raycast: {
            RaycastArgs a{};
            if (!ReadArg(args, argsLen, a))
                return Status::InvalidArgument;
            RaycastResult r{};
            const Status s = api.Raycast(a.origin, a.direction, a.maxDistance, r);
            if (s != Status::Ok)
                return s;
            return WriteRet(ret, retLen, r) ? Status::Ok : Status::BufferTooSmall;
        }
        case CallId::Count_:
            break;
    }
    return Status::Unsupported;  // unknown / non-callable id
}

}  // namespace Next::gameapi
