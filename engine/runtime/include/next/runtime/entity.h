#pragma once

#include <cstdint>
#include <functional>

namespace Next {

// Entity ID with version to avoid reuse issues
struct Entity {
    uint64_t id : 48;       // Entity index
    uint64_t version : 16;  // Version number

    Entity() : id(0), version(0) {}
    explicit Entity(uint64_t i, uint64_t v = 0) : id(i), version(v) {}

    bool IsValid() const { return id != 0 && version != 0; }

    bool operator==(const Entity& other) const { return id == other.id && version == other.version; }

    bool operator!=(const Entity& other) const { return !(*this == other); }

    // Pack to / unpack from the 64-bit form (version in the high 16 bits, index in the low 48).
    // This is the single source of truth for the layout; consumers that bridge to a uint64 id
    // (Game API, sim↔UE5 boundary) round-trip through these instead of open-coding the shift.
    operator uint64_t() const { return (uint64_t)version << 48 | id; }

    static Entity FromPacked(uint64_t packed) { return Entity(packed & 0x0000'FFFF'FFFF'FFFFull, packed >> 48); }

    static Entity Invalid() { return Entity(); }
};

const Entity INVALID_ENTITY = Entity::Invalid();

// Entity hash for unordered_map
struct EntityHash {
    size_t operator()(const Entity& e) const { return std::hash<uint64_t>()(static_cast<uint64_t>(e)); }
};

}  // namespace Next
