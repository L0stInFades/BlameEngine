# HackOps prototype target.

add_executable(hackops_demo
    src/main.cpp
)

target_compile_features(hackops_demo PRIVATE cxx_std_17)

if(NOT MSVC)
    target_compile_options(hackops_demo PRIVATE
        -Wformat=2
        -Werror=format
    )
endif()

target_link_libraries(hackops_demo PRIVATE
    next_terminal
)

if(WIN32)
    target_compile_definitions(hackops_demo PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()
