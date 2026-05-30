# Game API — the single capability-domained, versioned, deterministic contract (ADR-0007).
# The one surface shared by player sandbox code, AI agents, and the UE5 view. Sits on the
# headless Archetype ECS; depends on nothing from game/ and nothing from UE5.

cmake_minimum_required(VERSION 3.20)

add_library(next_gameapi STATIC
    src/game_api.cpp
    src/abi_dispatch.cpp
    src/intent_resolver.cpp
)

target_include_directories(next_gameapi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/engine/runtime/include
    ${CMAKE_SOURCE_DIR}/engine/foundation/include
)

target_link_libraries(next_gameapi PUBLIC
    next_runtime
    next_foundation
)

if(WIN32)
    target_compile_definitions(next_gameapi PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(MSVC)
    target_compile_options(next_gameapi PRIVATE /W4 /WX-)
else()
    target_compile_options(next_gameapi PRIVATE
        -Wall
        -Wextra
        -Wformat=2
        -Werror=format
        -Wno-unused-parameter
    )
endif()

if(MSVC)
    target_compile_options(next_gameapi PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )
else()
    target_compile_options(next_gameapi PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()
