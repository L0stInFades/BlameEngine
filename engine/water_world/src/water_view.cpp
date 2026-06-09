#include "next/water_world/water_view.h"

#include <set>

#include "next/water/water_cell.h"
#include "next/water/water_surface.h"

namespace Next::water {

Next::boundary::GameEvent ToBoundaryEvent(const WaterContactEvent& ev) {
    Next::boundary::GameEvent e{};
    e.type = ev.entered ? kWaterEventSplash : kWaterEventExit;
    e.subject = ev.entity;
    e.params[0] = ev.position[0];
    e.params[1] = ev.position[1];
    e.params[2] = ev.position[2];
    e.params[3] = ev.speed;  // splash intensity
    return e;
}

bool MockWaterConsumer::OnCellLoaded(int32_t cellX, int32_t cellZ, const uint8_t* waterBlob, size_t size) {
    WaterCellData parsed;
    if (!UnpackCell(waterBlob, size, parsed)) {
        return false;  // fail-closed
    }
    cells_[CellKey{cellX, cellZ}] = std::move(parsed.bodies);
    return true;
}

void MockWaterConsumer::OnCellUnloaded(int32_t cellX, int32_t cellZ) {
    cells_.erase(CellKey{cellX, cellZ});
}

size_t MockWaterConsumer::TotalBodyRecords() const {
    size_t total = 0;
    for (const auto& cell : cells_) {
        total += cell.second.size();
    }
    return total;
}

size_t MockWaterConsumer::DistinctBodyCount() const {
    std::set<uint32_t> ids;
    for (const auto& cell : cells_) {
        for (const WaterBodyInstance& b : cell.second) {
            ids.insert(b.bodyId);
        }
    }
    return ids.size();
}

size_t MockWaterConsumer::TotalWaveCount() const {
    size_t total = 0;
    for (const auto& cell : cells_) {
        for (const WaterBodyInstance& b : cell.second) {
            total += b.waveCount;
        }
    }
    return total;
}

const WaterBodyInstance* MockWaterConsumer::SurfaceParamsForBody(uint32_t bodyId) const {
    for (const auto& cell : cells_) {
        for (const WaterBodyInstance& b : cell.second) {
            if (b.bodyId == bodyId) {
                return &b;
            }
        }
    }
    return nullptr;
}

bool MockWaterConsumer::EvaluateSurface(uint32_t bodyId, float x, float z, double timeSeconds,
                                        SurfaceSample& out) const {
    const WaterBodyInstance* b = SurfaceParamsForBody(bodyId);
    if (b == nullptr) {
        return false;
    }
    out = SurfaceSampleAt(*b, x, z, timeSeconds);  // SAME authoritative Gerstner the sim uses
    return true;
}

bool MockWaterConsumer::EvaluateSurfaceHeightLOD(uint32_t bodyId, float x, float z, double timeSeconds, int maxWaves,
                                                 float& outHeight) const {
    const WaterBodyInstance* b = SurfaceParamsForBody(bodyId);
    if (b == nullptr) {
        return false;
    }
    outHeight = SampleHeightLOD(*b, x, z, timeSeconds, maxWaves);
    return true;
}

}  // namespace Next::water
