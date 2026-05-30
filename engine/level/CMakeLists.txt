# Level-design system (data-driven level authoring + validation + loading into the headless world).
# Pure downward deps: it authors over the ECS World + gameapi/physics/boundary component structs;
# no UE5, no game/, no legacy Task System. Deterministic, headless-testable under ASan.

cmake_minimum_required(VERSION 3.20)

add_library(next_level STATIC
    src/level_builder.cpp
    src/level_validator.cpp
    src/level_loader.cpp
    src/win_conditions.cpp
)

target_include_directories(next_level PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# next_gameapi pulls next_runtime + next_foundation (World symbols + math + components); next_physics
# and next_boundary contribute their header-only component structs (RigidBodyComponent / Renderable).
target_link_libraries(next_level PUBLIC
    next_gameapi
    next_physics
    next_boundary
)

if(WIN32)
    target_compile_definitions(next_level PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(MSVC)
    target_compile_options(next_level PRIVATE /W4 /WX-)
else()
    target_compile_options(next_level PRIVATE
        -Wall
        -Wextra
        -Wformat=2
        -Werror=format
        -Wno-unused-parameter
    )
endif()

if(MSVC)
    target_compile_options(next_level PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )
else()
    target_compile_options(next_level PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()
