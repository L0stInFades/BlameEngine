// Scale / correctness-at-scale (ADR-0014). The unit tests use ~100 instances per cell, which hides
// whether queries scale. Here we build a dense multi-cell field (thousands of instances) and assert the
// broadphase-accelerated queries return EXACTLY what an O(N) brute-force scan returns — so the speedup
// is not bought with wrong answers.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "next/vegetation/vegetation_builder.h"
#include "next/vegetation/vegetation_cell.h"
#include "next/vegetation/vegetation_scatter.h"
#include "next/vegetation_world/vegetation_query.h"
#include "next/vegetation_world/vegetation_store.h"

using namespace Next::vegetation;
namespace gameapi = Next::gameapi;

namespace {

constexpr float kCellSize = 64.0f;

VegetationDef DenseForest() {
    VegetationBuilder b("scale-forest");
    b.WithMasterSeed(99).WithMaxInstancesPerCell(1000000);
    b.AddSpecies(101);
    b.WithDensity(0.05f).WithSpacing(1.0f).WithLogicalRadius(1.5f).BlocksLineOfSight();
    return b.Take();
}

size_t Populate(VegetationStore& store, int32_t cellsX, int32_t cellsZ) {
    const VegetationDef def = DenseForest();
    FlatTerrainSampler terrain;
    size_t total = 0;
    for (int32_t x = 0; x < cellsX; ++x) {
        for (int32_t z = 0; z < cellsZ; ++z) {
            const std::vector<VegetationInstance> inst = ScatterCell(def, terrain, x, z, kCellSize);
            const std::vector<uint8_t> blob = PackCell(x, z, kCellSize, inst);
            EXPECT_TRUE(store.LoadCell(x, z, blob.data(), blob.size()));
            total += inst.size();
        }
    }
    return total;
}

// O(N) reference: scan every live instance.
std::vector<VegetationKey> BruteRadius(const VegetationStore& s, float x, float z, float r) {
    std::vector<VegetationKey> out;
    for (const VegetationKey& k : s.AllLive()) {
        const VegetationInstance* p = s.Find(k);
        const float dx = p->position[0] - x;
        const float dz = p->position[2] - z;
        if (dx * dx + dz * dz <= r * r) {
            out.push_back(k);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// O(N) reference for a +x ray (same cylinder math as the query), returns nearest distance or -1.
float BruteRayPlusX(const VegetationStore& s, float ox0, float oz0, float maxDist) {
    float best = maxDist;
    bool hit = false;
    for (const VegetationKey& k : s.AllLive(VegBlocksLineOfSight)) {
        const VegetationInstance* p = s.Find(k);
        const float ox = ox0 - p->position[0];
        const float oz = oz0 - p->position[2];
        const float rr = p->logicalRadius;
        if (rr <= 0.0f) {
            continue;
        }
        const float a = 1.0f;
        const float b = 2.0f * ox;
        const float c = ox * ox + oz * oz - rr * rr;
        const float disc = b * b - 4.0f * a * c;
        if (disc < 0.0f) {
            continue;
        }
        const float sq = std::sqrt(disc);
        float t = (-b - sq) / (2.0f * a);
        if (t < 0.0f) {
            const float t1 = (-b + sq) / (2.0f * a);
            if (t1 >= 0.0f) {
                t = 0.0f;
            } else {
                continue;
            }
        }
        if (t >= 0.0f && t <= best) {
            best = t;
            hit = true;
        }
    }
    return hit ? best : -1.0f;
}

}  // namespace

TEST(VegetationScale, BroadphaseRadiusMatchesBruteForce) {
    VegetationStore store;  // 8m broadphase grid
    const size_t total = Populate(store, 8, 8);
    EXPECT_GT(total, 5000u);  // genuinely at scale: thousands of instances
    EXPECT_EQ(store.LiveInstanceCount(), total);

    const std::vector<std::pair<float, float>> points = {{50, 50}, {200, 130}, {0, 0}, {500, 500}, {37, 412}};
    for (const float r : {3.0f, 12.0f, 40.0f}) {
        for (const auto& pt : points) {
            const std::vector<VegetationKey> fast = store.QueryRadius(pt.first, pt.second, r);
            const std::vector<VegetationKey> brute = BruteRadius(store, pt.first, pt.second, r);
            EXPECT_EQ(fast, brute) << "r=" << r << " at (" << pt.first << "," << pt.second << ")";
        }
    }
}

TEST(VegetationScale, BroadphaseRaycastMatchesBruteForce) {
    VegetationStore store;
    Populate(store, 6, 6);
    VegetationWorldQuery query(&store);

    // Several long rays across the dense field; each broadphase hit-distance must equal brute force.
    const std::vector<std::pair<float, float>> origins = {{10, 40}, {5, 150}, {20, 300}, {2, 12}};
    const float d[3] = {1.0f, 0.0f, 0.0f};
    for (const auto& o : origins) {
        const float origin[3] = {o.first, 0.0f, o.second};
        const gameapi::RaycastResult r = query.Raycast(origin, d, 300.0f);
        const float brute = BruteRayPlusX(store, o.first, o.second, 300.0f);
        if (brute < 0.0f) {
            EXPECT_EQ(r.hit, 0u) << "at z=" << o.second;
        } else {
            ASSERT_EQ(r.hit, 1u) << "at z=" << o.second;
            EXPECT_NEAR(r.distance, brute, 1e-2f) << "at z=" << o.second;
        }
    }
}

TEST(VegetationScale, UnloadKeepsBroadphaseConsistent) {
    VegetationStore store;
    Populate(store, 4, 4);
    const size_t before = store.LiveInstanceCount();

    store.UnloadCell(1, 1);
    store.UnloadCell(2, 3);
    EXPECT_LT(store.LiveInstanceCount(), before);

    // After unload, a broad query still matches brute force (no dangling grid entries).
    const std::vector<VegetationKey> fast = store.QueryRadius(150.0f, 150.0f, 50.0f);
    const std::vector<VegetationKey> brute = BruteRadius(store, 150.0f, 150.0f, 50.0f);
    EXPECT_EQ(fast, brute);
    for (const VegetationKey& k : fast) {
        EXPECT_FALSE(k.cellX == 1 && k.cellZ == 1);  // unloaded cells never appear
        EXPECT_FALSE(k.cellX == 2 && k.cellZ == 3);
    }
}
