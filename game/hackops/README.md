# HackOps Demo

`hackops_demo` is the first headless game target for the real-code gameplay
track. It does not depend on the DX12 renderer and is safe to build from the
`terminal-dev` preset on macOS/Linux CI.

Current behavior:

- creates the maintenance-window Ops workspace
- optionally resets it
- optionally creates a snapshot
- optionally runs `policy.py` through `PythonWorker`
- lists generated task files

Example:

```bash
out/build/terminal-dev/bin/hackops_demo \
  --reset \
  --workspace /tmp/next-hackops-maintenance-window \
  --snapshot smoke \
  --run-policy \
  --list
```
