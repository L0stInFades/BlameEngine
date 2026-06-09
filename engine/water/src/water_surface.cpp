#include "next/water/water_surface.h"

#include <algorithm>
#include <cmath>

#include "next/water/det_trig.h"

namespace Next::water {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr double kTwoPiD = 6.28318530717958647692;
// Fixed-point iterations to invert the Gerstner horizontal pinch when querying height at a world
// (x,z). For validated steepness (sum of Q bounded by 1) this converges below a millimeter.
constexpr int kInversionIterations = 4;

// Gerstner wave phase, computed in DOUBLE and reduced mod 2π before the float cast. The time term
// (omega*t) grows unbounded; casting it to float at large t collapses precision (visible jitter/drift
// over a long session — W27). Doing the whole phase in double and wrapping to [0, 2π) keeps the float
// argument small and exact. `dirDotPos` = D·(x,z); k = 2π/wavelength; omega = speed*k.
float WrapPhase(double wavelength, double dirDotPos, double speed, double timeSeconds) {
    const double k = kTwoPiD / wavelength;
    const double omega = speed * k;
    double ph = (k * dirDotPos) - (omega * timeSeconds);
    ph -= kTwoPiD * std::floor(ph / kTwoPiD);  // reduce to [0, 2π)
    return static_cast<float>(ph);
}

// D·(x,z) in double (positions can be large in an open world; keep the dot precise).
double DirDotPos(const WaveComponent& w, float x, float z) {
    return (static_cast<double>(w.direction[0]) * static_cast<double>(x)) +
           (static_cast<double>(w.direction[1]) * static_cast<double>(z));
}

// Defense-in-depth: waveCount indexes waves[kMaxWavesPerBody]. UnpackCell already fail-closes a corrupt
// count at the trust boundary; this clamp guarantees no sampler ever reads/writes past waves[] even for a
// directly-constructed body (tools/tests). For a valid body (count <= kMaxWavesPerBody) it is a no-op, so
// every result stays bit-identical.
uint8_t SafeWaveCount(const WaterBodyInstance& body) {
    return body.waveCount <= kMaxWavesPerBody ? body.waveCount : kMaxWavesPerBody;
}

// Sum the Gerstner HORIZONTAL displacement contributed at undisplaced point (x0,z0) at time t.
void GerstnerHorizontal(const WaterBodyInstance& body, float x0, float z0, double t, float& dx, float& dz) {
    dx = 0.0f;
    dz = 0.0f;
    const uint8_t count = SafeWaveCount(body);
    for (uint8_t i = 0; i < count; ++i) {
        const WaveComponent& w = body.waves[i];
        if (w.wavelength <= 0.0f) {
            continue;
        }
        const float phase = WrapPhase(w.wavelength, DirDotPos(w, x0, z0), w.speed, t);
        const float s = w.steepness * w.amplitude * DetSin(phase);
        dx -= w.direction[0] * s;
        dz -= w.direction[1] * s;
    }
}

}  // namespace

float EffectiveSurfaceHeight(const WaterBodyInstance& body, double timeSeconds) {
    if (static_cast<WaterType>(body.type) == WaterType::Flood && body.floodRate > 0.0f) {
        const double risen =
            static_cast<double>(body.surfaceHeight) + (static_cast<double>(body.floodRate) * timeSeconds);
        const double cap = static_cast<double>(body.floodMaxHeight);
        return static_cast<float>((risen > cap) ? cap : risen);
    }
    return body.surfaceHeight;
}

float SampleHeightFast(const WaterBodyInstance& body, float x, float z, double timeSeconds) {
    float h = EffectiveSurfaceHeight(body, timeSeconds);
    const uint8_t count = SafeWaveCount(body);
    for (uint8_t i = 0; i < count; ++i) {
        const WaveComponent& w = body.waves[i];
        if (w.wavelength <= 0.0f) {
            continue;
        }
        const float phase = WrapPhase(w.wavelength, DirDotPos(w, x, z), w.speed, timeSeconds);
        h += w.amplitude * DetCos(phase);
    }
    return h;
}

CompiledWaves CompileWaves(const WaterBodyInstance& body, double timeSeconds) {
    CompiledWaves cw;
    cw.base = EffectiveSurfaceHeight(body, timeSeconds);
    const uint8_t count = SafeWaveCount(body);  // never append past cw.waves[kMaxWavesPerBody]
    for (uint8_t i = 0; i < count; ++i) {
        const WaveComponent& w = body.waves[i];
        if (w.wavelength <= 0.0f) {
            continue;  // skip the SAME invalid waves SampleHeightFast skips -> identical order
        }
        CompiledWave& o = cw.waves[cw.count++];
        // Match WrapPhase's doubles EXACTLY so compiled sampling is bit-identical to the inline path.
        o.k = kTwoPiD / static_cast<double>(w.wavelength);
        o.omega = static_cast<double>(w.speed) * o.k;
        o.amplitude = w.amplitude;
        o.dirX = w.direction[0];
        o.dirZ = w.direction[1];
        o.steepness = w.steepness;
    }
    return cw;
}

