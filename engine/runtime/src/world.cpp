#include "next/runtime/world.h"
#include "next/foundation/logger.h"
#include "next/profiler/cpu_scope.h"
#include <algorithm>

namespace Next {

World::World() : nextEntityID_(1) {
    NEXT_LOG_INFO("World initialized (archetype ECS)");
}

World::~World() {
    // World does NOT own system pointers; the owner manages their lifetime. We deliberately
    // do not call Shutdown() here (a stack-allocated system may already be gone).
    std::vector<System*> systemsCopy;
    systems_.swap(systemsCopy);

    // Destroy component storage first: clearing archetypes runs every component's destructor
    // through the registered ops.
    entityLocation_.clear();
    archetypeIndex_.clear();
    archetypes_.clear();
    entities_.clear();
    entityMetadata_.clear();

    NEXT_LOG_INFO("World destroyed");
}

// --- Archetype machinery ---------------------------------------------------

Archetype* World::GetOrCreateArchetype(const std::vector<ComponentTypeID>& signature) {
    auto it = archetypeIndex_.find(signature);
    if (it != archetypeIndex_.end()) {
        return archetypes_[it->second].get();
    }
    archetypes_.push_back(std::make_unique<Archetype>(signature));
    const size_t index = archetypes_.size() - 1;
    archetypeIndex_[signature] = index;
    return archetypes_[index].get();
}

void World::EnsureEntityTracked(Entity entity) {
    if (entityLocation_.find(entity) != entityLocation_.end()) {
        return;
    }
    Archetype* empty = GetOrCreateArchetype({});
    const size_t row = empty->AddEntityDefault(entity);
    entityLocation_[entity] = EntityLocation{empty, row};
}

void World::MigrateEntity(Entity entity, Archetype* from, Archetype* to) {
    const size_t fromRow = entityLocation_.at(entity).row;
    const size_t newRow = to->AppendMigratedRow(entity, *from, fromRow);

    // Remove the (now moved-from) source row; a relocated entity needs its row fixed up.
    const Entity relocated = from->RemoveRow(fromRow);
    if (relocated.IsValid()) {
        entityLocation_[relocated].row = fromRow;
    }
    entityLocation_[entity] = EntityLocation{to, newRow};
}

void World::AddComponentType(Entity entity, ComponentTypeID type) {
    const EntityLocation loc = entityLocation_.at(entity);
    const std::vector<ComponentTypeID> newSignature = SignatureWith(loc.archetype->Signature(), type);
    Archetype* target = GetOrCreateArchetype(newSignature);
    MigrateEntity(entity, loc.archetype, target);
}

void World::RemoveComponentType(Entity entity, ComponentTypeID type) {
    const EntityLocation loc = entityLocation_.at(entity);
    const std::vector<ComponentTypeID> newSignature = SignatureWithout(loc.archetype->Signature(), type);
    Archetype* target = GetOrCreateArchetype(newSignature);
    MigrateEntity(entity, loc.archetype, target);
}

// --- Entity lifecycle ------------------------------------------------------

Entity World::CreateEntity() {
    uint64_t id;
    uint16_t version = 1;

    if (!freeIDs_.empty()) {
        id = freeIDs_.front();
        freeIDs_.pop_front();
        version = static_cast<uint16_t>(entityMetadata_[id].version + 1);
        if (version == 0) {
            version = 1;  // 0 is the reserved "invalid" sentinel (Entity::IsValid); skip it on wrap
        }
    } else {
        id = nextEntityID_++;
    }

    Entity entity(id, version);

    entities_.insert(entity);
    entityMetadata_[id] = {version, true};
    EnsureEntityTracked(entity);  // place in the empty archetype until a component is added

    NEXT_LOG_TRACE("Created entity: %llu (version: %u)", static_cast<unsigned long long>(id), version);

    NotifyEntityCreated(entity);
    return entity;
}

void World::DestroyEntity(Entity entity) {
    if (!IsEntityValid(entity)) {
        return;
    }

    auto it = entityLocation_.find(entity);
    if (it != entityLocation_.end()) {
        Archetype* archetype = it->second.archetype;
        const size_t row = it->second.row;
        const Entity relocated = archetype->RemoveRow(row);
        if (relocated.IsValid()) {
            entityLocation_[relocated].row = row;
        }
        entityLocation_.erase(entity);
    }

    entities_.erase(entity);
    entityMetadata_[entity.id].alive = false;
    freeIDs_.push_back(entity.id);

    NEXT_LOG_TRACE("Destroyed entity: %llu", static_cast<unsigned long long>(entity.id));

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

// --- Legacy Transform helpers ----------------------------------------------

TransformComponent* World::AddTransform(Entity entity) {
    return &AddComponent<TransformComponent>(entity);
}

TransformComponent* World::GetTransform(Entity entity) {
    return GetComponent<TransformComponent>(entity);
}

void World::RemoveTransform(Entity entity) {
    RemoveComponent<TransformComponent>(entity);
}

// --- System management -----------------------------------------------------

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

void World::Update(float deltaTime) {
    NEXT_CPU_SCOPE("World::Update");
    for (auto* system : systems_) {
        if (system && system->IsEnabled()) {
            system->Update(deltaTime);
        }
    }
}

// --- Statistics ------------------------------------------------------------

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
    for (const auto& archetype : archetypes_) {
        stats.totalComponents += archetype->Size() * archetype->Signature().size();
    }
    return stats;
}

// --- Notifications ---------------------------------------------------------

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

}  // namespace Next
