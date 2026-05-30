#include "next/runtime/world.h"
#include "next/runtime/entity.h"
#include "next/runtime/component.h"
#include "next/foundation/logger.h"
#include <gtest/gtest.h>

namespace Next {
namespace testing {

using ::testing::Test;

class WorldTest : public Test {
protected:
    void SetUp() override { Logger::Initialize(); }

    void TearDown() override { Logger::Shutdown(); }

    World world_;
};

// Test world initialization
TEST_F(WorldTest, Initialization) {
    EXPECT_EQ(world_.GetEntityCount(), 0u);
}

// Reusing one index past the 16-bit version wrap must never yield version 0 (the invalid
// sentinel): the recreated entity has to stay valid across the 65536th reuse.
TEST_F(WorldTest, EntityVersionWrapStaysValid) {
    Entity e;
    for (int i = 0; i < 70000; ++i) {
        e = world_.CreateEntity();
        ASSERT_TRUE(e.IsValid()) << "invalid at iteration " << i << " (version " << e.version << ")";
        ASSERT_TRUE(world_.IsEntityValid(e)) << "not tracked at iteration " << i;
        world_.DestroyEntity(e);
    }
}

// Test entity creation
TEST_F(WorldTest, CreateEntity) {
    Entity entity = world_.CreateEntity();

    EXPECT_TRUE(entity.IsValid());
    EXPECT_TRUE(world_.IsEntityValid(entity));
    EXPECT_EQ(world_.GetEntityCount(), 1u);
}

// Test multiple entity creation
TEST_F(WorldTest, CreateMultipleEntities) {
    const size_t count = 100;

    for (size_t i = 0; i < count; ++i) {
        Entity entity = world_.CreateEntity();
        EXPECT_TRUE(entity.IsValid());
    }

    EXPECT_EQ(world_.GetEntityCount(), count);
}

// Test entity destruction
TEST_F(WorldTest, DestroyEntity) {
    Entity entity = world_.CreateEntity();

    EXPECT_TRUE(world_.IsEntityValid(entity));
    EXPECT_EQ(world_.GetEntityCount(), 1u);

    world_.DestroyEntity(entity);

    EXPECT_FALSE(world_.IsEntityValid(entity));
    EXPECT_EQ(world_.GetEntityCount(), 0u);
}

// Test add component
TEST_F(WorldTest, AddComponent) {
    Entity entity = world_.CreateEntity();

    TransformComponent& transform = world_.AddComponent<TransformComponent>(entity);

    EXPECT_TRUE(world_.HasComponent<TransformComponent>(entity));

    TransformComponent* retrieved = world_.GetComponent<TransformComponent>(entity);
    EXPECT_NE(retrieved, nullptr);
    EXPECT_EQ(&transform, retrieved);
}

// Test add component with value
TEST_F(WorldTest, AddComponentWithValue) {
    Entity entity = world_.CreateEntity();

    TransformComponent original;
    original.position[0] = 1.0f;
    original.position[1] = 2.0f;
    original.position[2] = 3.0f;
    original.scale[0] = 2.0f;
    original.scale[1] = 2.0f;
    original.scale[2] = 2.0f;

    world_.AddComponent<TransformComponent>(entity, original);

    TransformComponent* retrieved = world_.GetComponent<TransformComponent>(entity);
    EXPECT_NE(retrieved, nullptr);
    EXPECT_FLOAT_EQ(retrieved->position[0], 1.0f);
    EXPECT_FLOAT_EQ(retrieved->position[1], 2.0f);
    EXPECT_FLOAT_EQ(retrieved->position[2], 3.0f);
    EXPECT_FLOAT_EQ(retrieved->scale[0], 2.0f);
    EXPECT_FLOAT_EQ(retrieved->scale[1], 2.0f);
    EXPECT_FLOAT_EQ(retrieved->scale[2], 2.0f);
}

// Test get component
TEST_F(WorldTest, GetComponent) {
    Entity entity = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(entity);

    TransformComponent* transform = world_.GetComponent<TransformComponent>(entity);
    EXPECT_NE(transform, nullptr);
}

// Test get non-existent component
TEST_F(WorldTest, GetNonExistentComponent) {
    Entity entity = world_.CreateEntity();

    TransformComponent* transform = world_.GetComponent<TransformComponent>(entity);
    EXPECT_EQ(transform, nullptr);
}

// Test has component
TEST_F(WorldTest, HasComponent) {
    Entity entity = world_.CreateEntity();

    EXPECT_FALSE(world_.HasComponent<TransformComponent>(entity));

    world_.AddComponent<TransformComponent>(entity);

    EXPECT_TRUE(world_.HasComponent<TransformComponent>(entity));
}

// Test remove component
TEST_F(WorldTest, RemoveComponent) {
    Entity entity = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(entity);
    EXPECT_TRUE(world_.HasComponent<TransformComponent>(entity));

    world_.RemoveComponent<TransformComponent>(entity);
    EXPECT_FALSE(world_.HasComponent<TransformComponent>(entity));
}

// Test multiple components on same entity
TEST_F(WorldTest, MultipleComponentsOnEntity) {
    Entity entity = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(entity);
    world_.AddComponent<NameComponent>(entity);
    world_.AddComponent<HierarchyComponent>(entity);

    EXPECT_TRUE(world_.HasComponent<TransformComponent>(entity));
    EXPECT_TRUE(world_.HasComponent<NameComponent>(entity));
    EXPECT_TRUE(world_.HasComponent<HierarchyComponent>(entity));
}

// Test query entities with components
TEST_F(WorldTest, QueryEntitiesWith) {
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();
    Entity e3 = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<NameComponent>(e1);

    world_.AddComponent<TransformComponent>(e2);

    world_.AddComponent<NameComponent>(e3);

    // Query entities with both TransformComponent and NameComponent
    auto results = world_.QueryEntitiesWith<TransformComponent, NameComponent>();

    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], e1);
}

