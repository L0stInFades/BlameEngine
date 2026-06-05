# UE5 Water Render & Authoring Contract (ADR-0015 · W17–W21)

This is the contract between the headless authoritative water sim (`engine/water`,
`engine/water_world`) and the **out-of-repo UE5 project** that renders it. Per
[ADR-0005](../adr/0005-ue5-renderer-jolt-headless-world.md) rendering and content authoring tools are
delegated to UE5/Jolt; this engine owns the *authoritative simulation* and hands UE5 exactly the bytes
it needs to draw the same water the sim simulates. Nothing here renders pixels — it defines and
**proves the contract** so the UE5 work is unambiguous and verifiable.

## The data path

```
designer authoring                cook (engine/water_world)              stream                 UE5 (out of repo)
─────────────────                 ──────────────────────────            ────────                ─────────────────
WaterBuilder (C++)   ─┐                                                  CellLayer::Water        AWaterManager:
  or                  ├─► WaterSceneDef ─► WaterValidator ─► CookWaterCell ──► .nlc cell ──────►  on cell load: unpack
ParseWaterDefText  ──┘     (fail-closed)    (per-cell select + BakeBody       (layered blob)        the Water layer →
  (.water text)                              + PackCell v2)                                          one Gerstner surface
                                                                                                     per WaterBodyInstance
  assetc water <in.water> <out>  drives the same cook from the CLI.                                  (de-dup by bodyId)
```

* **Authoring** is complete and in-repo: the fluent `WaterBuilder`, the `.water` text format
  (`ParseWaterDefText` — covers `ocean/pool/river/flood/lake`, `bounds/surface/density/flow/flood/flags/
  wave/ocean`-spectrum), and the `assetc water` subcommand. A designer authors a scene; the cook bakes,
  per world cell, the bodies overlapping it into a flat `WaterBodyInstance` array.
* **The wire record** is `WaterBodyInstance` (see `water_def.h`, `static_assert`-locked at 264 bytes)
  inside the versioned `NWTR` cell blob (`water_cell.h`, v2, migrates v1 — W28). It carries everything
  the renderer needs: world-space `bounds`, `surfaceHeight`, `type`, up to 8 `WaveComponent`s
  (amplitude/wavelength/direction/speed/steepness), `flowVelocity`, `flood{Rate,MaxHeight}`, `flags`,
  `density`, and `visual` (the UE5 mesh/material variant id, forwarded verbatim).

## What UE5 builds from the bytes

For each `WaterBodyInstance` in a loaded cell (de-duplicated by `bodyId` across the cells a body spans):

1. Instantiate a water surface actor/mesh over `bounds` (XZ extent; `surfaceHeight` is the base Y),
   selecting the mesh/material by `visual`.
2. Drive a **Gerstner surface shader** with the body's `WaveComponent`s. The surface MUST use the same
   analytic form the sim uses (`engine/water/water_surface.h`):
   ```
   phase_i = k_i·(D_i·(x,z)) − ω_i·t,   k_i = 2π/wavelength_i,   ω_i = speed_i·k_i
   height  = base + Σ A_i·cos(phase_i)            // base = EffectiveSurfaceHeight (flood rise applied)
   P.xz    = (x,z) − Σ Q_i·A_i·D_i·sin(phase_i)   // Gerstner horizontal pinch
   normal  = analytic (see SurfaceSampleAt)
   ```
   `t` is the **server-authoritative** sim time (W14), delivered with each snapshot; UE5 follows it via
   the `RenderClock` (`render_clock.h`) and never extrapolates past it. For determinism the sim uses an
   engine-owned `DetSin/DetCos` (`det_trig.h`); UE5's HLSL may use hardware trig for cosmetics, but for
   anything gameplay-visible (a boat sitting on the surface) it should match the same formula.
3. For `Flood` bodies, raise the surface to `min(surfaceHeight + floodRate·t, floodMaxHeight)`.
4. For `River` bodies, use `flowVelocity` to drive the flow map / foam direction.
5. Far water uses the **LOD** path (`SampleHeightLOD` / `MockWaterConsumer::EvaluateSurfaceHeightLOD`):
   sum only the N largest-amplitude components. This is cosmetic and NON-authoritative — never feed it
   back into the sim (it is not bit-identical and would break replay/determinism).

`flags` (`WaterConductive/Lethal/BreaksSight/...`) are gameplay signals the sim already consumes
(W11 electronics-short, W12 swim/drown, stealth); UE5 may use them for cosmetic cues (steam over
conductive water, murk for breaks-sight) but holds no authority over them.

## One-shot cues

Splash/exit (`kWaterEventSplash`/`kWaterEventExit`) and the gameplay consequences shorts
(`kWaterEventShort`) and drowning (`kWaterEventDrown`) arrive on the boundary's `GameEvent` channel
(`subject` = entity, `params` = position + intensity). They are cosmetic triggers; authority lives in
the sim (the device's `ElectronicComponent`, the character's `SwimmerComponent`).

## What is PROVEN in-repo vs what is UE5's

| Concern | In-repo (proven) | UE5 (out of repo) |
|---|---|---|
| Authoring → cook → cell bytes | ✅ `WaterBuilder`/`.water`/`assetc`, fail-closed validate, golden-stable cook | — |
| Wire contract carries every surface param | ✅ `test_water_render` (`StreamedParamsCarryEverything…`) | — |
| A consumer reproduces the authoritative surface (height+normal) from ONLY the bytes, to sub-mm, all body types, across time | ✅ `test_water_render` (`ConsumerReconstructsAuthoritativeSurface`) via `MockWaterConsumer::EvaluateSurface` | UE5's HLSL Gerstner, fed the same components + formula, agrees by construction |
| Streaming the cell via real async IO | ✅ `test_water_slice` (cook → StreamingManager → Sync → store) | UE5 cell-load hook calls the same unpack |
| Actual GPU rendering / materials / foam / caustics | ❌ (not our layer) | ✅ UE5 |
| In-editor placement UI | ❌ (the `.water`/builder authoring is the data; UE5 editor tooling is theirs) | ✅ UE5 |

**Honest status.** `MockWaterConsumer` is a faithful **headless reference + completeness proof**: it
unpacks the streamed bytes and reconstructs the exact surface the sim computes, so it guarantees the
contract is *sufficient* and *lossless*. It is NOT a renderer and proves nothing about UE5's pixels,
materials, or performance — those are delivered in the UE5 project against this contract. This is the
same honest boundary the rest of the engine draws (ADR-0005): the headless authoritative world is the
deliverable, UE5 consumes it.
