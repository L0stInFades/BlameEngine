# Player-code sandbox (ADR-0008): a security boundary, not a particular VM. The reference
# bytecode backend + the Game API gateway. Depends on next_gameapi (the host surface a guest may
# reach). No dependency on game/ and none on UE5; runs fully headless.

cmake_minimum_required(VERSION 3.20)

add_library(next_sandbox STATIC
    src/bytecode.cpp
    src/ref_vm.cpp
    src/gameapi_gateway.cpp
)

target_include_directories(next_sandbox PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(next_sandbox PUBLIC
    next_gameapi
)

if(WIN32)
    target_compile_definitions(next_sandbox PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(MSVC)
    target_compile_options(next_sandbox PRIVATE /W4 /WX-)
else()
    target_compile_options(next_sandbox PRIVATE
        -Wall
        -Wextra
        -Wformat=2
        -Werror=format
        -Wno-unused-parameter
    )
endif()

if(MSVC)
    target_compile_options(next_sandbox PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )
else()
    target_compile_options(next_sandbox PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()
