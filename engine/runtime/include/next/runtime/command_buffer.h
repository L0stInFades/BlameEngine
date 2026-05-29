#pragma once

#include "next/runtime/world.h"

#include <functional>
#include <utility>
#include <vector>

namespace Next {

// Records structural changes (add/remove component, create/destroy entity) and applies them
// later via Flush(). Use it to mutate structure safely while iterating a Query: archetype
// migration relocates rows, so adding/removing a component or destroying an entity *during*
// Query::ForEach would invalidate the in-progress iteration. Record the change into a
// CommandBuffer instead and call Flush() once the iteration has completed.
class CommandBuffer {
public:
    explicit CommandBuffer(World& world) : world_(world) {}

    template<typename T>
    void AddComponent(Entity entity, const T& component) {
        commands_.emplace_back([entity, component](World& world) { world.AddComponent<T>(entity, component); });
    }

    template<typename T>
    void AddComponent(Entity entity) {
        commands_.emplace_back([entity](World& world) { world.AddComponent<T>(entity); });
    }

    template<typename T>
    void RemoveComponent(Entity entity) {
        commands_.emplace_back([entity](World& world) { world.RemoveComponent<T>(entity); });
    }

    void DestroyEntity(Entity entity) {
        commands_.emplace_back([entity](World& world) { world.DestroyEntity(entity); });
    }

    // A deferred entity creation cannot return the id (it does not exist yet), so the new
    // entity is delivered to a callback when the command is flushed.
    void CreateEntity(std::function<void(World&, Entity)> onCreated) {
        commands_.emplace_back([callback = std::move(onCreated)](World& world) {
            const Entity created = world.CreateEntity();
            if (callback) {
                callback(world, created);
            }
        });
    }

    size_t PendingCount() const { return commands_.size(); }
    bool Empty() const { return commands_.empty(); }

    // Apply every recorded command in record order, then clear the buffer.
    void Flush() {
        for (auto& command : commands_) {
            command(world_);
        }
        commands_.clear();
    }

private:
    World& world_;
    std::vector<std::function<void(World&)>> commands_;
};

}  // namespace Next
