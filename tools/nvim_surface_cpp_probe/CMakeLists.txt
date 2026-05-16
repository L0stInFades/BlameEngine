add_executable(next_nvim_surface_probe
    src/main.cpp
)

target_compile_features(next_nvim_surface_probe PRIVATE cxx_std_17)

if(NOT MSVC)
    target_compile_options(next_nvim_surface_probe PRIVATE
        -Wformat=2
        -Werror=format
    )
endif()

target_link_libraries(next_nvim_surface_probe PRIVATE
    next_terminal
)
