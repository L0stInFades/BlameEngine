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

# --- Optional WASM backend (ADR-0011) ---------------------------------------------------------
# The player-LANGUAGE path: run real C++/Rust (compiled to wasm32) on the same ISandbox contract,
# routing host-calls through the same HostGateway as the NBVM RefVm. Off by default so trunk/CI
# never need wasm3 or a network. wasm3's TOP-LEVEL CMake drags in uvwasi/libuv (its WASI CLI app),
# which break on modern CMake — so we pull ONLY the interpreter library via SOURCE_SUBDIR source.
if(BUILD_WITH_WASM)
    include(FetchContent)
    message(STATUS "NEXT: BUILD_WITH_WASM=ON — fetching wasm3 (interpreter library only)")

    FetchContent_Declare(
        wasm3
        GIT_REPOSITORY https://github.com/wasm3/wasm3.git
        GIT_TAG v0.5.0
        SOURCE_SUBDIR source
    )
    FetchContent_MakeAvailable(wasm3)

    add_library(next_sandbox_wasm STATIC
        src/wasm_sandbox.cpp
        src/wasm_meter.cpp
    )
    target_include_directories(next_sandbox_wasm PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(next_sandbox_wasm PUBLIC
        next_sandbox
        m3
    )
    target_compile_definitions(next_sandbox_wasm PUBLIC NEXT_WITH_WASM=1)
    # wasm3 is third-party C: keep our -Werror off its headers (the macro API trips -Wformat etc).
    if(NOT MSVC)
        target_compile_options(next_sandbox_wasm PRIVATE -Wno-error)
    endif()
endif()
