# Gameplay framework layer (ADR-0010): the layer that wires the otherwise-decoupled cores together.
# Today it hosts ActuationSystem — the single authoritative mover that turns Game API movement
# intents into physics velocity (or direct transform integration), so TransformComponent has one
# writer. Depends on gameapi + physics + runtime; those cores stay independent of each other.

cmake_minimum_required(VERSION 3.20)

add_library(next_gameplay STATIC
    src/actuation_system.cpp
    src/physics_world_query.cpp
)

target_include_directories(next_gameplay PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(next_gameplay PUBLIC
    next_gameapi
    next_physics
    next_runtime
)

if(WIN32)
    target_compile_definitions(next_gameplay PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(MSVC)
    target_compile_options(next_gameplay PRIVATE /W4 /WX-)
else()
    target_compile_options(next_gameplay PRIVATE
        -Wall
        -Wextra
        -Wformat=2
        -Werror=format
        -Wno-unused-parameter
    )
endif()

if(MSVC)
    target_compile_options(next_gameplay PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )
else()
    target_compile_options(next_gameplay PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()
