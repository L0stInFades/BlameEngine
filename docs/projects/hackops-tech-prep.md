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

Current limitations:

- POSIX process launch is verified locally; Windows `CreateProcess` launch code
  is present but needs verification on a Windows machine.
- Main-grid text rendering works; floating grids, external cmdline UI,
  highlight/style tables, mouse, IME, and renderer integration are not complete.

## Second Spike: OpsWorkspace

The second executable research artifact is:

```text
engine/ops/
game/hackops/
```

It validates the next product bet: HackOps needs a controlled player workspace
owned by the engine, not raw access to arbitrary host directories.

The workspace currently seeds:

- `ops.toml`
- `policy.py`
- `orders.log`
- `city_graph.json`
- `README.md`

The headless game target:

```text
hackops_demo
```

can reset the workspace, create a snapshot, and list task files without starting
the renderer. This keeps the HackOps line buildable on macOS/Linux CI while the
existing song/editor path remains Windows/DX12-first.

Verified locally and in CI:

- `cmake --preset terminal-dev`
- `cmake --build --preset terminal-dev`
- `hackops_demo --reset --workspace /tmp/next-hackops-ci --snapshot ci --list`

Next steps:

- Add `PythonWorker` to run `policy.py` in an isolated process.
- Add `run_sim` to consume worker output and produce a route/risk result.
- Feed the generated workspace into `NvimSurface` so the editor opens the actual
  task files.

## Third Spike: PythonWorker

The third executable research artifact is:

```text
engine/ops/include/next/ops/python_worker.h
engine/ops/src/python_worker.cpp
```

It validates the execution boundary for player Python scripts. The game no
longer shells out manually from CI; `hackops_demo --run-policy` asks
`PythonWorker` to run `policy.py` in the generated workspace, capture stdout,
stderr, exit code, timeout state, and duration.

Verified locally and in CI:

- `hackops_demo --reset --workspace /tmp/next-hackops-ci --snapshot ci --run-policy --list`
- A failing `policy.py` exits through the worker with `policy_exit=7` and a
  non-zero `hackops_demo` process exit.
- Worker stdout contains the selected route JSON:

```json
{"order_id": "order.demo", "route_id": "route.loading-docks"}
```

Next steps:

- Convert worker stdout into a typed `PolicyResult`.
- Add `run_sim` that scores witness risk and route consequences.
- Add timeout fixtures so infinite-loop player code is part of the test matrix.

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
