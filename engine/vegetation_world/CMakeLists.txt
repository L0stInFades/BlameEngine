# Vegetation<->world integration (ADR-0014): the layer where the pure core (next_vegetation) meets the
# streaming format (next_world). Hosts the cook (def -> validate -> scatter -> pack -> layered cell) and
# (later) the runtime store. Keeping it here lets next_vegetation stay dependency-light (foundation only).

cmake_minimum_required(VERSION 3.20)

add_library(next_vegetation_world STATIC
    src/vegetation_cook.cpp
    src/vegetation_store.cpp
    src/vegetation_query.cpp
    src/vegetation_view.cpp
    src/vegetation_streaming_system.cpp
)

target_include_directories(next_vegetation_world PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(next_vegetation_world PUBLIC
    next_vegetation
    next_world
    next_gameapi   # IWorldQuery / RaycastResult (header-only contract) for the vegetation LOS adapter
    next_boundary  # GameEvent — destruction rides the sim->UE5 event channel (ADR-0006)
)

if(WIN32)
    target_compile_definitions(next_vegetation_world PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(MSVC)
    target_compile_options(next_vegetation_world PRIVATE /W4 /WX-)
else()
    target_compile_options(next_vegetation_world PRIVATE
        -Wall
        -Wextra
        -Wformat=2
        -Werror=format
        -Wno-unused-parameter
    )
endif()

if(MSVC)
    target_compile_options(next_vegetation_world PRIVATE
        $<$<CONFIG:Release>:/O2>
    )
else()
    target_compile_options(next_vegetation_world PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()
