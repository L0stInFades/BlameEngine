cmake_minimum_required(VERSION 3.20)
project(BlameEngine
    VERSION 0.1.0
    DESCRIPTION "Blame Engine - company core game engine (headless authoritative world + UE5 view)"
    LANGUAGES CXX
)

if(APPLE)
    enable_language(OBJCXX)
endif()

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform detection
if(WIN32)
    set(PLATFORM_WINDOWS ON)
    set(PLATFORM_NAME "Windows")
elseif(APPLE)
    set(PLATFORM_APPLE ON)
    set(PLATFORM_NAME "macOS")
endif()

# Build options
option(BUILD_TOOLS "Build tools" ON)
option(BUILD_TESTS "Build tests" ON)
option(BUILD_TERMINAL "Build Neovim terminal integration" ON)
option(BUILD_GAME_HACKOPS "Build HackOps prototype game target" OFF)
option(BUILD_LEGACY_LOG "Build deprecated next_log compatibility module" OFF)
# Physics: the headless core ships a deterministic reference backend with no third-party deps.
# Jolt (ADR-0009) is an OPTIONAL backend fetched on demand — off by default so the trunk build
# and CI never need the Jolt toolchain or network access.
option(BUILD_WITH_JOLT "Fetch JoltPhysics and build the Jolt physics backend (next_physics_jolt)" OFF)
# Player-LANGUAGE frontend (ADR-0011): an optional WASM sandbox backend (wasm3) that runs real
# C++/Rust compiled to wasm32, plus a demo that compiles classic algorithms and runs them in the
# headless world. OFF by default so the trunk build/CI never need wasm3 or a wasm toolchain.
option(BUILD_WITH_WASM "Fetch wasm3 and build the WASM sandbox backend + C++/Rust guest demo" OFF)

# wasm3 is C; the headless core is CXX-only, so enable the C language on demand for the WASM backend.
if(BUILD_WITH_WASM)
    enable_language(C)
endif()

# Rendering and physics are EXTERNAL (ADR-0005): rendering = UE5, physics = Jolt.
# This repo is the headless authoritative-world core; it does NOT build a renderer or RHI.

# Code-quality toggles (sanitizers, strict warnings, clang-tidy). Must be included
# before add_subdirectory so global sanitizer flags reach every target.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/NextQuality.cmake)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Windows-specific settings
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # 添加 UTF-8 编码支持,解决中文注释编译问题
    add_compile_options(/utf-8)
endif()

# Engine modules
add_subdirectory(engine/foundation)
if(BUILD_LEGACY_LOG)
    add_subdirectory(engine/log)
endif()
add_subdirectory(engine/serialization)
add_subdirectory(engine/platform)
add_subdirectory(engine/compression)
add_subdirectory(engine/runtime)
add_subdirectory(engine/profiler)
add_subdirectory(engine/jobsystem)
if(BUILD_TERMINAL)
    add_subdirectory(engine/terminal)
endif()

# Optional modules
add_subdirectory(engine/world)     # CP7: World Streaming
add_subdirectory(engine/script)    # CP8: Script System
add_subdirectory(engine/task)      # CP9: Task System

# Moat modules (ADR-0005): the single Game API contract, the player-code sandbox, and the
# headless→UE5 boundary. Self-contained, headless, no UE5/game dependencies.
add_subdirectory(engine/gameapi)   # ADR-0007: capability-domained, versioned, deterministic contract
add_subdirectory(engine/sandbox)   # ADR-0008: security-first, backend-agnostic player-code runtime
add_subdirectory(engine/boundary)  # ADR-0006: sim↔UE5 snapshot stream + command/event queues
add_subdirectory(engine/physics)   # ADR-0009: IPhysicsWorld + deterministic reference backend (+ optional Jolt)
add_subdirectory(engine/gameplay)  # ADR-0010: gameplay layer — ActuationSystem (single Transform writer)
add_subdirectory(engine/level)     # data-driven level authoring + validation + deterministic loading
add_subdirectory(engine/vegetation)  # ADR-0014: data-driven species + deterministic per-cell scatter (core owns placement; UE5 renders)
add_subdirectory(engine/vegetation_world)  # ADR-0014: vegetation<->world integration (cook + runtime store)
# add_subdirectory(engine/animation)
# add_subdirectory(engine/ai)
# add_subdirectory(engine/gameplay)

# Player-language frontend demo (ADR-0011): compile C++/Rust guests to wasm32 and run them in a
# headless world via the WASM sandbox backend. Guests must be built before the harness.
if(BUILD_WITH_WASM)
    add_subdirectory(examples/wasm_guests)
    add_subdirectory(tools/wasm_demo)
endif()

# Tools
if(BUILD_TOOLS)
    add_subdirectory(tools/assetc)
    add_subdirectory(tools/compression_probe)
    add_subdirectory(tools/sandbox_audit)  # ADR-0008: headless sandbox security-audit harness
    if(BUILD_TERMINAL)
        add_subdirectory(tools/nvim_surface_cpp_probe)
    endif()
endif()

# Game
if(BUILD_GAME_HACKOPS)
    if(BUILD_TERMINAL)
        add_subdirectory(game/hackops)
    else()
        message(FATAL_ERROR "BUILD_GAME_HACKOPS=ON requires BUILD_TERMINAL=ON")
    endif()
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()
    include(FetchContent)

    # Fetch Google Test
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    # For Windows: Prevent overriding parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests)
endif()
