# Profiler module - CP1

add_library(next_profiler STATIC
    src/profiler.cpp
    src/stats.cpp
    src/cpu_scope.cpp
)

target_include_directories(next_profiler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(next_profiler
    PRIVATE
        next_foundation
)

if(NOT MSVC)
    target_compile_options(next_profiler PRIVATE
        -Wformat=2
        -Werror=format
    )
endif()
