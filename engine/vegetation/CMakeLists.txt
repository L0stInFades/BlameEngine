# Vegetation system (ADR-0014): data-driven species + a deterministic per-cell scatter + a fail-closed
# validator + CellLayer::Vegetation blob packing. The headless core OWNS vegetation placement and
# per-instance logical state; UE5 only renders the resulting instances (using UE5's own foliage /
# Nanite / HISM is presentation, not authority — see ADR-0014). Pure downward deps: next_foundation
# for next/math (Vec3). No UE5, no game/, no world/runtime coupling. Deterministic, headless-testable
# under ASan.

cmake_minimum_required(VERSION 3.20)

add_library(next_vegetation STATIC
    src/vegetation_builder.cpp
    src/vegetation_validator.cpp
    src/vegetation_scatter.cpp
    src/vegetation_cell.cpp
)

target_include_directories(next_vegetation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/engine/foundation/include
)

target_link_libraries(next_vegetation PUBLIC
    next_foundation
)

if(WIN32)
    target_compile_definitions(next_vegetation PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(MSVC)
    target_compile_options(next_vegetation PRIVATE /W4 /WX-)
else()
    target_compile_options(next_vegetation PRIVATE
        -Wall
        -Wextra
        -Wformat=2
        -Werror=format
        -Wno-unused-parameter
    )
endif()

if(MSVC)
    target_compile_options(next_vegetation PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )
else()
    target_compile_options(next_vegetation PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()
