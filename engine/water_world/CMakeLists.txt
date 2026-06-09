# Water <-> world integration (ADR-0015): where the pure core (next_water) meets the streaming format,
# the physics world, and the sim->UE5 boundary. Hosts the cook (scene -> validate -> select-per-cell ->
# pack -> layered Water cell), the runtime WaterStore (broadphase + surface/submersion queries), the
# WaterWorldQuery (folds the surface into the Sense raycast), the WaterForceSystem (buoyancy/drag/current
# via IPhysicsWorld::AddForce/AddImpulse), the streaming Sync, and the Mock UE5 consumer + splash events.
# Keeping it here lets next_water stay dependency-light (foundation only).

cmake_minimum_required(VERSION 3.20)

add_library(next_water_world STATIC
    src/water_cook.cpp
    src/water_store.cpp
    src/water_query.cpp
    src/water_view.cpp
    src/water_streaming_system.cpp
    src/water_force_system.cpp
    src/water_hazard.cpp
    src/water_swim.cpp
)

target_include_directories(next_water_world PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(next_water_world PUBLIC
    next_water       # pure core: surface, volume, buoyancy, validator, cell
    next_world       # CellLayer / layered cell file / StreamingManager
    next_gameapi     # IWorldQuery, RaycastResult
    next_boundary    # GameEvent / transport (splash cues)
    next_runtime     # World / System / Each (the force system)
    next_physics     # IPhysicsWorld AddForce/AddImpulse + RigidBodyComponent
)

if(WIN32)
    target_compile_definitions(next_water_world PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(MSVC)
    target_compile_options(next_water_world PRIVATE /W4 /WX-)
else()
    target_compile_options(next_water_world PRIVATE
        -Wall
        -Wextra
        -Wformat=2
        -Werror=format
        -Wno-unused-parameter
    )
endif()

if(MSVC)
    target_compile_options(next_water_world PRIVATE
        $<$<CONFIG:Release>:/O2>
    )
else()
    target_compile_options(next_water_world PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()
