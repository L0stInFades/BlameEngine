#pragma once

#include "next/water/water_def.h"

// Submersion + analytic submerged-volume math. Two layers:
//  (1) PLANE-BASED geometry core: shape vs a horizontal water plane at `surfaceY`, clipped against the
//      container floor at `floorY`. Exact (spherical-cap for spheres, slab clip for boxes). Pure, fast,
//      surface-eval-free — the buoyancy system passes the already-sampled surfaceY. Only +,-,*,/,sqrt.
//      (V1 uses the locally-horizontal surface sampled at the shape's XZ; box-vs-tilted-plane via
//      inclusion-exclusion is a P2 add for Jolt torque, where rotation actually exists.)
//  (2) BODY-BASED point queries that sample the body's wavy surface — for gameplay (drowning,
//      conductivity, stealth): SubmersionDepth / IsPointSubmerged.

namespace Next::water {

// Volume (m^3) of a sphere (vertical center `centerY`, radius r) lying between a horizontal water
// surface at `surfaceY` and the container floor at `floorY`. Exact spherical cap clipped both ends.
// outFraction = submergedVolume / totalVolume in [0,1].
float SubmergedSphereVolume(float centerY, float radius, float surfaceY, float floorY, float& outFraction);

// Volume (m^3) of an axis-aligned box (vertical center `centerY`, half-extents h) between `surfaceY`
// and `floorY` (slab clip). outFraction in [0,1].
float SubmergedBoxVolume(float centerY, const float halfExtents[3], float surfaceY, float floorY, float& outFraction);

// Depth of world point (x,y,z) below this body's (wavy) surface at time t. > 0 submerged, <= 0
// at/above. Returns 0 when outside the body's XZ footprint or below the container floor.
float SubmersionDepth(const WaterBodyInstance& body, float x, float y, float z, double timeSeconds);

// True iff the point lies within the body's volume and at/below the surface.
bool IsPointSubmerged(const WaterBodyInstance& body, float x, float y, float z, double timeSeconds);

}  // namespace Next::water
