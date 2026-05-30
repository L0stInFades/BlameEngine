# ADR-0011: Player-language frontend — run C++/Rust in the sandbox via WebAssembly

- **Status**: Accepted
- **Date**: 2026-05-30
- **Relates to**: [ADR-0007](0007-game-api-contract.md) (Game API), [ADR-0008](0008-player-code-sandbox.md) (sandbox = a security boundary, backend-agnostic `ISandbox`)

## Context

The sandbox shipped with one backend — the bespoke `NBVM` bytecode VM (`RefVm`) — and the
[security audit](../security/sandbox-audit-2026-05-30.md) named the open *functional* gap plainly:
there is **no high-level language frontend**, so "player code" could only be hand-assembled
bytecode. Real players (and the HackOps `popen(python3)` probe we want to retire) need to write
real code in real languages and have it run, safely, against the authoritative world.

`ISandbox` was deliberately designed (ADR-0008) so a second backend could slot in without touching
callers. The question was only *which* path turns "C++/Rust" into "runs in the sandbox".

## Decision

**Compile player languages to WebAssembly (`wasm32`) and execute them in a WASM `ISandbox`
backend.** WASM is the lingua franca every serious systems language already targets — clang emits
it for C/C++, `rustc` has a first-class `wasm32-unknown-unknown` target — so one backend buys us
*all* of them. We do NOT write a C++/Rust frontend for the NBVM ISA (that would be a full compiler
backend, and NBVM is too small to host real programs).

Concretely:

- **`next_sandbox_wasm` / `Wasm3Sandbox`** — a second `ISandbox` backed by **wasm3** (a small,
  embeddable, interpreter-only WASM runtime; MIT). Optional, behind `BUILD_WITH_WASM` and pulled
  via FetchContent, exactly like the optional Jolt backend (ADR-0009). The headless core and CI
  never need it. (wasm3's top-level build drags in uvwasi/libuv; we pull only the interpreter
  library via `SOURCE_SUBDIR source`.)
- **One ABI, two backends.** A WASM guest imports a single function
  `env.host_call(i32 callId, i32 argsOff, i32 argsLen, i32 retOff, i32 retLen) -> i32` and exports
  `i32 run(i32 arg)`. `host_call` maps *exactly* onto the existing `HostGateway::Invoke` — same
  `CallId` table, same arg/ret memory windows, same capability gating, same `AbiDispatch`. So a
  guest written in C++ speaks the **identical Game API** as a hand-assembled NBVM guest; the
  gateway/facade is unchanged.
- **Same security checks at the seam.** The host-call trampoline re-derives `(base, size)` from the
  WASM linear memory and applies the same window bounds checks as `RefVm` before handing pointers
  to the gateway; WASM itself bounds-checks every guest memory access. The per-run host-call budget
  is enforced.

## Consequences

**Proven (this change).** `examples/wasm_guests` compiles two classic algorithms — **A\*** in
**C++23** and **binary search** in **Rust (edition 2024)** — to `wasm32` at build time, and
`tools/wasm_demo` runs them on `Wasm3Sandbox` against a live headless world: the C++ guest senses
obstacles through the Game API, plans, and its path length matches a host-side BFS to the step
(24, a forced detour); the Rust guest senses beacons and `core::slice::binary_search`es the sorted
map. The same `host_call` ABI carries both. This is the player-language frontend, working.

**Toolchain.** C++→wasm needs an LLVM clang with the `wasm32` target (NOT Apple clang) + `wasm-ld`;
Rust needs the `wasm32-unknown-unknown` target. CMake auto-detects and skips a guest if its
toolchain is absent, so the demo degrades gracefully.

**CPU fuel — RESOLVED by [ADR-0012](0012-wasm-fuel-gas-metering.md).** wasm3 has no native fuel
hook, so as a follow-up the backend now rewrites each module with **load-time gas instrumentation**:
`SandboxPolicy::fuel` is charged per metered block and the guest traps `FuelExhausted` when it runs
out (an infinite loop is bounded, not a hang), with exact `fuelUsed` reported. Memory safety,
capability gating, and the host-call budget were already enforced. `RefVm` remains the deterministic
reference backend.

**Determinism.** wasm3 is a pure interpreter (no JIT); the guests here use only the IEEE-754 basic
ops, so results are deterministic. The float-determinism build flags noted in the audit (F-2) apply
equally to any backend.

**Retiring `popen`.** With this in place, the HackOps policy loop's `popen(python3)` probe finally
has a real replacement path: compile the player's code to wasm and run it in `Wasm3Sandbox`.
