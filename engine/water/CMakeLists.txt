# Water system (ADR-0015): a deterministic, authoritative water capability — analytic Gerstner-wave
# surface, Archimedes buoyancy + velocity-clamped drag + current advection, submersion queries, and a
# CellLayer::Water blob. PURE CORE: the headless core OWNS the water surface + simulation; UE5 only
# RENDERS the same surface from the same parameters (see ADR-0015), exactly as vegetation owns placement
# and UE5 only draws it. Pure downward deps: next_foundation for next/math (Vec3). No physics/ECS/UE5/
# world coupling here — the buoyancy MATH is physics-free; the WaterForceSystem wiring lives in
# next_water_world. Deterministic, headless-testable under ASan.

cmake_minimum_required(VERSION 3.20)

add_library(next_water STATIC
    src/water_surface.cpp
    src/water_volume.cpp
    src/water_spectrum.cpp
    src/buoyancy.cpp
    src/water_validator.cpp
    src/water_builder.cpp
    src/water_cell.cpp
)

target_include_directories(next_water PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/engine/foundation/include
)

target_link_libraries(next_water PUBLIC
    next_foundation
)

if(WIN32)
    target_compile_definitions(next_water PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(MSVC)
    target_compile_options(next_water PRIVATE /W4 /WX-)
else()
    target_compile_options(next_water PRIVATE
        -Wall
        -Wextra
        -Wformat=2
        -Werror=format
        -Wno-unused-parameter
    )
endif()

if(MSVC)
    target_compile_options(next_water PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )
else()
    target_compile_options(next_water PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()
