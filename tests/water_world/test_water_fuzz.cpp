// Adversarial water parser fuzz (ADR-0015): random / truncated / header-corrupted bytes vs UnpackCell,
// and random / truncated text vs ParseWaterDefText. Everything must FAIL-CLOSED (return false, or yield
// a scene the validator rejects) and NEVER crash / read out of bounds — this file runs under ASan/UBSan
// in CI, so a memory error here fails the build. Deterministic in-test RNG (no std::rand).

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "next/water/water_cell.h"
#include "next/water/water_def.h"
#include "next/water/water_validator.h"
#include "next/water_world/water_cook.h"

using namespace Next::water;

namespace {

uint64_t Next64(uint64_t& s) {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30u)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27u)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31u);
}

std::vector<uint8_t> ValidBlob() {
    WaterBodyInstance a;
    a.bodyId = 1;
    a.surfaceHeight = 0.0f;
    a.waveCount = 2;
    a.waves[0] = {0.3f, 5.0f, {1.0f, 0.0f}, 1.0f, 0.2f};
    a.waves[1] = {0.1f, 3.0f, {0.0f, 1.0f}, 0.8f, 0.1f};
    WaterBodyInstance b = a;
    b.bodyId = 2;
    return PackCell(3, -2, 64.0f, {a, b});
}

}  // namespace

TEST(WaterFuzz, UnpackCellRejectsRandomBuffers) {
    uint64_t s = 0xC0FFEEull;
    for (int iter = 0; iter < 5000; ++iter) {
        const size_t len = static_cast<size_t>(Next64(s) % 600u);
        std::vector<uint8_t> buf(len);
        for (size_t i = 0; i < len; ++i) {
            buf[i] = static_cast<uint8_t>(Next64(s) & 0xFFu);
        }
        WaterCellData out;
        // Must not crash or read OOB (ASan). If it claims success, the size must be exactly consistent
        // with the ON-DISK header it reported (version-agnostic: works for any migrated version).
        if (UnpackCell(buf.data(), buf.size(), out)) {
            EXPECT_EQ(buf.size(), out.header.headerSize + out.bodies.size() * out.header.bodyStride);
        }
    }
}

TEST(WaterFuzz, UnpackCellTruncationFailsClosed) {
    const std::vector<uint8_t> blob = ValidBlob();
    WaterCellData ok;
    ASSERT_TRUE(UnpackCell(blob.data(), blob.size(), ok));  // baseline valid
    for (size_t len = 0; len < blob.size(); ++len) {
        WaterCellData out;
        EXPECT_FALSE(UnpackCell(blob.data(), len, out)) << "len=" << len;  // any short read rejected
    }
    // A trailing extra byte also breaks the exact-size contract.
    std::vector<uint8_t> longer = blob;
    longer.push_back(0x00);
    WaterCellData out;
    EXPECT_FALSE(UnpackCell(longer.data(), longer.size(), out));
}

TEST(WaterFuzz, UnpackCellHeaderCorruptionFailsClosed) {
    const std::vector<uint8_t> base = ValidBlob();
    // Corrupt the magic (byte 0).
    {
        std::vector<uint8_t> bad = base;
        bad[0] ^= 0xFF;
        WaterCellData out;
        EXPECT_FALSE(UnpackCell(bad.data(), bad.size(), out));
    }
    // Corrupt the version (bytes 4-5).
    {
        std::vector<uint8_t> bad = base;
        bad[4] = 0xEE;
        WaterCellData out;
        EXPECT_FALSE(UnpackCell(bad.data(), bad.size(), out));
    }
    // Set an enormous bodyCount (bytes 8-11) — must be rejected before any allocation.
    {
        std::vector<uint8_t> bad = base;
        bad[8] = 0xFF;
        bad[9] = 0xFF;
        bad[10] = 0xFF;
        bad[11] = 0xFF;
        WaterCellData out;
        EXPECT_FALSE(UnpackCell(bad.data(), bad.size(), out));
    }
}

TEST(WaterFuzz, ParseWaterDefTextNeverCrashes) {
    const std::string valid =
        "scene s\n"
        "body p pool\n"
        "  bounds -5 -2 -5 5 0 5\n"
        "  surface 0\n"
        "  density 1000\n"
        "  wave 0.5 10 1 0 2 0.3\n";

    // Truncate the valid text at every prefix length.
    for (size_t len = 0; len <= valid.size(); ++len) {
        WaterSceneDef scene;
        std::string err;
        const bool ok = ParseWaterDefText(valid.substr(0, len), scene, err);
        if (ok) {
            // If it parsed, validating it must also not crash (and is allowed to reject).
            (void)WaterValidator::Validate(scene);
        }
    }

    // Random byte soup as "text".
    uint64_t s = 0x1234567ull;
    for (int iter = 0; iter < 2000; ++iter) {
        const size_t len = static_cast<size_t>(Next64(s) % 200u);
        std::string junk(len, ' ');
        for (size_t i = 0; i < len; ++i) {
            junk[i] = static_cast<char>(Next64(s) & 0x7Fu);  // printable-ish ASCII incl. newlines/digits
        }
        WaterSceneDef scene;
        std::string err;
        if (ParseWaterDefText(junk, scene, err)) {
            (void)WaterValidator::Validate(scene);
        }
    }
}
