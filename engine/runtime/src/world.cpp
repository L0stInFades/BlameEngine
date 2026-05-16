#include "next/runtime/world.h"
#include "next/foundation/logger.h"
#include "next/profiler/cpu_scope.h"
#include <algorithm>

namespace Next {

World::World() : nextEntityID_(1) {
    NEXT_LOG_INFO("World initialized (CP4 ECS)");
}

World::~World() {
    // Clean up all systems
    // NOTE: World does NOT own system pointers. Users must manage system lifetime.
    // Systems can be stack-allocated or heap-allocated.
    // We only call Shutdown() here, never delete.
    // Make a copy of systems vector to avoid issues if Shutdown() modifies the vector
    std::vector<System*> systemsCopy;
    systems_.swap(systemsCopy);

    for (auto* system : systemsCopy) {
        if (system) {
            // IMPORTANT: Don't access system members as the system may already be destroyed
            // (e.g., if it was stack-allocated and went out of scope)
            // We only check if the pointer is non-null before potentially calling Shutdown.
            // However, calling Shutdown() on a destroyed system is undefined behavior,
            // so we should NOT call it here. The system owner is responsible for cleanup.
        }
    }

    // Clean up entities and components
    entities_.clear();
    entityComponents_.clear();
    componentArrays_.clear();
    entityMetadata_.clear();

    NEXT_LOG_INFO("World destroyed");
}

Entity World::CreateEntity() {
    uint64_t id;
    uint16_t version = 1;

    // Reuse free IDs if available
    if (!freeIDs_.empty()) {
        id = freeIDs_.front();
        freeIDs_.pop_front();
        version = entityMetadata_[id].version + 1;
    } else {
        id = nextEntityID_++;
    }

    Entity entity(id, version);

    // Track entity
    entities_.insert(entity);
    entityMetadata_[id] = {version, true};

    NEXT_LOG_TRACE("Created entity: %llu (version: %u)", id, version);

    // Notify systems
    NotifyEntityCreated(entity);

    return entity;
}

void World::DestroyEntity(Entity entity) {
    if (!IsEntityValid(entity)) {
        return;
    }

    // Remove all components
    auto it = entityComponents_.find(entity);
    if (it != entityComponents_.end()) {
        for (ComponentTypeID typeID : it->second) {
            auto arrayIt = componentArrays_.find(typeID);
            if (arrayIt != componentArrays_.end()) {
                arrayIt->second->RemoveEntity(entity);
            }
        }
        entityComponents_.erase(it);
    }

    // Mark as invalid
    entities_.erase(entity);
    entityMetadata_[entity.id].alive = false;

    // Add ID to free list
    freeIDs_.push_back(entity.id);

    NEXT_LOG_TRACE("Destroyed entity: %llu", entity.id);

    // Notify systems
    NotifyEntityDestroyed(entity);
}

bool World::IsEntityValid(Entity entity) const {
    if (!entity.IsValid()) {
        return false;
    }

    auto it = entityMetadata_.find(entity.id);
    if (it == entityMetadata_.end()) {
        return false;
    }

    return it->second.alive && it->second.version == entity.version;
}

// Legacy Transform methods
TransformComponent* World::AddTransform(Entity entity) {
    return &AddComponent<TransformComponent>(entity);
}

TransformComponent* World::GetTransform(Entity entity) {
    return GetComponent<TransformComponent>(entity);
}

void World::RemoveTransform(Entity entity) {
    RemoveComponent<TransformComponent>(entity);
}

// System management
void World::RegisterSystem(System* system) {
    if (system) {
        systems_.push_back(system);
        system->world_ = this;
        system->Initialize();
        NEXT_LOG_INFO("Registered system: %s", system->GetName());
    }
}

void World::UnregisterSystem(System* system) {
    auto it = std::find(systems_.begin(), systems_.end(), system);
    if (it != systems_.end()) {
        systems_.erase(it);
        system->Shutdown();
        NEXT_LOG_INFO("Unregistered system: %s", system->GetName());
    }
}

// World update
void World::Update(float deltaTime) {
    NEXT_CPU_SCOPE("World::Update");

    // Update all systems
    for (auto* system : systems_) {
        if (system && system->IsEnabled()) {
            system->Update(deltaTime);
        }
    }
}

std::vector<Entity> World::GetAllEntities() const {
    std::vector<Entity> result(entities_.begin(), entities_.end());
    std::sort(result.begin(), result.end(), [](const Entity& lhs, const Entity& rhs) {
        if (lhs.id != rhs.id) {
            return lhs.id < rhs.id;
        }
        return lhs.version < rhs.version;
    });
    return result;
}

World::WorldStats World::GetStats() const {
    WorldStats stats;
    stats.entityCount = entities_.size();
    stats.systemCount = systems_.size();

    stats.totalComponents = 0;
    for (const auto& [entity, components] : entityComponents_) {
        stats.totalComponents += components.size();
    }

    return stats;
}

// Notifications
void World::NotifyEntityCreated(Entity entity) {
    for (auto* system : systems_) {
        system->OnEntityCreated(entity);
    }
}

void World::NotifyEntityDestroyed(Entity entity) {
    for (auto* system : systems_) {
        system->OnEntityDestroyed(entity);
    }
}

void World::NotifyComponentAdded(Entity entity, ComponentTypeID type) {
    for (auto* system : systems_) {
        system->OnComponentAdded(entity, type);
    }
}

void World::NotifyComponentRemoved(Entity entity, ComponentTypeID type) {
    for (auto* system : systems_) {
        system->OnComponentRemoved(entity, type);
    }
}

} // namespace Next
