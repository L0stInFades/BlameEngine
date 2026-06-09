# World Streaming Module (CP7)
# High-performance World Partition & Streaming System

cmake_minimum_required(VERSION 3.20)

# World Streaming Library
add_library(next_world STATIC
    # Source files
    src/world_partition.cpp
    src/async_io.cpp
    src/streaming_manager.cpp
    src/layered_cell_file.cpp
    src/interest_manager.cpp
    src/lod_system.cpp
    src/eviction_policy.cpp
    src/prediction_system.cpp
    src/debug_visualization.cpp
)

# Include directories
target_include_directories(next_world
    PUBLIC
        ${CMAKE_SOURCE_DIR}/engine/world/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/foundation/include
        ${CMAKE_SOURCE_DIR}/engine/compression/include
        ${CMAKE_SOURCE_DIR}/engine/runtime/include
        ${CMAKE_SOURCE_DIR}/engine/jobsystem/include
        ${CMAKE_SOURCE_DIR}/engine/profiler/include
)

# Link libraries
target_link_libraries(next_world
    PUBLIC
        next_foundation
    PRIVATE
        next_compression
    PUBLIC
        next_runtime
        next_jobsystem
        next_profiler
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(next_world PRIVATE
        _WIN32_WINNT=0x0601  # Windows 7 or later
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )

    # Link Windows libraries
    target_link_libraries(next_world
        PUBLIC
            ws2_32
    )
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(next_world PRIVATE
        /W4
        /WX-
    )
else()
    target_compile_options(next_world PRIVATE
        -Wall
        -Wextra
        -Wformat=2
        -Werror=format
        -Wno-unused-parameter
    )
endif()

# Optimization (Release builds) - 使用生成器表达式以支持多配置生成器
if(MSVC)
    target_compile_options(next_world PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/Ot>
        $<$<CONFIG:Release>:/Ob2>
    )
else()
    target_compile_options(next_world PRIVATE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
    )
endif()
