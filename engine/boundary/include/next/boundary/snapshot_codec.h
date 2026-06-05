#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "next/boundary/snapshot.h"

// Flat, versioned WIRE encoding of the boundary messages (ADR-0006 W15). In-process the snapshot
// travels as SoA vectors (no serialization); across a process / network boundary it must become a
// byte buffer. This codec is that boundary: a header (magic + version + counts) followed by the same
// POD records packed contiguously (native-endian, like the water/vegetation cell blobs). Decoding is
// FAIL-CLOSED: any bad magic/version, a count past the ceiling (checked BEFORE sizing), or a length
// mismatch returns false — a corrupt datagram can never drive an over-read or a pathological alloc.

namespace Next::boundary {

constexpr uint32_t kSnapshotWireMagic = static_cast<uint32_t>('N') | (static_cast<uint32_t>('S') << 8u) |
                                        (static_cast<uint32_t>('N') << 16u) | (static_cast<uint32_t>('P') << 24u);
constexpr uint16_t kSnapshotWireVersion = 1;

// DoS ceiling: the most records of one kind a single decoded snapshot may claim (checked before any
// allocation). Generous for a render-view delta; a crafted header past it is rejected.
constexpr uint32_t kMaxRecordsPerSnapshot = 1u << 20;  // 1M

struct SnapshotWireHeader {
    uint32_t magic = kSnapshotWireMagic;
    uint16_t version = kSnapshotWireVersion;
    uint16_t headerSize = 56;
    uint64_t tick = 0;
    double simTimeSeconds = 0.0;
    SnapshotSequence sequence = 0;
    SnapshotSequence baselineSequence = kNoBaseline;
    uint32_t spawnCount = 0;
    uint32_t updateCount = 0;
    uint32_t despawnCount = 0;
    uint32_t reserved = 0;
};
static_assert(sizeof(SnapshotWireHeader) == 56, "SnapshotWireHeader wire layout must stay stable");

// Serialize a snapshot delta into `out` (cleared first). Deterministic; byte-stable for equal inputs.
void EncodeSnapshot(const SnapshotBlock& block, std::vector<uint8_t>& out);

// Parse a snapshot blob. FAIL-CLOSED: returns false on bad magic/version/headerSize, a record count
// past kMaxRecordsPerSnapshot, or a length that does not equal header + the three record arrays.
bool DecodeSnapshot(const uint8_t* data, size_t size, SnapshotBlock& out);

}  // namespace Next::boundary
