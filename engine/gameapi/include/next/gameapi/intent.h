#pragma once

#include <cstdint>
#include <vector>

#include "next/gameapi/abi.h"

// Write-as-intent model (ADR-0007). No Game API write touches the world directly. Each write
// records a validated Intent here; the sim drains the queue at the tick boundary and applies
// intents in record order via an IntentResolver. This is what makes the contract deterministic,
// server-authoritative, and replay/anti-cheat friendly.

namespace Next::gameapi {

enum class IntentType : uint32_t {
    MoveTo,          // move `source` toward `target` capped at `scalar` (maxSpeed) units/sec
    Stop,            // cancel motion for `source`
    SetActionFlag,   // set/clear action bit `a` to `b` on `source`
    SendSignal,      // emit signal: channel `a`, code `b`, payload `scalar`
    ReportProgress,  // advance objective `a` by `b`
};

// A single recorded intent. Kept as one flat POD (no union) for clarity and safety; the fields
// used depend on `type` (documented per IntentType above).
struct Intent {
    IntentType type = IntentType::Stop;
    EntityId source = kInvalidEntity;
    Vec3Abi vec{};     // MoveTo target
    float scalar = 0;  // maxSpeed / signal payload
    uint32_t a = 0;    // action bit / channel / objectiveId
    int32_t b = 0;     // action on-off / signal code / progress delta
};

// Per-context intent buffer. Bounded indirectly by the per-tick host-call quota.
class IntentQueue {
public:
    void Push(const Intent& intent) { intents_.push_back(intent); }
    const std::vector<Intent>& Items() const { return intents_; }
    bool Empty() const { return intents_.empty(); }
    size_t Size() const { return intents_.size(); }
    void Clear() { intents_.clear(); }

private:
    std::vector<Intent> intents_;
};

}  // namespace Next::gameapi
