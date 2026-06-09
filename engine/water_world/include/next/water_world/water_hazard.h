#pragma once

#include <cstdint>

#include "next/boundary/transport.h"
#include "next/gameapi/sim_clock.h"
#include "next/runtime/system.h"
#include "next/water_world/water_store.h"

// Water x electronics hazard (ADR-0015 W11). The water system already tells gameplay where conductive
// water is (WaterConductive + submersion); this is the consumer that ACTS on it: an electronic device
// submerged in conductive water SHORTS OUT — it stops functioning (a hacking target goes dark, a camera
// dies, a powered door fails). It is the gameplay counterpart of the stealth (breaks-sight) hook.
//
// Authority model: the device's ElectronicComponent is the authoritative state (functional / shortedOut).
// The short is LATCHED (a fried board does not heal when the water drains) and EDGE-TRIGGERED (one event
// per device), so it is deterministic and replay-safe. Repairing a device (clearing shortedOut) is a
// separate gameplay/repair concern. A cosmetic 'WSHT' GameEvent is forwarded for UE5 sparks/smoke.

namespace Next::water {

// Marks an entity as a powered electronic device the water hazard can disable. A fuller hacking/device
// system would own/extend this (power draw, hack difficulty, ...); here it is the minimal authoritative
// state the short-circuit acts on.
struct ElectronicComponent {
    bool functional = true;   // currently operational (false once shorted, until repaired)
    bool shortedOut = false;  // LATCHED: has been shorted by conductive water at least once
};

// ECS system: each tick, any functional device standing in conductive, submerged water is shorted out.
// Reads the authoritative WaterStore at the shared SimClock time (so it agrees with buoyancy/queries),
// reads each device's TransformComponent position, and writes only its ElectronicComponent (no Transform
// write — the single-writer invariant is untouched). Register anywhere after streaming has synced water.
class WaterHazardSystem : public Next::System {
public:
    explicit WaterHazardSystem(const WaterStore* store, const Next::gameapi::SimClock* clock = nullptr,
                               Next::boundary::ISnapshotTransport* transport = nullptr)
        : store_(store), clock_(clock), transport_(transport) {}

    void Update(float deltaTime) override;
    const char* GetName() const override { return "WaterHazardSystem"; }

    // Cumulative number of devices shorted out since construction (one per device; diagnostics/tests).
    uint64_t TotalShortedOut() const { return totalShortedOut_; }

private:
    const WaterStore* store_;
    const Next::gameapi::SimClock* clock_;
    Next::boundary::ISnapshotTransport* transport_;
    uint64_t totalShortedOut_ = 0;
};

}  // namespace Next::water
