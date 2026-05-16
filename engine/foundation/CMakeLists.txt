add_library(next_foundation STATIC
    src/logger.cpp
    src/assert.cpp
)

target_include_directories(next_foundation
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(NOT MSVC)
    target_compile_options(next_foundation PRIVATE
        -Wformat=2
        -Werror=format
    )
endif()
