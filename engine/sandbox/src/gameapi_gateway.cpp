#include "next/sandbox/gameapi_gateway.h"

#include "next/gameapi/abi_dispatch.h"
#include "next/gameapi/game_api.h"

namespace Next::sandbox {

gameapi::Status GameApiGateway::Invoke(gameapi::CallId id, uint8_t* memoryBase, uint32_t /*memorySize*/,
                                       uint32_t argsOffset, uint32_t argsLen, uint32_t retOffset, uint32_t retLen,
                                       const gameapi::CapabilitySet& granted) {
    if (api_ == nullptr) {
        return gameapi::Status::Internal;
    }
    // Defense in depth: the policy's granted set is the outer ring; the GameApi facade re-checks
    // its own set inside the call. Unknown ids map to Capability::Count_ and are denied here.
    const gameapi::Capability cap = gameapi::CapabilityFor(id);
    if (cap == gameapi::Capability::Count_ || !granted.Has(cap)) {
        return gameapi::Status::PermissionDenied;
    }

    // The VM already bounds-checked these windows against the arena; turn them into host pointers.
    const void* args = (argsLen > 0) ? memoryBase + argsOffset : nullptr;
    void* ret = (retLen > 0) ? memoryBase + retOffset : nullptr;
    return gameapi::AbiDispatch::HostCall(*api_, id, args, argsLen, ret, retLen);
}

}  // namespace Next::sandbox
