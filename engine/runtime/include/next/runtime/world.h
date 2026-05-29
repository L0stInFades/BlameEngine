#pragma once

#include "next/runtime/entity.h"
#include "next/runtime/component_type.h"
#include "next/runtime/archetype.h"
#include "next/runtime/transform.h"
#include "next/runtime/component.h"
#include "next/runtime/system.h"

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Next {

// Data-oriented ECS world. Entities are generational ids; components live in archetype
// tables (one contiguous column per component type), so a query iterates the columns of
// matching archetypes linearly and add/remove migrate the entity between archetypes.
//
// The public component/query API matches the previous map-backed implementation, so
// existing systems, game code, and tests are unaffected. As with any archetype ECS, a
// reference returned by AddComponent/GetComponent is invalidated by the next structural
// change (add/remove/destroy) to that entity's archetype — re-fetch via GetComponent.
class World {
public:
    World();
    ~World();

    // Entity management
    Entity CreateEntity();
    void DestroyEntity(Entity entity);
    bool IsEntityValid(Entity entity) const;

    // Component management (generic)
    template<typename T>
    T& AddComponent(Entity entity) {
        const ComponentTypeID type = ComponentType<T>::GetID();
        EnsureEntityTracked(entity);
        const EntityLocation loc = entityLocation_.at(entity);
        if (loc.archetype->Has(type)) {
            T* existing = static_cast<T*>(loc.archetype->ColumnFor(type)->At(loc.row));
            *existing = T();
            NotifyComponentAdded(entity, type);
            return *existing;
        }
        AddComponentType(entity, type);
        const EntityLocation newLoc = entityLocation_.at(entity);
        T* added = static_cast<T*>(newLoc.archetype->ColumnFor(type)->At(newLoc.row));
        NotifyComponentAdded(entity, type);
        return *added;
    }

    template<typename T>
    T& AddComponent(Entity entity, const T& component) {
        const ComponentTypeID type = ComponentType<T>::GetID();
        EnsureEntityTracked(entity);
        const EntityLocation loc = entityLocation_.at(entity);
        if (loc.archetype->Has(type)) {
            T* existing = static_cast<T*>(loc.archetype->ColumnFor(type)->At(loc.row));
            *existing = component;
            NotifyComponentAdded(entity, type);
            return *existing;
        }
        AddComponentType(entity, type);
        const EntityLocation newLoc = entityLocation_.at(entity);
        T* added = static_cast<T*>(newLoc.archetype->ColumnFor(type)->At(newLoc.row));
        *added = component;
        NotifyComponentAdded(entity, type);
        return *added;
    }

    template<typename T>
    T* GetComponent(Entity entity) {
        auto it = entityLocation_.find(entity);
        if (it == entityLocation_.end()) {
            return nullptr;
        }
        ComponentColumn* column = it->second.archetype->ColumnFor(ComponentType<T>::GetID());
        return column != nullptr ? static_cast<T*>(column->At(it->second.row)) : nullptr;
    }

    template<typename T>
    const T* GetComponent(Entity entity) const {
        auto it = entityLocation_.find(entity);
        if (it == entityLocation_.end()) {
            return nullptr;
        }
        const ComponentColumn* column = it->second.archetype->ColumnFor(ComponentType<T>::GetID());
        return column != nullptr ? static_cast<const T*>(column->At(it->second.row)) : nullptr;
    }

    template<typename T>
    bool HasComponent(Entity entity) const {
        auto it = entityLocation_.find(entity);
        return it != entityLocation_.end() && it->second.archetype->Has(ComponentType<T>::GetID());
    }

    template<typename T>
    void RemoveComponent(Entity entity) {
        const ComponentTypeID type = ComponentType<T>::GetID();
        auto it = entityLocation_.find(entity);
        if (it == entityLocation_.end() || !it->second.archetype->Has(type)) {
            return;
        }
        RemoveComponentType(entity, type);
        NotifyComponentRemoved(entity, type);
    }

    // Query entities that have all of the given components (order-independent).
    template<typename... Components>
    std::vector<Entity> QueryEntitiesWith() {
        const std::vector<ComponentTypeID> query = MakeSortedSignature<Components...>();
        std::vector<Entity> result;
        for (const auto& archetype : archetypes_) {
            if (SignatureContainsAll(archetype->Signature(), query)) {
                const size_t count = archetype->Size();
                for (size_t row = 0; row < count; ++row) {
                    result.push_back(archetype->EntityAt(row));
                }
            }
        }
        return result;
    }

    // Data-oriented iteration: invokes fn(Entity, Components&...) over the contiguous columns
    // of every archetype that has all of Components. This is the fast path behind Query.
    template<typename... Components, typename Fn>
    void Each(Fn&& fn) {
        const std::vector<ComponentTypeID> query = MakeSortedSignature<Components...>();
        for (const auto& archetypePtr : archetypes_) {
            Archetype* archetype = archetypePtr.get();
            if (!SignatureContainsAll(archetype->Signature(), query)) {
                continue;
            }
            std::tuple<Components*...> columns{archetype->template ColumnData<Components>()...};
            const size_t count = archetype->Size();
            for (size_t row = 0; row < count; ++row) {
                fn(archetype->EntityAt(row), std::get<Components*>(columns)[row]...);
            }
        }
    }

    // Legacy Transform helpers (kept for backward compatibility)
    TransformComponent* AddTransform(Entity entity);
    TransformComponent* GetTransform(Entity entity);
    void RemoveTransform(Entity entity);

    // System management
    void RegisterSystem(System* system);
    void UnregisterSystem(System* system);

    // World update
    void Update(float deltaTime);

    // Statistics
    size_t GetEntityCount() const { return entities_.size(); }
    std::vector<Entity> GetAllEntities() const;

    struct WorldStats {
        size_t entityCount;
        size_t totalComponents;
        size_t systemCount;
    };
    WorldStats GetStats() const;

private:
    void NotifyEntityCreated(Entity entity);
    void NotifyEntityDestroyed(Entity entity);
    void NotifyComponentAdded(Entity entity, ComponentTypeID type);
    void NotifyComponentRemoved(Entity entity, ComponentTypeID type);

    // Archetype machinery (type-erased; defined in world.cpp).
    struct EntityLocation {
        Archetype* archetype = nullptr;
        size_t row = 0;
    };

    Archetype* GetOrCreateArchetype(const std::vector<ComponentTypeID>& signature);
    void EnsureEntityTracked(Entity entity);
    void MigrateEntity(Entity entity, Archetype* from, Archetype* to);
    void AddComponentType(Entity entity, ComponentTypeID type);
    void RemoveComponentType(Entity entity, ComponentTypeID type);

    // Entity bookkeeping
    struct EntityMetadata {
        uint16_t version;
        bool alive;
    };

    std::unordered_map<uint64_t, EntityMetadata> entityMetadata_;
    std::unordered_set<Entity, EntityHash> entities_;
    uint64_t nextEntityID_;
    std::deque<uint64_t> freeIDs_;

    // Component storage: archetypes + entity -> (archetype, row).
    std::vector<std::unique_ptr<Archetype>> archetypes_;
    std::map<std::vector<ComponentTypeID>, size_t> archetypeIndex_;  // signature -> index
    std::unordered_map<Entity, EntityLocation, EntityHash> entityLocation_;

    // Systems
    std::vector<System*> systems_;
};

}  // namespace Next
