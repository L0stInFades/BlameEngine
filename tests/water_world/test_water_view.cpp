// Water view contract tests (ADR-0015): the Mock UE5 consumer ingests the SAME Water blob bytes (proving
// the streamed bytes carry the full surface parameters UE5 needs), and authoritative contacts marshal
// into boundary GameEvents (cosmetic).

#include <gtest/gtest.h>

#include <vector>

#include "next/water/water_cell.h"
#include "next/water/water_def.h"
#include "next/water_world/water_view.h"

using namespace Next::water;
namespace boundary = Next::boundary;

namespace {

WaterBodyInstance MakeWavyBody(uint32_t id, uint8_t waveCount) {
    WaterBodyInstance b;
    b.bodyId = id;
    b.type = static_cast<uint8_t>(WaterType::Ocean);
    b.surfaceHeight = 0.0f;
    b.density = 1025.0f;
    b.waveCount = waveCount;
    for (uint8_t i = 0; i < waveCount; ++i) {
        b.waves[i] = {0.5f, 10.0f + i, {1.0f, 0.0f}, 2.0f, 0.1f};
    }
    return b;
}

}  // namespace

TEST(WaterView, ConsumerReceivesFullSurfaceParams) {
    MockWaterConsumer consumer;
    std::vector<WaterBodyInstance> bodies{MakeWavyBody(1, 3), MakeWavyBody(2, 2)};
    const std::vector<uint8_t> blob = PackCell(0, 0, 64.0f, bodies);
    ASSERT_TRUE(consumer.OnCellLoaded(0, 0, blob.data(), blob.size()));
    EXPECT_EQ(consumer.LoadedCellCount(), 1u);
    EXPECT_EQ(consumer.DistinctBodyCount(), 2u);
    EXPECT_EQ(consumer.TotalWaveCount(), 5u);  // 3 + 2 — the wave params survived the wire
    const WaterBodyInstance* p = consumer.SurfaceParamsForBody(1);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->waveCount, 3u);
    EXPECT_FLOAT_EQ(p->density, 1025.0f);
    consumer.OnCellUnloaded(0, 0);
    EXPECT_EQ(consumer.LoadedCellCount(), 0u);
    EXPECT_EQ(consumer.DistinctBodyCount(), 0u);
}

TEST(WaterView, FailClosedOnBadBlob) {
    MockWaterConsumer consumer;
    const std::vector<uint8_t> junk(40, 0xAB);
    EXPECT_FALSE(consumer.OnCellLoaded(0, 0, junk.data(), junk.size()));
}

TEST(WaterView, ContactMarshalsToBoundaryEvent) {
    WaterContactEvent enter;
    enter.entered = true;
    enter.entity = 42;
    enter.position[0] = 1.0f;
    enter.position[1] = 0.0f;
    enter.position[2] = -3.0f;
    enter.speed = 7.5f;
    const boundary::GameEvent splash = ToBoundaryEvent(enter);
    EXPECT_EQ(splash.type, kWaterEventSplash);
    EXPECT_EQ(splash.subject, 42u);
    EXPECT_FLOAT_EQ(splash.params[0], 1.0f);
    EXPECT_FLOAT_EQ(splash.params[3], 7.5f);  // splash intensity

    WaterContactEvent exit = enter;
    exit.entered = false;
    EXPECT_EQ(ToBoundaryEvent(exit).type, kWaterEventExit);
}
