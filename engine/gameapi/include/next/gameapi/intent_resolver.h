#pragma once

#include <cstdint>
#include <vector>

#include "next/gameapi/abi.h"
#include "next/gameapi/intent.h"
#include "next/gameapi/objective_store.h"

namespace Next {
class World;
}

namespace Next::gameapi {

// A signal emitted by the Comms domain, surfaced to the sim after intents are applied.
struct Signal {
    EntityId source = kInvalidEntity;
    uint32_t channel = 0;
    int32_t code = 0;
    float payload = 0.0f;
};

// Consumes a context's recorded intents and applies them to the authoritative world at the tick
// boundary, in record order (ADR-0007). This is the ONLY place player/agent writes become world
// state. Intents naming a now-invalid entity are skipped (graceful: the entity may have been
// destroyed between recording and application).
class IntentResolver {
public:
    virtual ~IntentResolver() = default;
    virtual void Apply(World& world, const std::vector<Intent>& intents) = 0;
};

// Reference resolver: MoveTo/Stop drive a persistent MoveTarget component, SetActionFlag toggles
// ActionFlags, ReportProgress advances the objective store, SendSignal is collected for the sim.
// Games may substitute their own rules; this one is enough to run a headless slice end to end.
class DefaultIntentResolver : public IntentResolver {
public:
    explicit DefaultIntentResolver(ObjectiveStore* objectives = nullptr) : objectives_(objectives) {}

    void Apply(World& world, const std::vector<Intent>& intents) override;

    // Advance kinematics one fixed step: every entity with an active MoveTarget and a transform
    // moves toward its target by up to maxSpeed*dt; on arrival it snaps and deactivates.
    static void StepKinematics(World& world, double dt);

    // Signals emitted during the most recent Apply(); the sim drains then clears these.
    const std::vector<Signal>& EmittedSignals() const { return signals_; }
    void ClearSignals() { signals_.clear(); }

private:
    ObjectiveStore* objectives_;
    std::vector<Signal> signals_;
};

}  // namespace Next::gameapi
