# Physics core (ADR-0009): the IPhysicsWorld abstraction, ECS components, the PhysicsSystem, and a
# built-in deterministic reference backend. Zero third-party dependencies — always builds, always
# headless-testable. The Jolt backend is a SEPARATE, optional target (see the BUILD_WITH_JOLT block
# at the end); this core never references Jolt.

cmake_minimum_required(VERSION 3.20)

add_library(next_physics STATIC
    src/reference_physics_world.cpp
    src/physics_system.cpp
)

target_include_directories(next_physics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/engine/runtime/include
    ${CMAKE_SOURCE_DIR}/engine/foundation/include
)

target_link_libraries(next_physics PUBLIC
    next_runtime
    next_foundation
)

if(WIN32)
    target_compile_definitions(next_physics PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(MSVC)
    target_compile_options(next_physics PRIVATE /W4 /WX-)
else()
    target_compile_options(next_physics PRIVATE
        -Wall
        -Wextra
        -Wformat=2
        -Werror=format
        -Wno-unused-parameter
    )
endif()

if(MSVC)
    target_compile_options(next_physics PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )
else()
    target_compile_options(next_physics PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()

# --- Optional Jolt backend (ADR-0009) ---------------------------------------------------------
# Off by default so the trunk build/CI never needs the Jolt toolchain or network. When enabled,
# FetchContent pulls JoltPhysics and builds next_physics_jolt (JoltPhysicsWorld). The core above
# stays Jolt-free; only code that explicitly wants Jolt links next_physics_jolt.
if(BUILD_WITH_JOLT)
    include(FetchContent)
    message(STATUS "NEXT: BUILD_WITH_JOLT=ON — fetching JoltPhysics")

    # Build only the Jolt library (skip its samples/tests/tools).
    set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
    set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
    set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
    set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        JoltPhysics
        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
        GIT_TAG v5.2.0
        SOURCE_SUBDIR Build
    )
    FetchContent_MakeAvailable(JoltPhysics)

    add_library(next_physics_jolt STATIC
        src/jolt_physics_world.cpp
    )
    target_include_directories(next_physics_jolt PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(next_physics_jolt PUBLIC
        next_physics
        Jolt
    )
    target_compile_definitions(next_physics_jolt PUBLIC NEXT_WITH_JOLT=1)
    # Jolt is third-party: do NOT apply our -Werror/strict warnings to its headers.
    if(MSVC)
        target_compile_options(Jolt PRIVATE /WX- /wd4865)
        target_compile_options(next_physics_jolt PRIVATE /WX- /wd4865)
    else()
        target_compile_options(next_physics_jolt PRIVATE -Wno-error)
    endif()
endif()
