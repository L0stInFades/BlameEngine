#include "next/boundary/snapshot_codec.h"

#include <cstring>

namespace Next::boundary {

void EncodeSnapshot(const SnapshotBlock& block, std::vector<uint8_t>& out) {
    SnapshotWireHeader h;
    h.tick = block.tick;
    h.simTimeSeconds = block.simTimeSeconds;
    h.sequence = block.sequence;
    h.baselineSequence = block.baselineSequence;
    h.spawnCount = static_cast<uint32_t>(block.spawns.size());
    h.updateCount = static_cast<uint32_t>(block.updates.size());
    h.despawnCount = static_cast<uint32_t>(block.despawns.size());

    const size_t spawnBytes = block.spawns.size() * sizeof(SpawnRecord);
    const size_t updateBytes = block.updates.size() * sizeof(UpdateRecord);
    const size_t despawnBytes = block.despawns.size() * sizeof(EntityId);

    out.clear();
    out.resize(sizeof(SnapshotWireHeader) + spawnBytes + updateBytes + despawnBytes);
    uint8_t* p = out.data();
    std::memcpy(p, &h, sizeof(h));
    p += sizeof(h);
    if (spawnBytes != 0) {
        std::memcpy(p, block.spawns.data(), spawnBytes);
        p += spawnBytes;
    }
    if (updateBytes != 0) {
        std::memcpy(p, block.updates.data(), updateBytes);
        p += updateBytes;
    }
    if (despawnBytes != 0) {
        std::memcpy(p, block.despawns.data(), despawnBytes);
    }
}

bool DecodeSnapshot(const uint8_t* data, size_t size, SnapshotBlock& out) {
    if (data == nullptr || size < sizeof(SnapshotWireHeader)) {
        return false;
    }
    SnapshotWireHeader h;
    std::memcpy(&h, data, sizeof(h));
    if (h.magic != kSnapshotWireMagic || h.version != kSnapshotWireVersion ||
        h.headerSize != sizeof(SnapshotWireHeader)) {
        return false;
    }
    // DoS guard: reject crafted counts BEFORE computing sizes / allocating.
    if (h.spawnCount > kMaxRecordsPerSnapshot || h.updateCount > kMaxRecordsPerSnapshot ||
        h.despawnCount > kMaxRecordsPerSnapshot) {
        return false;
    }
    const size_t spawnBytes = static_cast<size_t>(h.spawnCount) * sizeof(SpawnRecord);
    const size_t updateBytes = static_cast<size_t>(h.updateCount) * sizeof(UpdateRecord);
    const size_t despawnBytes = static_cast<size_t>(h.despawnCount) * sizeof(EntityId);
    const size_t expected = sizeof(SnapshotWireHeader) + spawnBytes + updateBytes + despawnBytes;
    if (size != expected) {
        return false;  // exact-size contract: no trailing garbage, no truncation
    }

    out.Reset(h.tick, h.simTimeSeconds);
    out.sequence = h.sequence;
    out.baselineSequence = h.baselineSequence;
    out.spawns.resize(h.spawnCount);
    out.updates.resize(h.updateCount);
    out.despawns.resize(h.despawnCount);
    const uint8_t* p = data + sizeof(SnapshotWireHeader);
    if (spawnBytes != 0) {
        std::memcpy(out.spawns.data(), p, spawnBytes);
        p += spawnBytes;
    }
    if (updateBytes != 0) {
        std::memcpy(out.updates.data(), p, updateBytes);
        p += updateBytes;
    }
    if (despawnBytes != 0) {
        std::memcpy(out.despawns.data(), p, despawnBytes);
    }
    return true;
}

}  // namespace Next::boundary
