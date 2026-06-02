// Adversarial parser fuzz (ADR-0014). Hand-picked corruption cases miss things; here we throw random,
// truncated, and bit-flipped bytes at every parser — UnpackCell, ParseLayeredCell, ParseVegetationDefText
// — and require (a) no crash / OOB / UB (AddressSanitizer + UBSan enforce this) and (b) fail-closed
// behavior (false / empty, never a partial read). Deterministic RNG so a failure reproduces.

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "next/streaming/layered_cell_file.h"
#include "next/vegetation/vegetation_builder.h"
#include "next/vegetation/vegetation_cell.h"
#include "next/vegetation/vegetation_scatter.h"
#include "next/vegetation_world/vegetation_cook.h"

using namespace Next::vegetation;
using Next::Streaming::CellFileCompression;
using Next::Streaming::CellLayer;
using Next::Streaming::LayeredCellChunk;
using Next::Streaming::LayeredCellChunkInput;
using Next::Streaming::PackLayeredCell;
using Next::Streaming::ParseLayeredCell;

namespace {

std::vector<uint8_t> ValidVegBlob() {
    VegetationBuilder b("fuzz");
    b.WithMasterSeed(1);
    b.AddSpecies(1);
    b.WithDensity(0.05f).WithSpacing(0.0f).WithLogicalRadius(1.0f);
    FlatTerrainSampler terrain;
    const std::vector<VegetationInstance> inst = ScatterCell(b.Def(), terrain, 0, 0, 64.0f);
    return PackCell(0, 0, 64.0f, inst);
}

std::vector<uint8_t> ValidLayeredBlob() {
    LayeredCellChunkInput c;
    c.layer = CellLayer::Vegetation;
    c.codec = CellFileCompression::None;
    c.data = ValidVegBlob();
    std::vector<LayeredCellChunkInput> chunks;
    chunks.push_back(std::move(c));
    return PackLayeredCell(chunks);
}

}  // namespace

TEST(VegetationFuzz, ParsersSurviveRandomInput) {
    std::mt19937 rng(12345u);
    std::uniform_int_distribution<int> lenDist(0, 512);
    std::uniform_int_distribution<int> byteDist(0, 255);

    for (int i = 0; i < 5000; ++i) {
        std::vector<uint8_t> buf(static_cast<size_t>(lenDist(rng)));
        for (uint8_t& b : buf) {
            b = static_cast<uint8_t>(byteDist(rng));
        }
        VegetationCellData cellOut;
        (void)UnpackCell(buf.data(), buf.size(), cellOut);  // must not crash / read OOB
        std::vector<LayeredCellChunk> chunks;
        (void)ParseLayeredCell(buf.data(), buf.size(), chunks);
        VegetationDef def;
        std::string err;
        const std::string text(buf.begin(), buf.end());
        (void)ParseVegetationDefText(text, def, err);
    }
    SUCCEED();  // reaching here under ASan/UBSan == the parsers stayed in bounds
}

TEST(VegetationFuzz, TruncationIsFailClosed) {
    const std::vector<uint8_t> veg = ValidVegBlob();
    for (size_t n = 0; n < veg.size(); ++n) {
        VegetationCellData out;
        EXPECT_FALSE(UnpackCell(veg.data(), n, out)) << "truncated veg blob parsed at n=" << n;
    }
    {
        VegetationCellData out;
        EXPECT_TRUE(UnpackCell(veg.data(), veg.size(), out));  // only the whole blob parses
    }

    const std::vector<uint8_t> layered = ValidLayeredBlob();
    for (size_t n = 0; n < layered.size(); ++n) {
        std::vector<LayeredCellChunk> chunks;
        EXPECT_FALSE(ParseLayeredCell(layered.data(), n, chunks)) << "truncated layered blob parsed at n=" << n;
    }
    {
        std::vector<LayeredCellChunk> chunks;
        EXPECT_TRUE(ParseLayeredCell(layered.data(), layered.size(), chunks));
    }
}

TEST(VegetationFuzz, BitFlipsNeverCrash) {
    std::mt19937 rng(777u);
    const std::vector<uint8_t> layered = ValidLayeredBlob();
    const std::vector<uint8_t> veg = ValidVegBlob();
    ASSERT_FALSE(layered.empty());
    ASSERT_FALSE(veg.empty());

    std::uniform_int_distribution<size_t> layeredPos(0, layered.size() - 1);
    std::uniform_int_distribution<size_t> vegPos(0, veg.size() - 1);
    std::uniform_int_distribution<int> bitDist(0, 7);

    for (int i = 0; i < 5000; ++i) {
        const int flips = 1 + (i % 4);

        std::vector<uint8_t> ml = layered;
        for (int f = 0; f < flips; ++f) {
            ml[layeredPos(rng)] ^= static_cast<uint8_t>(1u << bitDist(rng));
        }
        std::vector<LayeredCellChunk> chunks;
        (void)ParseLayeredCell(ml.data(), ml.size(), chunks);  // parse-or-reject, never OOB

        std::vector<uint8_t> mv = veg;
        for (int f = 0; f < flips; ++f) {
            mv[vegPos(rng)] ^= static_cast<uint8_t>(1u << bitDist(rng));
        }
        VegetationCellData out;
        (void)UnpackCell(mv.data(), mv.size(), out);
    }
    SUCCEED();
}
