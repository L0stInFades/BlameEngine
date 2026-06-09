#include "next/water/buoyancy.h"

#include <algorithm>
#include <cmath>

#include "next/water/water_def.h"
#include "next/water/water_surface.h"
#include "next/water/water_volume.h"

namespace Next::water {
namespace {

// Rotate a body-local vector by the orientation quaternion q=(x,y,z,w): v' = q v q^-1.
void RotateByQuat(const float q[4], const float v[3], float out[3]) {
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];
    const float tx = 2.0f * ((y * v[2]) - (z * v[1]));
    const float ty = 2.0f * ((z * v[0]) - (x * v[2]));
    const float tz = 2.0f * ((x * v[1]) - (y * v[0]));
    out[0] = v[0] + (w * tx) + ((y * tz) - (z * ty));
    out[1] = v[1] + (w * ty) + ((z * tx) - (x * tz));
    out[2] = v[2] + (w * tz) + ((x * ty) - (y * tx));
}

}  // namespace

WaterForceOutput ComputeWaterForce(const FluidSample& fluid, const BodyBuoyancyInput& body, float dt) {
    WaterForceOutput out;
    out.submersionDepth = fluid.surfaceHeight - body.position[1];

    float fraction = 0.0f;
    float volume = 0.0f;
    if (body.shape == 1) {  // Box
        volume = SubmergedBoxVolume(body.position[1], body.halfExtents, fluid.surfaceHeight, fluid.floorY, fraction);
    } else {  // Sphere (halfExtents[0] == radius)
        volume =
            SubmergedSphereVolume(body.position[1], body.halfExtents[0], fluid.surfaceHeight, fluid.floorY, fraction);
    }
    out.submergedFraction = fraction;
    out.inWater = fraction > 0.0f;
    if (!out.inWater || volume <= 0.0f) {
        return out;  // dry: no force
    }

    // Archimedes buoyancy (upward). Applied as a force so the backend's gravity composes with it.
    out.force[1] = fluid.density * kWaterGravity * volume;

    // Drag against velocity RELATIVE TO THE FLUID. The current (flowVelocity) only participates when
    // the body flags it (WaterCurrent), so a still pool damps toward rest while a river sweeps toward
    // its flow. Quadratic term grows with relative speed (form drag). Scaled by submerged fraction.
    const bool current = (fluid.flags & WaterCurrent) != 0;
    float u[3];
    for (int i = 0; i < 3; ++i) {
        u[i] = body.velocity[i] - (current ? fluid.flowVelocity[i] : 0.0f);
    }
    const float speed = std::sqrt((u[0] * u[0]) + (u[1] * u[1]) + (u[2] * u[2]));
    const float rate = (fluid.linearDrag + (fluid.quadraticDrag * speed)) * fraction;  // 1/s

    // CLAMP k to [0,1]: the impulse dv = -k*u can at most cancel the relative velocity, never reverse
    // it -> explicit drag is unconditionally stable at fixed dt (no rocket-launch).
    float k = rate * dt;
    if (k > 1.0f) {
        k = 1.0f;
    } else if (k < 0.0f) {
        k = 0.0f;
    }
    for (int i = 0; i < 3; ++i) {
        out.dragImpulse[i] = -body.mass * k * u[i];
    }
    return out;
}

BoxBuoyancyResult ComputeBoxBuoyancy(const WaterBodyInstance& water, const float center[3], const float rot[4],
                                     const float halfExtents[3], const float linVel[3], const float angVel[3],
                                     float mass, double timeSeconds, float dt) {
    BoxBuoyancyResult r;
    const float hx = halfExtents[0];
    const float hy = halfExtents[1];
    const float hz = halfExtents[2];
    if (hx <= 0.0f || hy <= 0.0f || hz <= 0.0f || dt <= 0.0f || mass <= 0.0f) {
        return r;
    }
    const float columnArea = hx * hz;  // each corner owns 1/4 of the (2hx*2hz) footprint
    const float localCorners[4][3] = {{hx, -hy, hz}, {hx, -hy, -hz}, {-hx, -hy, hz}, {-hx, -hy, -hz}};
    // W9: compile the body's waves ONCE; the 4 corners sample the SAME (body, time), so the per-wave
    // k/omega divide+mul is hoisted out of the per-corner loop. Bit-identical to per-corner SampleHeightFast.
    const CompiledWaves cw = CompileWaves(water, timeSeconds);
    float fracSum = 0.0f;
    for (int c = 0; c < 4; ++c) {
        float rWorld[3];
        RotateByQuat(rot, localCorners[c], rWorld);
        const float wx = center[0] + rWorld[0];
        const float wy = center[1] + rWorld[1];
        const float wz = center[2] + rWorld[2];
        const float surfaceY = SampleHeightFast(cw, wx, wz, timeSeconds);
        const float submH = std::clamp(surfaceY - wy, 0.0f, 2.0f * hy);
        if (submH <= 0.0f) {
            continue;
        }
        const float frac = submH / (2.0f * hy);
        fracSum += frac;
        const float buoy = water.density * kWaterGravity * (columnArea * submH);  // up
        // Vertical velocity of this corner = (linVel + angVel x rWorld).y
        const float vpY = linVel[1] + ((angVel[2] * rWorld[0]) - (angVel[0] * rWorld[2]));
        // Clamped vertical drag at the corner (damps heave AND pitch/roll via the offset): the per-corner
        // impulse is -(m/4)*k*vpY with k<=1, so it can at most cancel that corner's vertical motion.
        float k = water.linearDrag * frac * dt;
        if (k > 1.0f) {
            k = 1.0f;
        } else if (k < 0.0f) {
            k = 0.0f;
        }
        const float dragY = -(mass * 0.25f) * k * vpY / dt;  // force; impulse = force*dt = -(m/4)*k*vpY
        BuoyancyPointForce& pf = r.points[r.pointCount++];
        pf.point[0] = wx;
        pf.point[1] = wy;
        pf.point[2] = wz;
        pf.force[0] = 0.0f;
        pf.force[1] = buoy + dragY;
        pf.force[2] = 0.0f;
    }
    r.inWater = r.pointCount > 0;
    r.submergedFraction = fracSum / 4.0f;  // dry corners contribute 0 -> avg over all four
    if (r.inWater) {
        // COM horizontal + flow drag (clamped impulse); the vertical component is handled per-corner.
        const bool current = (water.flags & WaterCurrent) != 0;
        const float ux = linVel[0] - (current ? water.flowVelocity[0] : 0.0f);
        const float uz = linVel[2] - (current ? water.flowVelocity[2] : 0.0f);
        const float speed = std::sqrt((ux * ux) + (uz * uz));
        float kk = (water.linearDrag + (water.quadraticDrag * speed)) * r.submergedFraction * dt;
        if (kk > 1.0f) {
            kk = 1.0f;
        } else if (kk < 0.0f) {
            kk = 0.0f;
        }
        r.comDragImpulse[0] = -mass * kk * ux;
        r.comDragImpulse[1] = 0.0f;
        r.comDragImpulse[2] = -mass * kk * uz;
    }
    return r;
}

}  // namespace Next::water
