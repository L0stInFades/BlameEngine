add_library(next_game STATIC
    src/camera_graph.cpp
    src/input_curves.cpp
    src/game_feel.cpp
)

target_include_directories(next_game
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(next_game PUBLIC next_renderer next_runtime next_foundation)

if(NOT MSVC)
    target_compile_options(next_game PRIVATE
        -Wformat=2
        -Werror=format
    )
endif()
