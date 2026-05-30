#pragma once

#include <cstdint>

// Capability domains for the Game API (ADR-0007). A caller (player sandbox, AI agent,
// or engine system) holds a CapabilitySet; the GameApi facade enforces it at every call
// entry, and the sandbox HostGateway re-checks it (defense in depth). Capabilities are a
// compile-time bitset: zero allocation, statically auditable, no runtime string permissions.

namespace Next::gameapi {

enum class Capability : uint32_t {
    Observe = 0,    // read world state (transforms, validity, tag queries)
    Sense,          // perception queries (radius / nearest) relative to the controlled entity
    Actuate,        // issue movement / action intents for the controlled entity
    Comms,          // send bounded signals on a channel
    Tasks,          // read task/objective state and report progress
    Time,           // read the deterministic sim clock (tick / seconds)
    Log,            // rate-limited diagnostic logging
    SpawnEntities,  // privileged: create/destroy entities (not granted to player code by default)

    Count_  // sentinel; must be last and <= 32
};

class CapabilitySet {
public:
    constexpr CapabilitySet() = default;

    constexpr CapabilitySet& Grant(Capability c) {
        bits_ |= Bit(c);
        return *this;
    }
    constexpr CapabilitySet& Revoke(Capability c) {
        bits_ &= ~Bit(c);
        return *this;
    }
    constexpr bool Has(Capability c) const { return (bits_ & Bit(c)) != 0; }

    constexpr uint32_t Bits() const { return bits_; }
    constexpr bool operator==(const CapabilitySet& o) const { return bits_ == o.bits_; }

    // No capabilities: a caller that can do nothing until explicitly granted.
    static constexpr CapabilitySet None() { return CapabilitySet{}; }

    // The default surface granted to player code: everything an agent needs to observe,
    // sense, act, communicate, advance tasks, read time, and log — but NOT spawn entities.
    static constexpr CapabilitySet PlayerDefault() {
        CapabilitySet s;
        s.Grant(Capability::Observe)
            .Grant(Capability::Sense)
            .Grant(Capability::Actuate)
            .Grant(Capability::Comms)
            .Grant(Capability::Tasks)
            .Grant(Capability::Time)
            .Grant(Capability::Log);
        return s;
    }

    // The read-only surface for an AI co-pilot that observes and advises but does not act.
    static constexpr CapabilitySet ObserverOnly() {
        CapabilitySet s;
        s.Grant(Capability::Observe).Grant(Capability::Sense).Grant(Capability::Tasks).Grant(Capability::Time);
        return s;
    }

private:
    static constexpr uint32_t Bit(Capability c) { return 1u << static_cast<uint32_t>(c); }
    uint32_t bits_ = 0;
};

static_assert(static_cast<uint32_t>(Capability::Count_) <= 32,
              "CapabilitySet is a 32-bit set; add a wider set before exceeding 32 capabilities");

}  // namespace Next::gameapi
