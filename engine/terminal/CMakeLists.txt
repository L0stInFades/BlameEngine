add_library(next_terminal STATIC
    src/nvim_surface.cpp
)

target_include_directories(next_terminal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(next_terminal PUBLIC cxx_std_17)

if(WIN32)
    target_compile_definitions(next_terminal PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()
