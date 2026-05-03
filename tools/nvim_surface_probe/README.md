# NvimSurface Probe

This is the first HackOps technology-prep spike for embedding real Neovim
without a terminal emulator or WebView.

The probe starts `nvim --embed`, attaches as an external UI with
`ext_linegrid`, collects redraw events over Msgpack-RPC, maintains a simple
screen grid, and writes a text snapshot. It intentionally avoids third-party
Python packages so it can run on a clean developer machine.

By default it loads the user's normal Neovim configuration. On a machine with
LazyVim installed, the snapshot should show the real LazyVim statusline,
relative line numbers, icons, syntax highlights collapsed into text cells, and
the opened file content.

## Run

```bash
python3 tools/nvim_surface_probe/nvim_surface_probe.py \
  --file tools/nvim_surface_probe/sample_policy.py \
  --input "i# edited from probe<Esc>:wq<CR>" \
  --snapshot /tmp/nvim-surface-snapshot.txt
```

For a non-destructive save test, copy the sample file to `/tmp` first:

```bash
cp tools/nvim_surface_probe/sample_policy.py /tmp/nvim-surface-policy-edit.py
python3 tools/nvim_surface_probe/nvim_surface_probe.py \
  --file /tmp/nvim-surface-policy-edit.py \
  --input "Go# probe edit<Esc>:w<CR>" \
  --snapshot /tmp/nvim-surface-edited.txt
```

If the probe works, it proves these pieces:

- `nvim --embed` can be launched as a child process.
- NEXT can become an external Neovim UI instead of rendering a fake editor.
- The UI can consume `ext_linegrid` redraw events.
- Input can be sent as Neovim input sequences.
- A future engine renderer can draw the maintained grid into ImGui, a HUD, or a
  3D terminal surface.

## Scope

This is not a production terminal implementation. It does not implement every
Neovim UI event. The production spike should move the core ideas into C++:

- process launch
- Msgpack-RPC transport
- UI grid model
- highlight/style table
- input mapping
- resize
- texture/font-atlas rendering

The first C++ migration now exists in:

```text
engine/terminal/
tools/nvim_surface_cpp_probe/
```
