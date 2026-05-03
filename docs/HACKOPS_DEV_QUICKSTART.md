# HackOps Development Quickstart

HackOps development is isolated from the current Windows/DX12 song project. Use
the terminal preset when working on real-code gameplay systems, Neovim
embedding, Ops runtime, worker processes, or CTF tooling.

## Terminal / HackOps Track

Prerequisites:

- CMake 3.20+
- C++17 compiler
- Neovim on `PATH`

Configure and build:

```bash
cmake --preset terminal-dev
cmake --build --preset terminal-dev
```

Run the C++ Neovim probe:

```bash
out/build/terminal-dev/bin/next_nvim_surface_probe \
  --clean \
  --file tools/nvim_surface_probe/sample_policy.py \
  --snapshot /tmp/nvim-surface-cpp.txt
```

The `--clean` flag keeps CI and new developer machines independent from a
personal LazyVim setup. Omit it locally when validating the real user
configuration.

Run the HackOps headless workspace smoke:

```bash
out/build/terminal-dev/bin/hackops_demo \
  --reset \
  --workspace /tmp/next-hackops-maintenance-window \
  --snapshot smoke \
  --run-policy \
  --run-sim \
  --list
```

## Windows Song / Editor Track

The existing song/editor track is still Windows/DX12-first:

```powershell
cmake --preset windows-dx12-dev
cmake --build --preset windows-dx12-dev
ctest --test-dir out/build/windows-dx12-dev -C Debug --output-on-failure
```

## Current Module Boundary

- `engine/terminal`: real Neovim external UI integration.
- `engine/ops`: controlled workspaces and policy simulation for real-code gameplay.
- `game/hackops`: headless HackOps target for development and CI.
- `PythonWorker`: isolated process runner for workspace `policy.py` scripts.
- `tools/nvim_surface_cpp_probe`: command-line smoke test for `NvimSurface`.
- `tools/nvim_surface_probe`: Python reference spike.
- `docs/projects/hackops-tech-prep.md`: HackOps technical prep notes.

Keep HackOps-specific gameplay in future `game/hackops` or `data/hackops`
targets. Shared reusable technology belongs in neutral engine modules such as
`engine/terminal`, `engine/kernel`, `engine/world_api`, or `engine/rhi`.
