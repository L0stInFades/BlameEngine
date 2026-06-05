#pragma once

#include <cstdint>

#include "next/boundary/transport.h"
#include "next/gameapi/sim_clock.h"
#include "next/runtime/system.h"
#include "next/water_world/water_store.h"

// Swim / drown / oxygen (ADR-0015 W12). The downstream gameplay consumer of submersion: a character
// whose HEAD is underwater holds its breath (oxygen drains); when the air runs out it drowns (health
// drains) and eventually dies; surfacing refills the lungs. This is the human-facing counterpart of the
// electronics hazard (W11) and reuses the same authoritative submersion the buoyancy sim uses.
//
// "Swim" locomotion is NOT re-implemented here: a submerged character already floats (WaterForceSystem
// buoyancy) and moves by MoveTo intents (ActuationSystem); SwimmerComponent.submerged is the state flag
// gameplay/animation reads to switch to a swim gait. This system owns ONLY the oxygen/drown authority.
//
// The "head underwater" test uses headOffset so a character WADING in shallow water (body wet, head dry)
// breathes normally — only true submersion of the breathing point depletes air. Latched death is
// edge-triggered (one 'WDRN' event); all rates are fixed -> deterministic / replay-safe.

namespace Next::water {

// Per-character swimming/breathing state. Attach to a controllable character; the system updates the
// transient fields (submerged / drowning / dead) and the resources (oxygen / health).
struct SwimmerComponent {
    float oxygen = 20.0f;     // seconds of air remaining (starts full)
    float maxOxygen = 20.0f;  // lung capacity in seconds
    float health = 100.0f;    // drowning damage pool; death at <= 0
    float headOffset = 0.6f;  // breathing point height above the entity origin (meters)
    bool submerged = false;   // system-updated: the head (origin + headOffset) is below the surface
    bool drowning = false;    // system-updated: out of air and taking damage this tick
    bool dead = false;        // system-updated: LATCHED once health reaches 0
};

class SwimSystem : public Next::System {
public:
    // oxygenRecoveryPerSec: air refilled per second while breathing. drownDamagePerSec: health lost per
    // second while out of air and submerged. Defaults: a 20 s breath, ~4 s to refill, 10 s to drown out.
    explicit SwimSystem(const WaterStore* store, const Next::gameapi::SimClock* clock = nullptr,
                        Next::boundary::ISnapshotTransport* transport = nullptr, float oxygenRecoveryPerSec = 5.0f,
                        float drownDamagePerSec = 10.0f)
        : store_(store),
          clock_(clock),
          transport_(transport),
          // Clamp tuning rates to >= 0 so a negative value can never INVERT the mechanic (gain air while
          // drowning / heal by suffocating).
          oxygenRecoveryPerSec_(oxygenRecoveryPerSec > 0.0f ? oxygenRecoveryPerSec : 0.0f),
          drownDamagePerSec_(drownDamagePerSec > 0.0f ? drownDamagePerSec : 0.0f) {}

    void Update(float deltaTime) override;
    const char* GetName() const override { return "SwimSystem"; }

    // Cumulative drownings (deaths) since construction (diagnostics/tests).
    uint64_t TotalDrownings() const { return totalDrownings_; }

private:
    const WaterStore* store_;
    const Next::gameapi::SimClock* clock_;
    Next::boundary::ISnapshotTransport* transport_;
    float oxygenRecoveryPerSec_;
    float drownDamagePerSec_;
    uint64_t totalDrownings_ = 0;
};

}  // namespace Next::water
