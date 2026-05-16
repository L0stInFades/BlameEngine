add_library(next_rhi STATIC
    src/rhi.cpp
)

target_include_directories(next_rhi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(next_rhi PUBLIC cxx_std_17)

if(NOT MSVC)
    target_compile_options(next_rhi PRIVATE
        -Wformat=2
        -Werror=format
    )
endif()
