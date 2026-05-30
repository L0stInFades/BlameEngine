# ADR-0013: Data-driven level-design system (engine/level)

- **Status**: Accepted
- **Date**: 2026-05-30
- **Relates to**: [ADR-0002](0002-archetype-ecs.md) (ECS), [ADR-0007](0007-game-api-contract.md) (gameapi components / ObjectiveStore), [ADR-0006](0006-sim-ue5-boundary.md) (RenderableComponent), [ADR-0009](0009-physics-jolt-backend.md) (RigidBodyComponent)

## Context

The engine could run a hand-built world (the vertical slices construct entities in C++), but there
was no way to **author a level as data** — the initial state of the authoritative world (entities,
components, tags, objectives, win/lose conditions) — and load it deterministically with first-class
validation. The repo already has a `engine/task` "Task System", but it is a string/`std::variant`/
JSON, narrative-quest system from the previous game era (prototype-grade, not wired to the world,
not deterministic-by-construction) — the wrong substrate for the headless, integer-keyed,
replay-deterministic moat.

## Decision

Add **`engine/level` (`next_level`)** — a small, deterministic, data-oriented level system that
authors *over* the existing ECS + Game API, inventing no new component types:

- **`LevelDef`** — a pure POD/STL data model: metadata (id, schema version, optional agent ref),
  `EntityDef`s (each carries any subset of transform / tag / renderable / move / action / rigid
  body), `ObjectiveDef`s, and a flat list of `WinConditionDef`s. Integer-keyed throughout (entity
  refs, objective ids uint32, tag bit indices 0..63) to match the Game API contract.
- **`LevelBuilder`** — fluent C++ authoring; assigns dense 1-based entity refs. (The future
  text/binary loader would emit the same `LevelDef`.)
- **`LevelValidator`** — a **total, fail-closed** gate: it accumulates *all* violations (30+
  `ValidationCode`s — dangling/duplicate/reserved/oversized refs, parent cycles, non-finite or
  degenerate transforms, invalid bodies, move/renderable without a transform, dangling/parented/
  transform-less win targets, unknown win kinds, …) and the loader refuses to touch the World if
  any error is present. This is what makes "zero defects" enforceable: a malformed level is
  rejected here, not discovered at runtime (where, e.g., an out-of-range tag would silently no-op).
- **`LevelLoader`** — **transactional** (validate fully, then build; on failure the World is left
  untouched) and **deterministic** (entities created in vector order, components applied in a fixed
  order, parents resolved in a second pass, objectives seeded into `ObjectiveStore`). Two loads of
  the same def into fresh Worlds produce identical entities.
- **`WinEvaluator`** — a **read-only, total** evaluator of the flat win-condition list against the
  live World + ObjectiveStore (`ObjectiveAtLeast/Equals`, `EntityReached`, `AllTaggedDestroyed`),
  loss-priority. Runs each tick without perturbing determinism.

Win/lose is a **flat, total enum**, not a boolean-expression tree — deliberately small so the
evaluator is exhaustive and the validator can fully reason about it.

## Out of scope (v1, to keep the defect surface minimal)

Serialization / on-disk formats (LevelDef is authored in C++); capability grants in levels (the
loader is an engine/privileged op, not exposed through the Game API); composite/boolean win-
condition trees; runtime entity spawning; parent-transform composition (the engine composes no
parent transforms anywhere, so `EntityReached` against a parented target is *rejected* rather than
silently never firing — see the validator).

## Consequences

- **Proven** by 19 tests across 4 suites (validator: canonical-accept + one targeted reject per
  code + error accumulation + determinism; loader: transactional refusal, exact component
  application, agent/objective binding, parent wiring, two-load determinism, oversized-ref refusal,
  body pose seeding, transform-less-collider accept; conditions: each kind flips + loss priority +
  total-on-unsatisfiable; **end-to-end**: load a level → run the sandboxed seek guest in it → the
  win condition transitions to Won, twice deterministically). ASan/UBSan-clean; in CI.
- **Built via a planning → implement → strict-review → fix → re-review loop** (multi-agent
  workflows). The review caught and fixed real defects before merge: an unbounded `ref.value` that
  would force a multi-GB allocation; unvalidated NaN/negative `MoveTarget` fields that corrupt the
  transform in kinematic stepping; `EntityReached`/renderable/move targets without a transform that
  silently never fire/render; an unknown `WinKind` that validates but never fires; recursive
  parent-cycle detection that could stack-overflow on a deep chain; and the parented-target
  never-fires case. A `LoadedLevel` no longer borrows a pointer into the source def (a
  use-after-scope hazard the first review surfaced).
- **Cost model is documented contract**: `AllTaggedDestroyed` is vacuously true when no entity
  carries the tag; the validator warns statically (`WinConditionTagNeverPresent`) under the
  documented assumption that nothing mutates GameTags at runtime (GameTag is read-only over the
  Game API today). Revisit if a runtime spawn/tag-write path is introduced.
- Reuses `World`, `TransformComponent`, `gameapi::{GameTag,MoveTarget,ActionFlags,ObjectiveStore}`,
  `physics::RigidBodyComponent`, `boundary::RenderableComponent`. A loaded level flows through the
  existing tick loop and `SnapshotPublisher` to UE5 with zero new wiring.
