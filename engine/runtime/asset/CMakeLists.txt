# Asset Runtime module for CP3

add_library(next_asset STATIC
    src/asset_manager.cpp
    src/package_container.cpp
    src/asset_types.cpp
)

target_include_directories(next_asset
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(next_asset PUBLIC next_foundation next_jobsystem)
# Compression is an internal implementation detail: package_container decompresses
# LZ4/Zstd entries on read. Consumers of next_asset don't see the compression API.
target_link_libraries(next_asset PRIVATE next_compression)

if(NOT MSVC)
    target_compile_options(next_asset PRIVATE
        -Wformat=2
        -Werror=format
    )
endif()
