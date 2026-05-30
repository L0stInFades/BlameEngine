#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Load-time WASM gas metering (ADR-0011 follow-up). wasm3 has no native CPU-fuel hook, so we make
// the module meter ITSELF: an append-only binary transform rewrites a wasm32 module to charge an
// i64 budget and trap (`unreachable`) when it runs out. This closes the one gap of the wasm3
// backend — `SandboxPolicy::fuel` becomes enforceable for untrusted C++/Rust, with the same
// "fuel runs to zero -> FuelExhausted" semantics as the NBVM RefVm reference.
//
// Append-only (no reindexing of existing funcs/globals): we only grow the type/function/global/
// export/code sections in place — append a func type (i32)->(), a mutable i64 fuel global exported
// as "__fuel", and a `__gas(i32 cost)` function that does `fuel -= cost; if (fuel < 0) unreachable`,
// then splice `i32.const <cost>; call __gas` at every function entry and every loop / if / else
// header. Because every backward branch in structured WASM targets a `loop` header and every call
// re-enters a metered function entry, no unbounded computation can escape a metered point — total
// executed work is bounded by `fuel`. Charges are floored at 1 so even an empty loop body costs.
//
// Fail-closed: a module using an opcode the walker does not understand (SIMD/atomics/etc.) is
// REJECTED rather than run un-metered.

namespace Next::sandbox {

// The exported name of the injected mutable i64 fuel global. The host sets the per-run budget into
// it (m3_SetGlobal) before calling and reads the remainder (m3_GetGlobal) after, even across a trap.
inline constexpr const char* kFuelGlobalName = "__fuel";

// Rewrite `in` (a raw wasm32 module) into `out` with gas metering. Returns false and fills `error`
// if the module cannot be safely instrumented (missing a required section, malformed, or an
// unsupported opcode). On success `out` is a valid wasm module that traps when its __fuel global,
// charged per metered block, goes negative.
bool InstrumentWasmForFuel(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::string* error);

}  // namespace Next::sandbox
