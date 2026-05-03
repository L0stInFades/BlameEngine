# NEXT Terminal Module

`next_terminal` is the first engine-side HackOps module. It embeds real Neovim
as an external UI instead of drawing a fake editor.

Current scope:

- launch `nvim --embed`
- attach with `nvim_ui_attach`
- receive Msgpack-RPC redraw events
- maintain an `ext_linegrid` text grid
- send input through `nvim_input`
- expose snapshots for an ImGui panel, HUD, or future 3D terminal surface

This module intentionally does not own rendering. It is a terminal/editor state
source that higher-level UI code can draw.

## Probe

Build the standalone probe:

```bash
cmake -S . -B /tmp/next-terminal-cmake-tools \
  -DBUILD_TOOLS=ON -DBUILD_GAME=OFF -DBUILD_TESTS=OFF
cmake --build /tmp/next-terminal-cmake-tools --target next_nvim_surface_probe
```

Run it:

```bash
/tmp/next-terminal-cmake-tools/bin/next_nvim_surface_probe \
  --file tools/nvim_surface_probe/sample_policy.py \
  --snapshot /tmp/nvim-surface-cpp.txt
```

## Known Gaps

- POSIX process launch is verified locally; Windows `CreateProcess` launch code
  is present but still needs verification on a Windows machine.
- `ext_linegrid` main-grid rendering works, but floating grids, external
  cmdline UI, mouse, IME, and full highlight/style rendering still need proper
  engine implementation.
- The current output is a text snapshot, not a renderer-backed surface.
