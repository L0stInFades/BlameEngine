cmake_minimum_required(VERSION 3.20)

add_library(next_compression STATIC
    src/compression.cpp
)

target_include_directories(next_compression
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(next_compression PUBLIC cxx_std_17)

if(NOT MSVC)
    target_compile_options(next_compression PRIVATE
        -Wformat=2
        -Werror=format
    )
endif()

find_path(NEXT_LZ4_INCLUDE_DIR lz4.h
    PATHS
        /opt/homebrew/include
        /usr/local/include
)
find_library(NEXT_LZ4_LIBRARY lz4
    PATHS
        /opt/homebrew/lib
        /usr/local/lib
)

find_path(NEXT_ZSTD_INCLUDE_DIR zstd.h
    PATHS
        /opt/homebrew/include
        /usr/local/include
)
find_library(NEXT_ZSTD_LIBRARY zstd
    PATHS
        /opt/homebrew/lib
        /usr/local/lib
)

if(NEXT_LZ4_INCLUDE_DIR AND NEXT_LZ4_LIBRARY)
    target_compile_definitions(next_compression PRIVATE NEXT_HAS_LZ4=1)
    target_include_directories(next_compression PRIVATE ${NEXT_LZ4_INCLUDE_DIR})
    target_link_libraries(next_compression PUBLIC ${NEXT_LZ4_LIBRARY})
    message(STATUS "next_compression: LZ4 enabled")
else()
    message(STATUS "next_compression: LZ4 not found, backend disabled")
endif()

if(NEXT_ZSTD_INCLUDE_DIR AND NEXT_ZSTD_LIBRARY)
    target_compile_definitions(next_compression PRIVATE NEXT_HAS_ZSTD=1)
    target_include_directories(next_compression PRIVATE ${NEXT_ZSTD_INCLUDE_DIR})
    target_link_libraries(next_compression PUBLIC ${NEXT_ZSTD_LIBRARY})
    message(STATUS "next_compression: Zstd enabled")
else()
    message(STATUS "next_compression: Zstd not found, backend disabled")
endif()
