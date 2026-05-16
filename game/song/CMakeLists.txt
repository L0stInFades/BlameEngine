# Song Dynasty Game - CP0 Demo

add_executable(song_demo
    src/main.cpp
    src/game.cpp
)

target_include_directories(song_demo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(song_demo PRIVATE
    next_renderer
    next_world
)

if(NOT MSVC)
    target_compile_options(song_demo PRIVATE
        -Wformat=2
        -Werror=format
    )
endif()
