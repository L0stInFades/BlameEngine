#pragma once

#include <cstdint>

#include "next/gameapi/abi.h"

namespace Next::gameapi {

class GameApi;

// The thin POD adapter over GameApi (ADR-0007): decode the flat args blob for `id`, call the
// typed facade, and encode the result into the ret blob. This is what a sandbox HostGateway
// invokes once it has bounds-checked the guest's args/ret memory ranges. There is NO logic here
// beyond decode/encode — capability and argument validation live in GameApi.
//
// `args`/`ret` are host pointers into already-bounds-checked memory. Unknown ids -> Unsupported.
namespace AbiDispatch {

Status HostCall(GameApi& api, CallId id, const void* args, uint32_t argsLen, void* ret, uint32_t retLen);

}  // namespace AbiDispatch

}  // namespace Next::gameapi
