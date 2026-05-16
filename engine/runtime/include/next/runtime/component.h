#pragma once

#include "next/runtime/entity.h"
#include "next/runtime/transform.h"
#include "next/runtime/asset/asset_handle.h"
#include <cstdint>
#include <cstring>

namespace Next {

// Base component structure (all components should be POD-like for performance)
struct IComponent {
    virtual ~IComponent() = default;
};

// Name component - for debugging and editor
struct NameComponent : public IComponent {
    static constexpr size_t MAX_NAME_LENGTH = 64;
    char name[MAX_NAME_LENGTH];

    NameComponent() {
        memset(name, 0, MAX_NAME_LENGTH);
    }

    NameComponent(const char* n) : NameComponent() {
        if (n) {
            strncpy(name, n, MAX_NAME_LENGTH - 1);
            name[MAX_NAME_LENGTH - 1] = '\0';
        }
    }
};

// Hierarchy component - for scene graph
struct HierarchyComponent : public IComponent {
    Entity parent;
    uint32_t childCount;
    static constexpr uint32_t MAX_CHILDREN = 128;
    Entity children[MAX_CHILDREN];

    HierarchyComponent() : parent(Entity::Invalid()), childCount(0) {
        memset(children, 0, sizeof(children));
    }

    void NormalizeChildCount() {
        if (childCount > MAX_CHILDREN) {
            childCount = MAX_CHILDREN;
        }
    }

    void AddChild(Entity child) {
        NormalizeChildCount();
        if (childCount < MAX_CHILDREN) {
            children[childCount++] = child;
        }
    }

    void RemoveChild(Entity child) {
        NormalizeChildCount();
        for (uint32_t i = 0; i < childCount; ++i) {
            if (children[i] == child) {
                // Shift remaining children
                for (uint32_t j = i; j < childCount - 1; ++j) {
                    children[j] = children[j + 1];
                }
                children[childCount - 1] = Entity::Invalid();
                childCount--;
                return;
            }
        }
    }
};

// Mesh Renderer component - integrates with Asset Pipeline
struct MeshRendererComponent : public IComponent {
    MeshHandle mesh;
    MaterialHandle material;
    uint32_t submeshIndex;
    bool castShadows;
    bool receiveShadows;

    MeshRendererComponent()
        : submeshIndex(0), castShadows(true), receiveShadows(true) {}
};

// Tag components for filtering
struct StaticTag {};
struct DynamicTag {};
struct VisibleTag {};
struct InvisibleTag {};

} // namespace Next
