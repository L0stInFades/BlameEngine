// End-to-end water vertical slice (ADR-0015). Proves the WHOLE chain in one test:
//
//   cook (scene -> validate -> select -> pack -> layered Water cell)
//     -> StreamingManager::LoadCellLayer(Water) reads the real .nlc blob
//       -> WaterStreamingSystem::Sync indexes it into the authoritative WaterStore
//     -> sim: a World runs WaterForceSystem (buoyancy) BEFORE PhysicsSystem
//       -> a dropped sphere SETTLES at its Archimedes equilibrium (genuine simulation)
//       -> entering the water raises a cosmetic splash GameEvent on the boundary
//       -> WaterWorldQuery folds the surface into a Game API raycast (composed query)
//     -> unload: Sync drops the cell from the store everywhere.
//
// If this passes from a clean build, the water system is wired end to end — not just unit-green.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "next/boundary/transport.h"
#include "next/physics/components.h"
#include "next/physics/physics_system.h"
#include "next/physics/reference_physics_world.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/streaming/streaming_manager.h"
#include "next/water/water_builder.h"
#include "next/water/water_volume.h"
#include "next/water_world/water_cook.h"
#include "next/water_world/water_force_system.h"
#include "next/water_world/water_query.h"
#include "next/water_world/water_streaming_system.h"
#include "next/water_world/water_view.h"  // kWaterEventSplash

using namespace Next;
using namespace Next::physics;
using namespace Next::water;
namespace boundary = Next::boundary;
using Next::Streaming::CellCoord;
using Next::Streaming::CellData;
using Next::Streaming::CellLayer;
using Next::Streaming::StreamingManager;
using Next::Streaming::StreamingManagerConfig;

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr float kPi = 3.14159265358979323846f;

std::filesystem::path MakeTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path dir = std::filesystem::temp_directory_path() / ("next_water_slice_" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

bool PumpUntilLayerLoaded(StreamingManager& mgr, const CellCoord& coord, CellLayer layer) {
    for (int i = 0; i < 3000 && !mgr.IsCellLayerLoaded(coord, layer); ++i) {
        mgr.Update(0.016f, Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 0));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return mgr.IsCellLayerLoaded(coord, layer);
}

}  // namespace

TEST(WaterSlice, CookStreamFloatQueryEventUnload) {
    const std::filesystem::path dir = MakeTempDir();

    // ---- streaming up; one Update creates the cell grid around the origin ----
    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 128.0f;
    cfg.unloadRadius = 256.0f;
    cfg.enablePrediction = false;
    cfg.allowPlaceholderCellLoad = true;
    cfg.cellDataDirectory = dir.wstring();
    ASSERT_TRUE(mgr.Initialize(cfg));
    mgr.Update(0.016f, Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 0));
    const std::vector<CellCoord> loaded = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded.empty());
    const CellCoord coord = loaded.front();
    const CellData* preCell = mgr.GetCell(coord);
    ASSERT_NE(preCell, nullptr);
    const float cellSize = preCell->metadata.cellSize > 0.0f ? preCell->metadata.cellSize : 64.0f;

    // ---- STAGE 1: cook a still global sea (surface y=0) into the streaming directory ----
    WaterBuilder b("slice-sea");
    b.AddBody("sea", WaterType::Ocean)
        .WithBounds(-100000.0f, -50.0f, -100000.0f, 100000.0f, 0.0f, 100000.0f)
        .WithSurfaceHeight(0.0f)
        .WithDensity(1000.0f);
    const WaterSceneDef scene = b.Take();
    const WaterCookResult cooked = CookWaterCell(scene, coord.x, coord.z, cellSize);
    ASSERT_TRUE(cooked.ok);

    const std::filesystem::path resolvedDir(mgr.GetConfig().cellDataDirectory);
    std::filesystem::create_directories(resolvedDir);
    const std::filesystem::path cellFile =
        resolvedDir / ("cell_" + std::to_string(coord.x) + "_" + std::to_string(coord.z) + ".nlc");
    ASSERT_TRUE(WriteWaterCellFile(cellFile.string(), cooked.bytes));

    // ---- STAGE 2: streaming loads the Water layer via real async IO; Sync indexes it ----
    mgr.LoadCellLayer(coord, CellLayer::Water);
    ASSERT_TRUE(PumpUntilLayerLoaded(mgr, coord, CellLayer::Water));
    WaterStreamingSystem wss;
    ASSERT_GE(wss.Sync(mgr), 1u);
    ASSERT_GE(wss.Store().BodyCount(), 1u);

    // ---- STAGE 3: a World floats a dropped sphere on the streamed water ----
    World world;
    auto physics = MakeReferencePhysicsWorld();
    boundary::InProcessTransport transport;
    Next::gameapi::SimClock clock;  // the ONE authoritative clock (fixedDt defaults to 1/60 == kDt)
    WaterForceSystem waterForce(&wss.Store(), physics.get(), &clock, &transport);
    PhysicsSystem physicsSys(physics.get());
    world.RegisterSystem(&waterForce);  // before physics
    world.RegisterSystem(&physicsSys);

    const float r = 0.5f;
    Entity ball = world.CreateEntity();
    auto& t = world.AddComponent<TransformComponent>(ball);
    t.position[1] = 2.0f;  // starts dry, falls in -> splash
    RigidBodyComponent rb;
    rb.desc.motion = MotionType::Dynamic;
    rb.desc.shape = ShapeType::Sphere;
    rb.desc.halfExtents[0] = rb.desc.halfExtents[1] = rb.desc.halfExtents[2] = r;
    rb.desc.mass = 500.0f * (4.0f / 3.0f) * kPi * r * r * r;  // density 500 -> half-submerged
    rb.desc.position[1] = 2.0f;
    world.AddComponent<RigidBodyComponent>(ball, rb);

    bool gotSplash = false;
    for (int i = 0; i < 4000; ++i) {
        clock.Advance();  // advance the shared clock each fixed tick
        world.Update(kDt);
        boundary::GameEvent ev{};
        while (transport.PopEvent(ev)) {
            if (ev.type == kWaterEventSplash) {
                gotSplash = true;
            }
        }
    }

    // STAGE 3a: settled at the Archimedes equilibrium (fraction 0.5 for density 500).
    const RigidBodyComponent* rbc = world.GetComponent<RigidBodyComponent>(ball);
    ASSERT_NE(rbc, nullptr);
    float pos[3];
    float rot[4];
    physics->GetTransform(rbc->body, pos, rot);
    float frac = 0.0f;
    SubmergedSphereVolume(pos[1], r, 0.0f, -50.0f, frac);
    EXPECT_NEAR(frac, 0.5f, 0.03f) << "settledY=" << pos[1];

    // STAGE 3b: entering the water raised a cosmetic splash cue.
    EXPECT_TRUE(gotSplash);

    // ---- STAGE 4: WaterWorldQuery folds the surface into a Game API raycast ----
    WaterWorldQuery wq(&wss.Store(), &clock);
    const float origin[3] = {0.0f, 5.0f, 0.0f};
    const float down[3] = {0.0f, -1.0f, 0.0f};
    const Next::gameapi::RaycastResult hit = wq.Raycast(origin, down, 100.0f);
    EXPECT_EQ(hit.hit, 1u);
    EXPECT_NEAR(hit.point.y, 0.0f, 0.05f);  // hit the sea surface at y=0

    // ---- STAGE 5: unload drops the cell from the store everywhere ----
    mgr.UnloadCellLayer(coord, CellLayer::Water);
    wss.Sync(mgr);
    EXPECT_EQ(wss.Store().BodyCount(), 0u);

    mgr.Shutdown();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
