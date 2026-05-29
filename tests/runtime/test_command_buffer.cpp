#include <gtest/gtest.h>

#include "next/runtime/command_buffer.h"
#include "next/runtime/query.h"
#include "next/runtime/world.h"

#include <vector>

namespace Next {
namespace {

struct Health {
    int hp = 100;
};

struct Dead {};

// Recording structural changes during iteration and applying them afterwards must be safe:
// mutating archetypes directly inside ForEach would relocate rows mid-iteration.
TEST(CommandBufferTest, DeferredStructuralChangesDuringIteration) {
    World world;
    Query query(world);

    std::vector<Entity> entities;
    for (int i = 0; i < 8; ++i) {
        Entity entity = world.CreateEntity();
        Health& health = world.AddComponent<Health>(entity);
        health.hp = (i % 2 == 0) ? 0 : 50;  // even indices are "dead"
        entities.push_back(entity);
    }

    CommandBuffer commands(world);
    query.All<Health>().ForEach([&commands](Entity entity, Health& health) {
        if (health.hp == 0) {
            commands.AddComponent<Dead>(entity);
        }
    });
    EXPECT_EQ(commands.PendingCount(), 4u);

    commands.Flush();
    EXPECT_TRUE(commands.Empty());

    size_t deadCount = 0;
    for (Entity entity : entities) {
        if (world.HasComponent<Dead>(entity)) {
            ++deadCount;
        }
    }
    EXPECT_EQ(deadCount, 4u);

    // Deferred destruction of every Dead entity, recorded while iterating them.
    query.All<Dead>().ForEach([&commands](Entity entity) { commands.DestroyEntity(entity); });
    commands.Flush();

    EXPECT_EQ(world.GetEntityCount(), 4u);
    for (size_t i = 0; i < entities.size(); ++i) {
        EXPECT_EQ(world.IsEntityValid(entities[i]), (i % 2 != 0));
    }
}

TEST(CommandBufferTest, DeferredCreateEntityRunsOnFlush) {
    World world;
    CommandBuffer commands(world);

    int spawned = 0;
    commands.CreateEntity([&spawned](World& world, Entity entity) {
        world.AddComponent<Health>(entity);
        ++spawned;
    });

    EXPECT_EQ(world.GetEntityCount(), 0u);
    EXPECT_EQ(commands.PendingCount(), 1u);

    commands.Flush();

    EXPECT_EQ(spawned, 1);
    EXPECT_EQ(world.GetEntityCount(), 1u);
}

}  // namespace
}  // namespace Next