float SampleHeightFast(const CompiledWaves& compiled, float x, float z, double timeSeconds) {
    float h = compiled.base;
    for (uint8_t i = 0; i < compiled.count; ++i) {
        const CompiledWave& w = compiled.waves[i];
        // Identical arithmetic to WrapPhase + the inline SampleHeightFast loop (k/omega are pre-divided).
        const double dirDot = (static_cast<double>(w.dirX) * static_cast<double>(x)) +
                              (static_cast<double>(w.dirZ) * static_cast<double>(z));
        double ph = (w.k * dirDot) - (w.omega * timeSeconds);
        ph -= kTwoPiD * std::floor(ph / kTwoPiD);
        h += w.amplitude * DetCos(static_cast<float>(ph));
    }
    return h;
}

float SampleHeightLOD(const WaterBodyInstance& body, float x, float z, double timeSeconds, int maxWaves) {
    float h = EffectiveSurfaceHeight(body, timeSeconds);
    if (maxWaves <= 0) {
        return h;
    }
    // Collect valid-wave indices, then partial-select the `keep` largest by |amplitude| (n <= 8).
    int idx[kMaxWavesPerBody];
    int n = 0;
    const uint8_t count = SafeWaveCount(body);  // never write past idx[kMaxWavesPerBody]
    for (uint8_t i = 0; i < count; ++i) {
        if (body.waves[i].wavelength > 0.0f) {
            idx[n++] = i;
        }
    }
    const int keep = std::min(maxWaves, n);
    for (int a = 0; a < keep; ++a) {
        int best = a;
        for (int b = a + 1; b < n; ++b) {
            if (std::fabs(body.waves[idx[b]].amplitude) > std::fabs(body.waves[idx[best]].amplitude)) {
                best = b;
            }
        }
        std::swap(idx[a], idx[best]);
    }
    for (int a = 0; a < keep; ++a) {
        const WaveComponent& w = body.waves[idx[a]];
        const float phase = WrapPhase(w.wavelength, DirDotPos(w, x, z), w.speed, timeSeconds);
        h += w.amplitude * DetCos(phase);
    }
    return h;
}

float SurfaceHeightAt(const WaterBodyInstance& body, float x, float z, double timeSeconds) {
    const float base = EffectiveSurfaceHeight(body, timeSeconds);
    if (body.waveCount == 0) {
        return base;
    }
    // Invert P = P0 + horizDisp(P0) for the rest point P0 given the world point (x,z).
    float x0 = x;
    float z0 = z;
    for (int it = 0; it < kInversionIterations; ++it) {
        float dx = 0.0f;
        float dz = 0.0f;
        GerstnerHorizontal(body, x0, z0, timeSeconds, dx, dz);
        x0 = x - dx;
        z0 = z - dz;
    }
    float dy = 0.0f;
    const uint8_t count = SafeWaveCount(body);
    for (uint8_t i = 0; i < count; ++i) {
        const WaveComponent& w = body.waves[i];
        if (w.wavelength <= 0.0f) {
            continue;
        }
        const float phase = WrapPhase(w.wavelength, DirDotPos(w, x0, z0), w.speed, timeSeconds);
        dy += w.amplitude * DetCos(phase);
    }
    return base + dy;
}

SurfaceSample SurfaceSampleAt(const WaterBodyInstance& body, float x, float z, double timeSeconds) {
    SurfaceSample out;
    const float base = EffectiveSurfaceHeight(body, timeSeconds);
    if (body.waveCount == 0) {
        out.height = base;
        return out;
    }
    float x0 = x;
    float z0 = z;
    for (int it = 0; it < kInversionIterations; ++it) {
        float dx = 0.0f;
        float dz = 0.0f;
        GerstnerHorizontal(body, x0, z0, timeSeconds, dx, dz);
        x0 = x - dx;
        z0 = z - dz;
    }
    // Height + analytic Gerstner normal at the inverted rest point: N = (-dP.y/dx, 1, -dP.y/dz) in the
    // closed form for a Gerstner sum.
    float dy = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    float nz = 0.0f;
    const uint8_t count = SafeWaveCount(body);
    for (uint8_t i = 0; i < count; ++i) {
        const WaveComponent& w = body.waves[i];
        if (w.wavelength <= 0.0f) {
            continue;
        }
        const float k = 2.0f * kPi / w.wavelength;
        const float phase = WrapPhase(w.wavelength, DirDotPos(w, x0, z0), w.speed, timeSeconds);
        const float wa = k * w.amplitude;
        const float c = DetCos(phase);
        const float s = DetSin(phase);
        dy += w.amplitude * c;
        nx -= w.direction[0] * wa * c;
        nz -= w.direction[1] * wa * c;
        ny -= w.steepness * wa * s;
    }
    out.height = base + dy;
    const float len = std::sqrt((nx * nx) + (ny * ny) + (nz * nz));
    if (len > 1e-8f) {
        out.normal[0] = nx / len;
        out.normal[1] = ny / len;
        out.normal[2] = nz / len;
    }
    return out;
}

}  // namespace Next::water
