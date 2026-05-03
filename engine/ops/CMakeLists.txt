add_library(next_ops STATIC
    src/ops_workspace.cpp
    src/python_worker.cpp
)

target_include_directories(next_ops
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(next_ops PUBLIC cxx_std_17)
