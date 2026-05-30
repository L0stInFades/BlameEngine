#pragma once

#include <cstdint>

#include "next/gameapi/abi.h"

namespace Next::gameapi {

// A queryable gameplay-tag bitset attached to entities the sim wants exposed to QueryByTag /
// SenseNearest. Tag indices are 0..kMaxTagIndex; each is one bit. Data-oriented: the column is
// scanned directly by the Observe/Sense calls.
struct GameTag {
    uint64_t bits = 0;

    void Set(uint32_t tagIndex) {
        if (tagIndex <= kMaxTagIndex) {
            bits |= (uint64_t{1} << tagIndex);
        }
    }
    void Clear(uint32_t tagIndex) {
        if (tagIndex <= kMaxTagIndex) {
            bits &= ~(uint64_t{1} << tagIndex);
        }
    }
    bool Has(uint32_t tagIndex) const { return tagIndex <= kMaxTagIndex && (bits & (uint64_t{1} << tagIndex)) != 0; }
};

// Persistent movement goal set by the Actuate domain. The kinematic step (StepKinematics)
// advances the entity's TransformComponent toward `target` at up to `maxSpeed` units/sec while
// `active`. A MoveTo intent sets it; a Stop intent clears `active`. Keeping the goal as world
// state (not a per-tick command) lets a guest issue MoveTo once and lets the motion be replayed.
struct MoveTarget {
    Vec3Abi target{};
    float maxSpeed = 0.0f;
    uint8_t active = 0;
};

// Generic per-entity action bitset toggled by SetActionFlag (e.g. "crouch", "hack-in-progress").
struct ActionFlags {
    uint32_t bits = 0;
};

}  // namespace Next::gameapi
