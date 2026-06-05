# sim↔UE5 boundary (ADR-0006): flat POD snapshot stream + one-way event/command channels. The
# headless authoritative world's only outward render-view surface. Depends on next_runtime (to
# read ECS transforms); has no dependency on UE5 — UE5 consumes these bytes, never the reverse.

cmake_minimum_required(VERSION 3.20)

add_library(next_boundary STATIC
    src/snapshot_publisher.cpp
    src/snapshot_receiver.cpp
    src/snapshot_codec.cpp
    src/datagram_transport.cpp
    src/udp_datagram.cpp
)

target_include_directories(next_boundary PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/engine/runtime/include
    ${CMAKE_SOURCE_DIR}/engine/foundation/include
)

target_link_libraries(next_boundary PUBLIC
    next_runtime
    next_foundation
)

if(WIN32)
    target_compile_definitions(next_boundary PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(MSVC)
    target_compile_options(next_boundary PRIVATE /W4 /WX-)
else()
    target_compile_options(next_boundary PRIVATE
        -Wall
        -Wextra
        -Wformat=2
        -Werror=format
        -Wno-unused-parameter
    )
endif()

if(MSVC)
    target_compile_options(next_boundary PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )
else()
    target_compile_options(next_boundary PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()
