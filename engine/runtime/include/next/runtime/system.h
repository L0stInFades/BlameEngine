#pragma once

#include "next/runtime/entity.h"
#include "next/runtime/component_type.h"
#include <string>

namespace Next {

class World;

// Base class for all systems
class System {
    friend class World;
public:
    virtual ~System() = default;

    // System lifecycle
    virtual void Initialize() {}
    virtual void Update(float deltaTime) = 0;
    virtual void Shutdown() {}

    // Entity events
    virtual void OnEntityCreated(Entity /*entity*/) {}
    virtual void OnEntityDestroyed(Entity /*entity*/) {}
    virtual void OnComponentAdded(Entity /*entity*/, ComponentTypeID /*type*/) {}
    virtual void OnComponentRemoved(Entity /*entity*/, ComponentTypeID /*type*/) {}

    // System name for debugging
    virtual const char* GetName() const { return "System"; }

    // Enable/disable system
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

protected:
    bool enabled_ = true;
    World* world_ = nullptr;
};

// Transform System - handles hierarchy and world matrices
class TransformSystem : public System {
public:
    void Update(float deltaTime) override;
    const char* GetName() const override { return "TransformSystem"; }
};

// Render System - handles mesh rendering
class RenderSystem : public System {
public:
    void Update(float deltaTime) override;
    const char* GetName() const override { return "RenderSystem"; }
};

} // namespace Next
