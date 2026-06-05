# Compile the C++ / Rust player guests to wasm32 at build time (ADR-0011). This is the
# player-language toolchain in miniature: modern C++ and Rust -> wasm32 -> the sandbox.
#
# Requirements (auto-detected, override via cache vars):
#   * WASM_CLANGXX  a clang++ WITH the wasm32 target (NOT Apple clang) + WASM_LD (wasm-ld)
#   * RUSTC         rustc with the wasm32-unknown-unknown target installed
# A missing toolchain skips only that language's guest (configure still succeeds); the wasm_demo
# harness reports which guests are present.

set(WASM_GUESTS_OUT ${CMAKE_BINARY_DIR}/wasm_guests CACHE INTERNAL "compiled wasm guest dir")
file(MAKE_DIRECTORY ${WASM_GUESTS_OUT})

set(_llvm_dirs
    /usr/local/opt/llvm/bin
    /opt/homebrew/opt/llvm/bin
    "C:/Program Files/LLVM/bin"
)
file(GLOB _lld_dirs
    /usr/local/opt/lld/bin /opt/homebrew/opt/lld/bin
    /usr/local/Cellar/lld@*/*/bin /opt/homebrew/Cellar/lld@*/*/bin
    /usr/local/opt/llvm/bin /opt/homebrew/opt/llvm/bin
    "C:/Program Files/LLVM/bin")
find_program(WASM_CLANGXX NAMES clang++ clang++-22 PATHS ${_llvm_dirs})
find_program(WASM_LD NAMES wasm-ld PATHS ${_lld_dirs})
find_program(RUSTC NAMES rustc)

set(_guests "")

if(WASM_CLANGXX AND WASM_LD)
    # clang's wasm driver spawns `wasm-ld` by name; put its dir on PATH for the compile step (more
    # robust here than --ld-path). lld@21 + clang 22 agree on the wasm object format.
    get_filename_component(WASM_LD_DIR ${WASM_LD} DIRECTORY)
    add_custom_command(
        OUTPUT ${WASM_GUESTS_OUT}/astar.wasm
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${WASM_LD_DIR}:$ENV{PATH}"
                ${WASM_CLANGXX} --target=wasm32 -std=c++23 -O2 -nostdlib -fno-exceptions -fno-rtti
                -I${CMAKE_CURRENT_SOURCE_DIR}
                -Wl,--no-entry -Wl,--export=run -Wl,--allow-undefined
                -o ${WASM_GUESTS_OUT}/astar.wasm ${CMAKE_CURRENT_SOURCE_DIR}/astar.cpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/astar.cpp ${CMAKE_CURRENT_SOURCE_DIR}/guest_api.h
        COMMENT "wasm guest: C++23 A* -> astar.wasm"
        VERBATIM)
    list(APPEND _guests ${WASM_GUESTS_OUT}/astar.wasm)

    # Adversarial CPU-fuel guests (no Game API import): an infinite loop and a counted loop. They
    # prove the load-time gas instrumentation bounds untrusted code (FuelExhausted) — see wasm_demo.
    foreach(g spin busyloop)
        add_custom_command(
            OUTPUT ${WASM_GUESTS_OUT}/${g}.wasm
            COMMAND ${CMAKE_COMMAND} -E env "PATH=${WASM_LD_DIR}:$ENV{PATH}"
                    ${WASM_CLANGXX} --target=wasm32 -std=c++23 -O2 -nostdlib -fno-exceptions -fno-rtti
                    -Wl,--no-entry -Wl,--export=run -Wl,--allow-undefined
                    -o ${WASM_GUESTS_OUT}/${g}.wasm ${CMAKE_CURRENT_SOURCE_DIR}/${g}.cpp
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${g}.cpp
            COMMENT "wasm guest: C++ ${g} -> ${g}.wasm"
            VERBATIM)
        list(APPEND _guests ${WASM_GUESTS_OUT}/${g}.wasm)
    endforeach()
    message(STATUS "NEXT wasm guests: C++ via ${WASM_CLANGXX} (+ ${WASM_LD})")
else()
    message(WARNING "NEXT wasm guests: no wasm-capable clang++/wasm-ld — skipping C++ A* guest")
endif()

if(RUSTC)
    add_custom_command(
        OUTPUT ${WASM_GUESTS_OUT}/mapsearch.wasm
        COMMAND ${RUSTC} --edition 2024 --target wasm32-unknown-unknown -O --crate-type=cdylib
                -o ${WASM_GUESTS_OUT}/mapsearch.wasm ${CMAKE_CURRENT_SOURCE_DIR}/mapsearch.rs
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/mapsearch.rs
        COMMENT "wasm guest: Rust 2024 binary-search -> mapsearch.wasm"
        VERBATIM)
    list(APPEND _guests ${WASM_GUESTS_OUT}/mapsearch.wasm)
    message(STATUS "NEXT wasm guests: Rust via ${RUSTC}")
else()
    message(WARNING "NEXT wasm guests: rustc not found — skipping Rust binary-search guest")
endif()

if(_guests)
    add_custom_target(wasm_guests ALL DEPENDS ${_guests})
endif()
