# wasm_demo (ADR-0011): boots a headless world and runs the C++/Rust player guests (compiled to
# wasm32 by examples/wasm_guests) on the Wasm3Sandbox backend, verifying their results against
# host-side ground truth. The proof that real C++ and Rust run inside the sandbox.
add_executable(wasm_demo src/main.cpp)

target_compile_features(wasm_demo PRIVATE cxx_std_17)

# next_sandbox_wasm transitively pulls next_sandbox -> next_gameapi -> next_runtime/next_foundation.
target_link_libraries(wasm_demo PRIVATE next_sandbox_wasm)

# Where examples/wasm_guests dropped the compiled .wasm files.
target_compile_definitions(wasm_demo PRIVATE WASM_GUESTS_DIR="${WASM_GUESTS_OUT}")

if(TARGET wasm_guests)
    add_dependencies(wasm_demo wasm_guests)
endif()

if(NOT MSVC)
    target_compile_options(wasm_demo PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
