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
find_program(WASM_CLANGXX NAMES clang++-22 clang++ PATHS ${_llvm_dirs} NO_DEFAULT_PATH)
if(NOT WASM_CLANGXX AND NOT APPLE)
    find_program(WASM_CLANGXX NAMES clang++-22 clang++)
endif()
find_program(WASM_LD NAMES wasm-ld-22 wasm-ld PATHS ${_lld_dirs} NO_DEFAULT_PATH)
if(NOT WASM_LD AND NOT APPLE)
    find_program(WASM_LD NAMES wasm-ld-22 wasm-ld)
endif()
find_program(RUSTC NAMES rustc)

set(_guests "")

if(WASM_CLANGXX AND WASM_LD)
    file(WRITE ${CMAKE_BINARY_DIR}/wasm_guests_clang_probe.cpp "extern \"C\" int run() { return 0; }\n")
    execute_process(
        COMMAND ${WASM_CLANGXX} --target=wasm32 -std=c++23 -nostdlib -fno-exceptions -fno-rtti
                -c ${CMAKE_BINARY_DIR}/wasm_guests_clang_probe.cpp
                -o ${CMAKE_BINARY_DIR}/wasm_guests_clang_probe.o
        RESULT_VARIABLE _wasm_clang_probe_result
        OUTPUT_QUIET
        ERROR_VARIABLE _wasm_clang_probe_error)

    if(_wasm_clang_probe_result EQUAL 0)
        function(next_add_cpp_wasm_guest guest_name guest_source)
            set(_guest_obj ${WASM_GUESTS_OUT}/${guest_name}.wasm.o)
            set(_guest_wasm ${WASM_GUESTS_OUT}/${guest_name}.wasm)
            add_custom_command(
                OUTPUT ${_guest_wasm}
                BYPRODUCTS ${_guest_obj}
                COMMAND ${WASM_CLANGXX} --target=wasm32 -std=c++23 -O2 -nostdlib -fno-exceptions -fno-rtti
                        -I${CMAKE_CURRENT_SOURCE_DIR}
                        -c -o ${_guest_obj} ${guest_source}
                COMMAND ${WASM_LD} --no-entry --export=run --allow-undefined
                        -o ${_guest_wasm} ${_guest_obj}
                DEPENDS ${guest_source} ${ARGN}
                COMMENT "wasm guest: C++ ${guest_name} -> ${guest_name}.wasm"
                VERBATIM)
            list(APPEND _guests ${_guest_wasm})
            set(_guests ${_guests} PARENT_SCOPE)
        endfunction()

        next_add_cpp_wasm_guest(astar
            ${CMAKE_CURRENT_SOURCE_DIR}/astar.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/guest_api.h)

        # Adversarial CPU-fuel guests (no Game API import): an infinite loop and a counted loop.
        # They prove the load-time gas instrumentation bounds untrusted code (FuelExhausted) — see wasm_demo.
        foreach(g spin busyloop)
            next_add_cpp_wasm_guest(${g} ${CMAKE_CURRENT_SOURCE_DIR}/${g}.cpp)
        endforeach()
        message(STATUS "NEXT wasm guests: C++ via ${WASM_CLANGXX} (+ ${WASM_LD})")
    else()
        string(STRIP "${_wasm_clang_probe_error}" _wasm_clang_probe_error)
        message(WARNING
            "NEXT wasm guests: ${WASM_CLANGXX} cannot compile --target=wasm32; "
            "skipping C++ guests. ${_wasm_clang_probe_error}")
    endif()
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
