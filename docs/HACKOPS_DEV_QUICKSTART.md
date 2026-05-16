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

## Windows Song / Editor Track

The existing song/editor track is still Windows/DX12-first:

```powershell
cmake --preset windows-dx12-dev
cmake --build --preset windows-dx12-dev
ctest --test-dir out/build/windows-dx12-dev -C Debug --output-on-failure
```

Run the DX12U renderer smoke verification:

```powershell
.\scripts\verify_dx12u.ps1
```

This verifier is intentionally stricter than a normal build. It must run on
Windows, requires Windows SDK `10.0.20348.0` or newer, `cmake`, `dxc.exe`, and
`dxcompiler.dll`, compiles all SM6 shader targets including mesh shaders,
sampler feedback, and the RTGI `lib_6_3` library, then runs `song_demo` with
`NEXT_RENDERER_BACKEND=dx12`, `NEXT_REQUIRE_DX12U=1`, `NEXT_MESH_SHADER_DEBUG=1`,
and `NEXT_SAMPLER_FEEDBACK_DEBUG=1` so the mesh shader PSO, `DispatchMesh`,
sampler feedback map creation, and feedback write path are exercised during
runtime smoke.

Useful renderer switches while debugging:

```powershell
$env:NEXT_DEBUG_VIEW = "heatmap"      # default, wireframe, normals, depth, heatmap, ...
$env:NEXT_VRS_SHADING_RATE = "2x2"    # 1x1, 1x2, 2x1, 2x2
$env:NEXT_MESH_SHADER_DEBUG = "1"     # enables the DX12U mesh shader debug dispatch
$env:NEXT_SAMPLER_FEEDBACK_DEBUG = "1" # enables sampler feedback map writes
$env:NEXT_DX12_ALLOW_GPU_OVERLAP = "1" # opt out of conservative sync after per-frame dynamic resources are audited
.\scripts\verify_dx12u.ps1 -SmokeFrames 240
```

Use build/HLSL-only mode on Windows CI runners without an attached DX12U GPU:

```powershell
.\scripts\verify_dx12u.ps1 -SkipRuntimeSmoke
```

## Current Module Boundary

- `engine/terminal`: real Neovim external UI integration.
- `tools/nvim_surface_cpp_probe`: command-line smoke test for `NvimSurface`.
- `game/hackops`: minimal policy-to-world-state executable.
- `tools/nvim_surface_probe`: Python reference spike.
- `docs/projects/hackops-tech-prep.md`: HackOps technical prep notes.

Keep HackOps-specific gameplay in future `game/hackops` or `data/hackops`
targets. Shared reusable technology belongs in neutral engine modules such as
`engine/terminal`, `engine/kernel`, `engine/world_api`, or `engine/rhi`.
