#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace Next {

using ComponentTypeID = uint32_t;

// Type-erased operations for one component type, used by archetype tables to relocate
// and destroy components generically. Components are NOT required to be trivially
// copyable (some derive from IComponent and are polymorphic), so storage must never
// memcpy them — it always default-constructs, move-constructs, and destroys via these ops.
struct ComponentOps {
    size_t size = 0;
    size_t align = 0;
    void (*defaultConstruct)(void* dst) = nullptr;
    void (*moveConstruct)(void* dst, void* src) = nullptr;  // construct *dst from std::move(*src)
    void (*destroy)(void* p) = nullptr;
};

template<typename T>
inline ComponentOps MakeComponentOps() {
    ComponentOps ops;
    ops.size = sizeof(T);
    ops.align = alignof(T);
    ops.defaultConstruct = [](void* dst) { ::new (dst) T(); };
    ops.moveConstruct = [](void* dst, void* src) { ::new (dst) T(std::move(*static_cast<T*>(src))); };
    ops.destroy = [](void* p) { static_cast<T*>(p)->~T(); };
    return ops;
}

// Process-global registry mapping ComponentTypeID -> ops. Populated lazily the first time
// ComponentType<T>::GetID() is evaluated for a given T. Thread-safe registration so first
// use of distinct component types from different threads is well defined.
class ComponentRegistry {
public:
    static ComponentRegistry& Get() {
        static ComponentRegistry instance;
        return instance;
    }

    void Register(ComponentTypeID id, const ComponentOps& ops) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (id >= ops_.size()) {
            ops_.resize(static_cast<size_t>(id) + 1);
        }
        ops_[id] = ops;
    }

    const ComponentOps& GetOps(ComponentTypeID id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ops_[id];
    }

private:
    ComponentRegistry() = default;
    mutable std::mutex mutex_;
    std::vector<ComponentOps> ops_;
};

struct ComponentTypeBase {
    static std::atomic<ComponentTypeID> nextTypeID;
};

// Assigns a dense, run-stable id per component type and registers its type-erased ops on
// first use. The id doubles as the column key inside archetype tables.
template<typename T>
struct ComponentType : public ComponentTypeBase {
    static ComponentTypeID GetID() {
        static const ComponentTypeID id = []() {
            const ComponentTypeID newId = nextTypeID.fetch_add(1, std::memory_order_relaxed);
            ComponentRegistry::Get().Register(newId, MakeComponentOps<T>());
            return newId;
        }();
        return id;
    }
};

}  // namespace Next
