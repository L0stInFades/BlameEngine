# Shared build-quality helpers for Blame Engine.

option(NEXT_WARNINGS_AS_ERRORS "Treat project warning baselines as errors" OFF)
option(NEXT_ENABLE_CLANG_TIDY "Run clang-tidy for C++ targets during compilation" OFF)
set(NEXT_SANITIZE "" CACHE STRING "Comma-separated sanitizer list, for example address,undefined or thread")

function(next_apply_warnings target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "next_apply_warnings: target '${target_name}' does not exist")
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4
            /permissive-
        )
        if(NEXT_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
        if(NEXT_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

if(NEXT_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "NEXT_SANITIZE='${NEXT_SANITIZE}' is supported only with GCC/Clang-style toolchains")
    endif()

    string(REPLACE "," ";" NEXT_SANITIZE_LIST "${NEXT_SANITIZE}")
    string(REPLACE ";" "," NEXT_SANITIZE_FLAGS "${NEXT_SANITIZE_LIST}")

    add_compile_options(
        -fsanitize=${NEXT_SANITIZE_FLAGS}
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all
    )
    add_link_options(
        -fsanitize=${NEXT_SANITIZE_FLAGS}
        -fno-sanitize-recover=all
    )
endif()

if(NEXT_ENABLE_CLANG_TIDY)
    find_program(NEXT_CLANG_TIDY_EXE NAMES clang-tidy)
    if(NOT NEXT_CLANG_TIDY_EXE)
        message(FATAL_ERROR "NEXT_ENABLE_CLANG_TIDY=ON but clang-tidy was not found")
    endif()
    set(CMAKE_CXX_CLANG_TIDY "${NEXT_CLANG_TIDY_EXE}" CACHE STRING "clang-tidy command" FORCE)
endif()
