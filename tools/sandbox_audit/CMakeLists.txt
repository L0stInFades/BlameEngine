# Sandbox security audit harness (ADR-0008). A standalone headless program that boots a real
# authoritative world + Game API + reference physics, then drives the player-code sandbox with a
# battery of hand-written guest programs: API-boundary mapping, ISA/language coverage, and
# adversarial boundary-condition probes. Its stdout IS the audit evidence (see
# docs/security/sandbox-audit-2026-05-30.md). Not a unit test — it is an investigative tool, run
# on demand; the repeatable subset of its assertions lives in tests/sandbox/*.
add_executable(sandbox_audit
    src/main.cpp
)

target_compile_features(sandbox_audit PRIVATE cxx_std_17)

# next_sandbox transitively pulls next_gameapi -> next_runtime/next_foundation; next_gameplay pulls
# next_physics. These two leaves cover the whole moat without duplicate-link warnings.
target_link_libraries(sandbox_audit PRIVATE
    next_sandbox
    next_gameplay
)

if(NOT MSVC)
    target_compile_options(sandbox_audit PRIVATE
        -Wall -Wextra -Wformat=2 -Werror=format -Wno-unused-parameter
    )
endif()