// Test query entities with single component
TEST_F(WorldTest, QueryEntitiesWithSingle) {
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();
    Entity e3 = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<TransformComponent>(e2);

    // e3 has no TransformComponent

    auto results = world_.QueryEntitiesWith<TransformComponent>();

    EXPECT_EQ(results.size(), 2u);
}

// Test query empty result
TEST_F(WorldTest, QueryEmptyResult) {
    Entity entity = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(entity);

    auto results = world_.QueryEntitiesWith<NameComponent>();

    EXPECT_EQ(results.size(), 0u);
}

// Test world statistics
TEST_F(WorldTest, WorldStats) {
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<NameComponent>(e1);
    world_.AddComponent<TransformComponent>(e2);

    World::WorldStats stats = world_.GetStats();

    EXPECT_EQ(stats.entityCount, 2u);
    EXPECT_GT(stats.totalComponents, 0u);
}

// Test get all entities
TEST_F(WorldTest, GetAllEntities) {
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();
    Entity e3 = world_.CreateEntity();

    auto entities = world_.GetAllEntities();

    EXPECT_EQ(entities.size(), 3u);
}

// Test destroy entity removes components
TEST_F(WorldTest, DestroyEntityRemovesComponents) {
    Entity entity = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(entity);
    world_.AddComponent<NameComponent>(entity);

    EXPECT_TRUE(world_.HasComponent<TransformComponent>(entity));
    EXPECT_TRUE(world_.HasComponent<NameComponent>(entity));

    world_.DestroyEntity(entity);

    EXPECT_FALSE(world_.HasComponent<TransformComponent>(entity));
    EXPECT_FALSE(world_.HasComponent<NameComponent>(entity));
}

// Test name component
TEST_F(WorldTest, NameComponent) {
    Entity entity = world_.CreateEntity();

    NameComponent name("TestEntity");

    world_.AddComponent<NameComponent>(entity, name);

    NameComponent* retrieved = world_.GetComponent<NameComponent>(entity);
    EXPECT_NE(retrieved, nullptr);
    EXPECT_STREQ(retrieved->name, "TestEntity");
}

// Test hierarchy component
TEST_F(WorldTest, HierarchyComponent) {
    Entity parent = world_.CreateEntity();
    Entity child1 = world_.CreateEntity();
    Entity child2 = world_.CreateEntity();

    HierarchyComponent hierarchy;
    hierarchy.parent = parent;

    world_.AddComponent<HierarchyComponent>(parent, hierarchy);

    HierarchyComponent* retrieved = world_.GetComponent<HierarchyComponent>(parent);
    EXPECT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->parent, parent);
    EXPECT_EQ(retrieved->childCount, 0u);
}

// Test entity reuse
TEST_F(WorldTest, EntityReuse) {
    Entity e1 = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(e1);
    e1.id = 42;
    e1.version = 1;

    uint64_t originalID = e1.id;
    uint64_t originalVersion = e1.version;

    world_.DestroyEntity(e1);

    Entity e2 = world_.CreateEntity();

    // IDs can be reused but versions should differ
    // or new IDs should be allocated
    EXPECT_TRUE(e2.IsValid() && e2.id != 0);
}

// Test stress test many entities
TEST_F(WorldTest, StressTestManyEntities) {
    const size_t count = 1000;

    std::vector<Entity> entities;
    for (size_t i = 0; i < count; ++i) {
        Entity entity = world_.CreateEntity();
        world_.AddComponent<TransformComponent>(entity);
        entities.push_back(entity);
    }

    EXPECT_EQ(world_.GetEntityCount(), count);

    // Verify all entities are valid
    for (const auto& entity : entities) {
        EXPECT_TRUE(world_.IsEntityValid(entity));
        EXPECT_TRUE(world_.HasComponent<TransformComponent>(entity));
    }
}

// Test world update
TEST_F(WorldTest, WorldUpdate) {
    Entity entity = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(entity);

    // Update should not crash
    world_.Update(0.016f);

    SUCCEED();
}

// Test add transform legacy
TEST_F(WorldTest, AddTransformLegacy) {
    Entity entity = world_.CreateEntity();

    TransformComponent* transform = world_.AddTransform(entity);

    EXPECT_NE(transform, nullptr);
    EXPECT_TRUE(world_.HasComponent<TransformComponent>(entity));
}

// Test get transform legacy
TEST_F(WorldTest, GetTransformLegacy) {
    Entity entity = world_.CreateEntity();

    world_.AddTransform(entity);

    TransformComponent* transform = world_.GetTransform(entity);

    EXPECT_NE(transform, nullptr);
}

}  // namespace testing
}  // namespace Next

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
