# NvimSurface C++ Probe

This probe exercises `next_terminal` without starting the full renderer.

```bash
cmake -S . -B /tmp/next-terminal-cmake-tools \
  -DBUILD_TOOLS=ON -DBUILD_GAME=OFF -DBUILD_TESTS=OFF
cmake --build /tmp/next-terminal-cmake-tools --target next_nvim_surface_probe

/tmp/next-terminal-cmake-tools/bin/next_nvim_surface_probe \
  --file tools/nvim_surface_probe/sample_policy.py \
  --snapshot /tmp/nvim-surface-cpp.txt
```

Input tokens supported by the probe:

- `<Esc>`
- `<CR>`
- `<Tab>`
- `<BS>`

Example save test:

```bash
cp tools/nvim_surface_probe/sample_policy.py /tmp/nvim-surface-cpp-edit.py
/tmp/next-terminal-cmake-tools/bin/next_nvim_surface_probe \
  --file /tmp/nvim-surface-cpp-edit.py \
  --input "Go# cpp probe edit<Esc>:w<CR>" \
  --snapshot /tmp/nvim-surface-cpp-edited.txt
```
