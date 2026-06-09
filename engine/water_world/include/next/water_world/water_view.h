#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "next/boundary/snapshot.h"  // GameEvent / EntityId
#include "next/water/water_def.h"
#include "next/water/water_surface.h"  // SurfaceSample (render-side surface reconstruction)

// sim->UE5 water view contract (ADR-0015). Bulk water streams to UE5 as per-cell payloads (the same
// wire-ready CellLayer::Water blob), NOT as per-entity records — UE5 reads each WaterBodyInstance's
// WaveComponents/bounds/flow and builds/animates its own water surface (Gerstner shader fed the SAME
// parameters the sim evaluates, so the surface AGREES). One-shot splash/exit cues ride the boundary's
// existing GameEvent channel (cosmetic, no authority). MockWaterConsumer is a faithful headless
// stand-in that proves the streamed bytes are consumable (the byte contract — it is NOT a renderer).

namespace Next::water {

// GameEvent.type for one-shot cosmetic water cues (FX + audio).
constexpr uint32_t kWaterEventSplash = 0x5753504Cu;  // 'WSPL' — a body entered the water
constexpr uint32_t kWaterEventExit = 0x57455854u;    // 'WEXT' — a body left the water
// GameEvent.type for a GAMEPLAY hazard: an electronic device shorted out in conductive water (W11).
// Unlike splash/exit this is an authoritative consequence (the device is now disabled), forwarded so
// UE5 can play the sparks/smoke cue; the authority lives in the device's ElectronicComponent.
constexpr uint32_t kWaterEventShort = 0x57534854u;  // 'WSHT' — electronics shorted by conductive water
// GameEvent.type for drowning death: a swimmer ran out of air and lost all health underwater (W12).
// Authority lives in the SwimmerComponent; this is the cosmetic/UI cue (gasp, screen, ragdoll).
constexpr uint32_t kWaterEventDrown = 0x5744524Eu;  // 'WDRN' — a swimmer drowned

// An authoritative water contact the force system detected, to be forwarded as a cosmetic cue.
struct WaterContactEvent {
    bool entered = true;  // true: entered water (splash); false: left water
    Next::boundary::EntityId entity = Next::boundary::kInvalidEntity;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float speed = 0.0f;  // impact/exit speed -> splash intensity
};

// Convert an authoritative contact into a boundary GameEvent (subject = the entity; params = position
// + speed). Carries no authority.
Next::boundary::GameEvent ToBoundaryEvent(const WaterContactEvent& ev);

// A faithful UE5-side consumer stand-in (test double). UE5 would, on cell-load, unpack the Water blob
// and (de-dup by bodyId across cells) instantiate/animate a water surface per body from its
// WaveComponents/bounds; on unload drop them. It holds NO authority — it only mirrors what the sim sent,
// and exposes enough to prove the bytes carry the full surface parameters.
class MockWaterConsumer {
public:
    // Cell streamed in: parse the Water blob and keep its body records. FAIL-CLOSED on a bad blob.
    bool OnCellLoaded(int32_t cellX, int32_t cellZ, const uint8_t* waterBlob, size_t size);
    void OnCellUnloaded(int32_t cellX, int32_t cellZ);

    size_t LoadedCellCount() const { return cells_.size(); }
    size_t TotalBodyRecords() const;   // sum across cells (a body spanning N cells counts N times)
    size_t DistinctBodyCount() const;  // unique bodyIds (what UE5 would actually instantiate)
    size_t TotalWaveCount() const;     // total Gerstner components received (proves wave params survive)
    // The surface parameters received for a body (first cell that carried it), or nullptr.
    const WaterBodyInstance* SurfaceParamsForBody(uint32_t bodyId) const;

    // --- Render-side surface reconstruction (ADR-0015 W17-W21) ---
    // What UE5 does with the bytes: turn each streamed WaterBodyInstance into the SAME analytic Gerstner
    // surface the sim simulates. These evaluate that surface from ONLY the streamed parameters, using the
    // shared authoritative surface math (engine/water), so a render-parity test can prove the wire
    // contract carries EVERYTHING needed to reproduce the sim surface to the meter — and the UE5 HLSL,
    // fed the same WaveComponents/bounds/flood and the same det-trig formula, will agree. This is the
    // headless reference + completeness proof; it is NOT a GPU and renders nothing.
    bool EvaluateSurface(uint32_t bodyId, float x, float z, double timeSeconds, SurfaceSample& out) const;
    // Cosmetic LOD height (the renderer's far-field path; sums only the maxWaves largest components).
    bool EvaluateSurfaceHeightLOD(uint32_t bodyId, float x, float z, double timeSeconds, int maxWaves,
                                  float& outHeight) const;

private:
    struct CellKey {
        int32_t x = 0;
        int32_t z = 0;
        bool operator<(const CellKey& o) const { return x != o.x ? x < o.x : z < o.z; }
    };
    std::map<CellKey, std::vector<WaterBodyInstance>> cells_;
};

}  // namespace Next::water
