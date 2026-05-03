cmake_minimum_required(VERSION 3.20)
project(NEXTEngine
    VERSION 0.1.0
    DESCRIPTION "NEXT Self-Hosted Engine - Song Dynasty Project"
    LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform detection
if(WIN32)
    set(PLATFORM_WINDOWS ON)
    set(PLATFORM_NAME "Windows")
endif()

# Build options
option(BUILD_TOOLS "Build tools" ON)
option(BUILD_GAME "Build game" ON)
option(BUILD_TESTS "Build tests" ON)

set(NEXT_DEFAULT_BUILD_RENDERER ON)
if(NOT WIN32)
    set(NEXT_DEFAULT_BUILD_RENDERER OFF)
endif()
option(BUILD_RENDERER "Build the current DX12 renderer path" ${NEXT_DEFAULT_BUILD_RENDERER})
option(BUILD_EDITOR "Build the editor tool" ON)
option(BUILD_TERMINAL "Build Neovim terminal/editor integration" ON)

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
add_subdirectory(engine/log)                # 统一日志系统
add_subdirectory(engine/serialization)      # 序列化系统
add_subdirectory(engine/platform)
add_subdirectory(engine/foundation)
add_subdirectory(engine/runtime)
if(BUILD_RENDERER)
    add_subdirectory(engine/renderer)
else()
    message(STATUS "BUILD_RENDERER=OFF: skipping DX12 renderer")
endif()
add_subdirectory(engine/profiler)
add_subdirectory(engine/jobsystem)
if(BUILD_RENDERER)
    add_subdirectory(engine/game)
else()
    message(STATUS "BUILD_RENDERER=OFF: skipping engine/game camera-feel module")
endif()
if(BUILD_TERMINAL)
    add_subdirectory(engine/terminal)
endif()

# Optional modules
add_subdirectory(engine/world)     # CP7: World Streaming
add_subdirectory(engine/script)    # CP8: Script System
add_subdirectory(engine/task)      # CP9: Task System
# add_subdirectory(engine/physics)
# add_subdirectory(engine/animation)
# add_subdirectory(engine/ai)
# add_subdirectory(engine/gameplay)

# Tools
if(BUILD_TOOLS)
    add_subdirectory(tools/assetc)
    if(BUILD_EDITOR AND BUILD_RENDERER AND WIN32)
        add_subdirectory(third_party/imgui)
        add_subdirectory(tools/editor)
    elseif(BUILD_EDITOR)
        message(STATUS "Skipping next_editor: requires BUILD_RENDERER=ON and WIN32")
    endif()
    if(BUILD_TERMINAL)
        add_subdirectory(tools/nvim_surface_cpp_probe)
    endif()
endif()

# Game
if(BUILD_GAME)
    if(BUILD_RENDERER)
        add_subdirectory(game/song)
    else()
        message(STATUS "Skipping song_demo: requires BUILD_RENDERER=ON")
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
