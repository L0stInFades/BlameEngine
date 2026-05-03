# NEXT Ops Module

`next_ops` owns the engine-side scaffolding for real-code gameplay systems that
are not specific to a single game target.

The first implemented piece is `OpsWorkspace`: a controlled player workspace
for the HackOps maintenance-window prototype. It can generate seed files,
reset the workspace, list files, and create snapshots without exposing the
entire host filesystem as game state.

Current seed files:

- `ops.toml`
- `policy.py`
- `orders.log`
- `city_graph.json`
- `README.md`

`PythonWorker` runs a workspace Python script in an isolated child process and
captures stdout, stderr, exit code, timeout state, and runtime duration.
`PolicySimulation` parses the JSON emitted by `policy.py`, validates selected
orders and routes against `city_graph.json`, and produces a typed route/risk
result for headless smoke tests and later world-system integration.
