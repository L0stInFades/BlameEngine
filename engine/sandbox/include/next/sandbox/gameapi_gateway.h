#pragma once

#include "next/sandbox/sandbox.h"

namespace Next::gameapi {
class GameApi;
}

namespace Next::sandbox {

// The concrete HostGateway that wires a sandboxed guest to the Game API (ADR-0008). It is the
// one place a guest's host-call becomes a real Game API call. Two things happen here:
//   1. defense-in-depth capability re-check against the policy's granted set (the GameApi facade
//      checks again, so the effective permission is the intersection — more restrictive wins);
//   2. the (already VM-bounds-checked) guest memory windows are handed to AbiDispatch.
// It performs NO other logic; argument semantics live in GameApi.
class GameApiGateway final : public HostGateway {
public:
    explicit GameApiGateway(gameapi::GameApi* api) : api_(api) {}

    gameapi::Status Invoke(gameapi::CallId id, uint8_t* memoryBase, uint32_t memorySize, uint32_t argsOffset,
                           uint32_t argsLen, uint32_t retOffset, uint32_t retLen,
                           const gameapi::CapabilitySet& granted) override;

private:
    gameapi::GameApi* api_;
};

}  // namespace Next::sandbox
