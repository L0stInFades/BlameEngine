#include "next/vegetation/vegetation_cell.h"

#include <cstring>

namespace Next::vegetation {

std::vector<uint8_t> PackCell(int32_t cellX, int32_t cellZ, float cellSize,
                              const std::vector<VegetationInstance>& instances) {
    VegetationCellHeader header;
    header.instanceCount = static_cast<uint32_t>(instances.size());
    header.cellX = cellX;
    header.cellZ = cellZ;
    header.cellSize = cellSize;

    const size_t instBytes = instances.size() * sizeof(VegetationInstance);
    std::vector<uint8_t> blob(sizeof(VegetationCellHeader) + instBytes);
    std::memcpy(blob.data(), &header, sizeof(VegetationCellHeader));
    if (instBytes > 0) {
        std::memcpy(blob.data() + sizeof(VegetationCellHeader), instances.data(), instBytes);
    }
    return blob;
}

bool UnpackCell(const uint8_t* data, size_t size, VegetationCellData& out) {
    if (data == nullptr || size < sizeof(VegetationCellHeader)) {
        return false;
    }

    VegetationCellHeader header;
    std::memcpy(&header, data, sizeof(VegetationCellHeader));
    if (header.magic != kVegetationCellMagic || header.version != kVegetationCellVersion ||
        header.headerSize != sizeof(VegetationCellHeader)) {
        return false;
    }
    if (header.instanceCount > kMaxVegetationCellInstances) {
        return false;  // absurd count -> reject before sizing/allocating (also guards 32-bit overflow)
    }

    const size_t expected =
        sizeof(VegetationCellHeader) + static_cast<size_t>(header.instanceCount) * sizeof(VegetationInstance);
    if (size != expected) {
        return false;
    }

    out.header = header;
    out.instances.resize(header.instanceCount);
    if (header.instanceCount > 0) {
        std::memcpy(out.instances.data(), data + sizeof(VegetationCellHeader),
                    static_cast<size_t>(header.instanceCount) * sizeof(VegetationInstance));
    }
    return true;
}

std::vector<uint32_t> QueryRadiusXZ(const std::vector<VegetationInstance>& instances, float x, float z, float radius) {
    std::vector<uint32_t> result;
    if (!(radius > 0.0f)) {
        return result;
    }
    const float r2 = radius * radius;
    for (const VegetationInstance& inst : instances) {
        const float dx = inst.position[0] - x;
        const float dz = inst.position[2] - z;
        if (dx * dx + dz * dz <= r2) {
            result.push_back(inst.instanceId);
        }
    }
    return result;
}

}  // namespace Next::vegetation
