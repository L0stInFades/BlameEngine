# ADR-0012: CPU-fuel metering for the WASM sandbox via load-time gas instrumentation

- **Status**: Accepted
- **Date**: 2026-05-30
- **Relates to / refines**: [ADR-0008](0008-player-code-sandbox.md) (fuel is red-line #3), [ADR-0011](0011-wasm-language-frontend.md) (the WASM backend; this closes its one documented gap)

## Context

The WASM backend (ADR-0011) ran real C++/Rust but had one hole: **wasm3 has no native CPU-fuel
hook**, so a guest with an infinite loop would hang the host and `SandboxPolicy::fuel` was ignored
on that backend (`fuelUsed` was hardcoded 0). For untrusted player code on a server this is a DoS
hole — fuel metering is ADR-0008 red line #3. Two ways to close it: (A) a wasmtime C-API backend
with native `consume_fuel`, or (B) load-time **gas instrumentation** of the wasm bytes, keeping
wasm3.

## Decision

**Option B — instrument the module at load time, on the existing wasm3 backend.** A multi-agent
design pass weighed both; B wins on every constraint that matters here:

- **Reuses the existing fuel currency.** The transform charges the SAME `SandboxPolicy::fuel` with
  the same "runs to zero → `FuelExhausted`" semantics as the NBVM `RefVm` reference, so fuel means
  one thing across both backends. wasmtime's native fuel is a separate, incomparable unit.
- **No new dependency, deterministic, offline.** The transform is our own C++ over the `.wasm`
  bytes the backend already holds; wasm3 stays a tiny, source-built, pure interpreter. wasmtime is
  a ~15 MB prebuilt Cranelift JIT pinned per platform/arch — a code generator inside the
  untrusted-code boundary, and a heavier supply chain.
- **Closes the gap rather than adding a third backend.** Backend matrix stays {RefVm, wasm3}.

## How it works (`engine/sandbox/wasm_meter.{h,cpp}`)

Append-only, **no reindexing** of existing functions/globals. The pass grows five sections in place:
append a func type `(i32)->()`, a mutable i64 global exported as `__fuel`, and a `__gas(i32 cost)`
function that does `fuel -= cost; if (fuel < 0) unreachable`; then splice `i32.const <cost>; call
__gas` at **function entry and every loop / if / else header**, with `cost` = the block's
instruction count (floored at 1). Because every backward branch in structured WASM targets a `loop`
header and every call re-enters a metered function entry, **no unbounded computation can escape a
metered point** — total executed work is bounded by the budget. The host loads the budget into
`__fuel` via `m3_SetGlobal` before the call and reads the remainder via `m3_GetGlobal` after (the
global survives a trap), yielding exact `fuelUsed`; an `unreachable` with `__fuel <= 0` maps to
`FuelExhausted`. Unknown/unsupported opcodes (SIMD/atomics) are **rejected** (fail-closed), never
run un-metered. (`m3_ParseModule` aliases the buffer, so the instrumented bytes are kept in the
backend instance for the runtime's lifetime.)

## Consequences

- **Proven** (`tools/wasm_demo`, behind `BUILD_WITH_WASM`): an infinite-loop guest now traps
  `FuelExhausted` instead of hanging; a counted loop completes under an ample budget (exact
  `fuelUsed` reported, scaling with work) and traps under a tight one; the real A* (C++) and
  binary-search (Rust) guests round-trip through the instrumentation unchanged and still pass.
  ASan-clean. Default trunk unaffected (wasm opt-in; headless 17/17).
- **Cost model is the contract**, not bit-identical RefVm counts: fuel is charged per metered block
  (function entry + loop/if/else headers). It is a faithful CPU bound; document it as such.
- **Approximations**: dynamic-length ops (`memory.fill/copy`) count as one instruction (bounded by
  the guest's page-limited memory, so not an unbounded escape); straight-line code inside a nested
  plain `block` after an `end` is charged to the enclosing region — total work stays bounded by
  `fuel × max-block-size`.
- **wasm3 + UBSan**: wasm3's indirect-call trampoline trips `-fsanitize=function` (a benign
  third-party artifact); the wasm backend stays opt-in and out of the default CI sanitizer matrix,
  like the Jolt backend. ASan (memory safety) of the meter is clean.
- **Float determinism** (ADR-0011 F-2) is orthogonal and still applies; gas metering neither fixes
  nor worsens it.

A future wasmtime backend remains possible behind the same `ISandbox` if JIT speed is ever needed;
it is no longer required for safety.
