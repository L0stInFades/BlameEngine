#include "next/water/water_cell.h"

#include <cstddef>
#include <cstring>

namespace Next::water {

// W28 migration tripwire (completeness): the identity v1->current body copy is valid only while the
// CURRENT WaterBodyInstance has the SAME field LAYOUT as the frozen v1 record — not merely the same
// size. The sizeof assert in MigrateBodyV1ToCurrent catches a size change; this offsetof chain catches a
// field REORDER at constant size (which the memcpy would otherwise silently mis-map). If any of these
// fire, the v1 layout diverged: add a frozen WaterBodyInstanceV1 + field-by-field translation.
static_assert(offsetof(WaterBodyInstance, boundsMin) < offsetof(WaterBodyInstance, boundsMax),
              "v1 layout: boundsMin<boundsMax");
static_assert(offsetof(WaterBodyInstance, boundsMax) < offsetof(WaterBodyInstance, surfaceHeight),
              "v1 layout: boundsMax<surfaceHeight");
static_assert(offsetof(WaterBodyInstance, surfaceHeight) < offsetof(WaterBodyInstance, density),
              "v1 layout: surfaceHeight<density");
static_assert(offsetof(WaterBodyInstance, density) < offsetof(WaterBodyInstance, flowVelocity),
              "v1 layout: density<flowVelocity");
static_assert(offsetof(WaterBodyInstance, flowVelocity) < offsetof(WaterBodyInstance, linearDrag),
              "v1 layout: flowVelocity<linearDrag");
static_assert(offsetof(WaterBodyInstance, linearDrag) < offsetof(WaterBodyInstance, quadraticDrag),
              "v1 layout: linearDrag<quadraticDrag");
static_assert(offsetof(WaterBodyInstance, quadraticDrag) < offsetof(WaterBodyInstance, floodRate),
              "v1 layout: quadraticDrag<floodRate");
static_assert(offsetof(WaterBodyInstance, floodRate) < offsetof(WaterBodyInstance, floodMaxHeight),
              "v1 layout: floodRate<floodMaxHeight");
static_assert(offsetof(WaterBodyInstance, floodMaxHeight) < offsetof(WaterBodyInstance, waves),
              "v1 layout: floodMaxHeight<waves");
static_assert(offsetof(WaterBodyInstance, waves) < offsetof(WaterBodyInstance, bodyId), "v1 layout: waves<bodyId");
static_assert(offsetof(WaterBodyInstance, bodyId) < offsetof(WaterBodyInstance, visual), "v1 layout: bodyId<visual");
static_assert(offsetof(WaterBodyInstance, visual) < offsetof(WaterBodyInstance, flags), "v1 layout: visual<flags");
static_assert(offsetof(WaterBodyInstance, flags) < offsetof(WaterBodyInstance, type), "v1 layout: flags<type");
static_assert(offsetof(WaterBodyInstance, type) < offsetof(WaterBodyInstance, waveCount), "v1 layout: type<waveCount");

namespace {

// Frozen v1 on-disk header (24 bytes). NEVER edit — it documents bytes already written to disk.
struct WaterCellHeaderV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t headerSize;
    uint32_t bodyCount;
    int32_t cellX;
    int32_t cellZ;
    float cellSize;
};
static_assert(sizeof(WaterCellHeaderV1) == 24, "v1 header is frozen at 24 bytes");

// Migrate one on-disk body record to the CURRENT in-memory WaterBodyInstance.
//  - The v1 record == the current layout (264 bytes), so today this is an identity copy. This is the
//    migration seam: when WaterBodyInstance changes, freeze a WaterBodyInstanceV1 struct, read the
//    record INTO it here, and translate field-by-field (new fields take their defaults). The
//    static_assert below fires the moment the current layout diverges from v1, forcing that work.
WaterBodyInstance MigrateBodyV1ToCurrent(const uint8_t* rec) {
    static_assert(sizeof(WaterBodyInstance) == kWaterBodyRecordSizeV1,
                  "v1->current migration is identity ONLY while the body layout is unchanged; freeze a "
                  "WaterBodyInstanceV1 + add field-by-field translation when this assert fires");
    WaterBodyInstance b;
    std::memcpy(&b, rec, kWaterBodyRecordSizeV1);
    return b;
}

bool UnpackCellV1(const uint8_t* data, size_t size, WaterCellData& out) {
    if (size < sizeof(WaterCellHeaderV1)) {
        return false;
    }
    WaterCellHeaderV1 h;
    std::memcpy(&h, data, sizeof(h));
    if (h.magic != kWaterCellMagic || h.version != kWaterCellVersionV1 || h.headerSize != kWaterCellHeaderSizeV1) {
        return false;
    }
    if (h.bodyCount > kMaxWaterBodiesPerCell) {
        return false;  // DoS guard: reject before sizing/allocating
    }
    const size_t stride = kWaterBodyRecordSizeV1;
    const size_t expected = sizeof(WaterCellHeaderV1) + (static_cast<size_t>(h.bodyCount) * stride);
    if (size != expected) {
        return false;
    }
    // Provenance header records the ON-DISK source; bodies are migrated to the current in-memory form.
    out.header.magic = h.magic;
    out.header.version = kWaterCellVersionV1;
    out.header.headerSize = kWaterCellHeaderSizeV1;
    out.header.bodyCount = h.bodyCount;
    out.header.cellX = h.cellX;
    out.header.cellZ = h.cellZ;
    out.header.cellSize = h.cellSize;
    out.header.bodyStride = static_cast<uint32_t>(stride);
    out.bodies.clear();
    out.bodies.reserve(h.bodyCount);
    const uint8_t* rec = data + sizeof(WaterCellHeaderV1);
    for (uint32_t i = 0; i < h.bodyCount; ++i) {
        WaterBodyInstance b = MigrateBodyV1ToCurrent(rec);
        if (b.waveCount > kMaxWavesPerBody) {
            return false;  // fail-closed: a corrupt waveCount must never drive an OOB read/write of waves[]
        }
        out.bodies.push_back(b);
        rec += stride;
    }
    return true;
}

bool UnpackCellV2(const uint8_t* data, size_t size, WaterCellData& out) {
    if (size < sizeof(WaterCellHeader)) {
        return false;
    }
    WaterCellHeader h;
    std::memcpy(&h, data, sizeof(h));
    if (h.magic != kWaterCellMagic || h.version != kWaterCellVersion || h.headerSize != kWaterCellHeaderSizeV2) {
        return false;
    }
    if (h.bodyStride != static_cast<uint32_t>(sizeof(WaterBodyInstance))) {
        return false;  // a v2 blob whose record size disagrees with this build is corrupt/incompatible
    }
    if (h.bodyCount > kMaxWaterBodiesPerCell) {
        return false;  // DoS guard: reject before sizing/allocating
    }
    const size_t stride = sizeof(WaterBodyInstance);
    const size_t expected = sizeof(WaterCellHeader) + (static_cast<size_t>(h.bodyCount) * stride);
    if (size != expected) {
        return false;
    }
    out.header = h;
    out.bodies.resize(h.bodyCount);
    if (h.bodyCount > 0) {
        std::memcpy(out.bodies.data(), data + sizeof(WaterCellHeader), static_cast<size_t>(h.bodyCount) * stride);
    }
    // Validate the untrusted per-body waveCount: it indexes waves[kMaxWavesPerBody]; a corrupt value
    // would drive an OOB read/write in every sampler. Fail-closed (consistent with the count guards above).
    for (const WaterBodyInstance& b : out.bodies) {
        if (b.waveCount > kMaxWavesPerBody) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::vector<uint8_t> PackCell(int32_t cellX, int32_t cellZ, float cellSize,
                              const std::vector<WaterBodyInstance>& bodies) {
    WaterCellHeader header;  // version/headerSize/bodyStride take their (current) defaults
    header.bodyCount = static_cast<uint32_t>(bodies.size());
    header.cellX = cellX;
    header.cellZ = cellZ;
    header.cellSize = cellSize;

    const size_t stride = sizeof(WaterBodyInstance);
    std::vector<uint8_t> out;
    out.resize(sizeof(WaterCellHeader) + (bodies.size() * stride));
    std::memcpy(out.data(), &header, sizeof(WaterCellHeader));
    if (!bodies.empty()) {
        std::memcpy(out.data() + sizeof(WaterCellHeader), bodies.data(), bodies.size() * stride);
    }
    return out;
}

std::vector<uint8_t> PackCellLegacyV1(int32_t cellX, int32_t cellZ, float cellSize,
                                      const std::vector<WaterBodyInstance>& bodies) {
    WaterCellHeaderV1 header;
    header.magic = kWaterCellMagic;
    header.version = kWaterCellVersionV1;
    header.headerSize = kWaterCellHeaderSizeV1;
    header.bodyCount = static_cast<uint32_t>(bodies.size());
    header.cellX = cellX;
    header.cellZ = cellZ;
    header.cellSize = cellSize;

    const size_t stride = kWaterBodyRecordSizeV1;
    std::vector<uint8_t> out;
    out.resize(sizeof(WaterCellHeaderV1) + (bodies.size() * stride));
    std::memcpy(out.data(), &header, sizeof(WaterCellHeaderV1));
    if (!bodies.empty()) {
        std::memcpy(out.data() + sizeof(WaterCellHeaderV1), bodies.data(), bodies.size() * stride);
    }
    return out;
}

bool UnpackCell(const uint8_t* data, size_t size, WaterCellData& out) {
    // Common minimal prefix: magic(4) + version(2). Peek the version, then dispatch to its decoder.
    if (data == nullptr || size < 6) {
        return false;
    }
    uint32_t magic = 0;
    uint16_t version = 0;
    std::memcpy(&magic, data, 4);
    std::memcpy(&version, data + 4, 2);
    if (magic != kWaterCellMagic) {
        return false;
    }
    switch (version) {
        case kWaterCellVersionV1:
            return UnpackCellV1(data, size, out);
        case kWaterCellVersion:
            return UnpackCellV2(data, size, out);
        default:
            return false;  // unknown/newer version: fail closed (no silent partial parse)
    }
}

}  // namespace Next::water
