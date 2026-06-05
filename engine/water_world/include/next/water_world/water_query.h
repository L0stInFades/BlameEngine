#pragma once

#include "next/gameapi/sim_clock.h"
#include "next/gameapi/world_query.h"
#include "next/water_world/water_store.h"

// Water gameplay queries (ADR-0015). A WaterWorldQuery makes the water SURFACE a raycast target, so it
// composes into the EXISTING Game API Sense-gated raycast (ADR-0007/0010) — player code / AI "see" the
// water surface with no new ABI, exactly as VegetationWorldQuery folds vegetation cover in. Plus the
// gameplay helpers the hooks need: water height, submersion depth, stealth (submerged + breaks-sight),
// and conductivity (submerged + conductive — the hacking short-circuit hazard).

namespace Next::water {

// IWorldQuery over the water surface: a downward (or any) ray returns the nearest water-surface hit
// within its body's XZ bounds. An optional fallback IWorldQuery (e.g. physics/vegetation) is also
// consulted and the NEARER hit wins. The surface is evaluated at the shared authoritative SimClock
// (the SAME clock the WaterForceSystem uses, so buoyancy and what the ray "sees" agree); a null clock
// means t=0 (still water).
//
// It ALSO implements IWaterQuery (ADR-0015 W10), so the same object backs the Game API's Sense-gated
// GetWaterState call: player code / AI read submersion depth, surface height, current, and the
// conductive/lethal flags of the governing body — the same authoritative state buoyancy uses this tick.
class WaterWorldQuery final : public Next::gameapi::IWorldQuery, public Next::gameapi::IWaterQuery {
public:
    explicit WaterWorldQuery(const WaterStore* store, const Next::gameapi::SimClock* clock = nullptr,
                             Next::gameapi::IWorldQuery* fallback = nullptr)
        : store_(store), clock_(clock), fallback_(fallback) {}

    Next::gameapi::RaycastResult Raycast(const float origin[3], const float direction[3], float maxDistance) override;
    Next::gameapi::WaterStateResult QueryWater(const float point[3]) override;

private:
    const WaterStore* store_;
    const Next::gameapi::SimClock* clock_;
    Next::gameapi::IWorldQuery* fallback_;
};

// Surface height at (x,z), with outFound=false (and 0 returned) when there is no water there.
float WaterHeightAt(const WaterStore& store, float x, float z, double timeSeconds, bool& outFound);

// Depth of (x,y,z) below the governing body's surface (<= 0 / 0 when not submerged or no water).
float SubmersionDepthAt(const WaterStore& store, float x, float y, float z, double timeSeconds);

// Is the point at/below a water surface (and above the floor)?
bool IsSubmergedAt(const WaterStore& store, float x, float y, float z, double timeSeconds);

// Stealth: a point submerged in water flagged WaterBreaksSight is hidden from line-of-sight sensors.
bool IsHiddenBySubmersion(const WaterStore& store, float x, float y, float z, double timeSeconds);

// Hacking hazard: a point submerged in water flagged WaterConductive shorts electronics there.
bool IsInConductiveWater(const WaterStore& store, float x, float y, float z, double timeSeconds);

}  // namespace Next::water
