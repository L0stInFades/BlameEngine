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
- The policy loop still uses direct host Python execution. The production path
  still needs `engine/sandbox`, resource budgets, and a proper World API bridge.

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
