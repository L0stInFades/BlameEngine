# Runtime core library
add_library(next_runtime STATIC
    src/entity.cpp
    src/component_registry.cpp
    src/world.cpp
    src/event_bus.cpp
    src/system.cpp
)

target_include_directories(next_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(next_runtime PUBLIC next_foundation next_asset)

if(NOT MSVC)
    target_compile_options(next_runtime PRIVATE
        -Wformat=2
        -Werror=format
    )
endif()

# Project warning baseline (-Wall -Wextra; -Werror when NEXT_WARNINGS_AS_ERRORS=ON).
next_apply_warnings(next_runtime)

# Asset runtime module (CP3)
add_subdirectory(asset)
