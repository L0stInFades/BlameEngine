# HackOps Technology Prep

HackOps is an experimental game target for validating real-code gameplay on top
of NEXT without turning NEXT into a hack-game-only engine.

## First Spike: NvimSurface

The first executable research artifact is:

```text
tools/nvim_surface_probe/
```

It validates a core product bet: NEXT can render real Neovim as an embedded UI
instead of building a fake terminal/editor.

The probe:

- starts `nvim --embed`
- calls `nvim_ui_attach`
- consumes `ext_linegrid` redraw events
- maintains a text grid
- accepts basic Neovim input
- writes a snapshot for verification

The second artifact moves the same idea into C++:

```text
engine/terminal/
tools/nvim_surface_cpp_probe/
```

`next_terminal` now provides a minimal `NvimSurface` API for launching Neovim,
attaching to the external UI, consuming Msgpack-RPC redraw events, maintaining a
main `ext_linegrid`, sending input, and producing snapshots for future renderer
integration.

Verified locally:

- `next_terminal` builds as a CMake target.
- `next_nvim_surface_probe` builds as a CMake target.
- The C++ probe opens `sample_policy.py` with the user's real Neovim/LazyVim
  config and writes a snapshot.
- The C++ probe can edit and save a `/tmp` copy through `nvim_input`.

## Current Spike: Minimal Policy Loop

The first game target is:

```text
game/hackops/
```

`hackops_demo` keeps the loop deliberately small:

- opens a Python policy file in `NvimSurface`
- writes a terminal snapshot for smoke verification
- executes the policy with the host Python runtime
- maps the printed score to a simulated world order state

Run it through the terminal preset:

```bash
cmake --preset terminal-dev
cmake --build --preset terminal-dev
out/build/terminal-dev/bin/hackops_demo \
  --policy tools/nvim_surface_probe/sample_policy.py \
  --snapshot /tmp/hackops-policy-snapshot.txt
```

Current limitations:

- POSIX process launch is verified locally; Windows `CreateProcess` launch code
  is present but needs verification on a Windows machine.
- Main-grid text rendering works; floating grids, external cmdline UI,
  highlight/style tables, mouse, IME, and renderer integration are not complete.
- The policy loop still uses direct host Python execution (`popen`), which has
  **zero isolation** — it is a terminal/UX smoke test, NOT the production path.
  The production path now exists: `engine/gameapi` (the capability-domained Game
  API, ADR-0007), `engine/sandbox` (the security-first player-code runtime with a
  deterministic fuel-metered VM, ADR-0008), and `engine/boundary` (the sim↔UE5
  snapshot stream, ADR-0006). The headless vertical slice
  (`tests/integration/test_vertical_slice.cpp`) runs sandboxed "player code"
  through the Game API into the authoritative world and out to a view snapshot.
  The player-language frontend now exists too: `next_sandbox_wasm` (ADR-0011)
  runs real C++/Rust compiled to wasm32 on the same Game API ABI — classic
  algorithms (A*, binary search) already run in a headless world via
  `tools/wasm_demo`. What remains to retire `popen` for HackOps is the wiring:
  compile the Neovim-edited policy to wasm and run it in `Wasm3Sandbox` instead of
  a host Python process. CPU fuel for untrusted code is already enforced (load-time
  gas instrumentation, ADR-0012), so this is now wiring, not missing infrastructure.

## Why This Matters

If this path works, the production engine can use:

```text
NvimSurface
  -> nvim --embed
  -> LazyVim / LSP / diagnostics
  -> NEXT renderer and input
  -> Ops Workspace
  -> World API
```

That keeps real editing behavior while avoiding a full Linux VM as the default
runtime.
