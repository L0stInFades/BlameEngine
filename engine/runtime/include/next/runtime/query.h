#pragma once

#include "next/runtime/world.h"
#include <type_traits>
#include <utility>

namespace Next {

template<typename... Components>
class QueryView {
public:
    explicit QueryView(World& world) : world_(world) {}

    // Iterate every entity that has all of Components. Backed by World::Each, which walks the
    // contiguous component columns of matching archetypes (no per-entity component lookup).
    // The callback may accept (Entity, Components&...), (Components&...), or (Entity).
    template<typename Func>
    void ForEach(Func&& func) {
        world_.template Each<Components...>([&func](Entity entity, Components&... components) {
            if constexpr (std::is_invocable_v<Func&, Entity, Components&...>) {
                func(entity, components...);
            } else if constexpr (std::is_invocable_v<Func&, Components&...>) {
                func(components...);
            } else if constexpr (std::is_invocable_v<Func&, Entity>) {
                func(entity);
            } else {
                static_assert(AlwaysFalse<Func>::value,
                              "Query::ForEach callback must accept Entity, Components..., or Entity + Components...");
            }
        });
    }

private:
    template<typename T>
    struct AlwaysFalse : std::false_type {};

    World& world_;
};

class Query {
public:
    explicit Query(World& world) : world_(world) {}

    template<typename... Components>
    QueryView<Components...> All() {
        return QueryView<Components...>(world_);
    }

private:
    World& world_;
};

}  // namespace Next
