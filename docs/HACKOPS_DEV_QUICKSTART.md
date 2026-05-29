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

Run the first HackOps policy loop:

```bash
out/build/terminal-dev/bin/hackops_demo \
  --policy tools/nvim_surface_probe/sample_policy.py \
  --snapshot /tmp/hackops-policy-snapshot.txt
```

This opens the policy in the C++ Neovim surface, executes the policy with
`python3`, and maps the printed score into a small world-state decision. It is
the current shortest playable loop while renderer, sandbox, and World API work
are still being built out.

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

## Rendering (UE5, out of this repo)

There is no in-repo renderer anymore (ADR-0005): the self-built DX12/Metal
backend, the editor, and the `song_demo` target were deleted in 2026-05.
Rendering happens in a separate **UE5 view client** that consumes this core's
snapshot stream over the sim↔UE5 boundary. The HackOps tech line here runs
fully **headless** — `hackops_demo` needs no GPU. See
[`design/sim-ue5-boundary.md`](design/sim-ue5-boundary.md).

## Current Module Boundary

- `engine/terminal`: real Neovim external UI integration.
- `tools/nvim_surface_cpp_probe`: command-line smoke test for `NvimSurface`.
- `game/hackops`: minimal policy-to-world-state executable.
- `tools/nvim_surface_probe`: Python reference spike.
- `docs/projects/hackops-tech-prep.md`: HackOps technical prep notes.

Keep HackOps-specific gameplay in future `game/hackops` or `data/hackops`
targets. Shared reusable technology belongs in neutral engine modules such as
`engine/terminal`, `engine/runtime`, `engine/world`, or the future
`engine/gameapi` / `engine/sandbox` (the Game API + WASM player-code runtime).
