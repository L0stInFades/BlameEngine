# Asset Compiler for CP3

add_executable(next_assetc
    src/main.cpp
    src/asset_compiler.cpp
    src/obj_loader.cpp
    src/tga_loader.cpp
)

target_include_directories(next_assetc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    PUBLIC
        ${CMAKE_SOURCE_DIR}/engine/runtime/asset/include
        ${CMAKE_SOURCE_DIR}/engine/world/include
)

target_link_libraries(next_assetc PRIVATE
    next_asset
    next_compression
    next_vegetation_world
)

if(NOT MSVC)
    target_compile_options(next_assetc PRIVATE
        -Wformat=2
        -Werror=format
    )
endif()
