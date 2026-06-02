# Blame Engine Unit Tests
# Using Google Test framework

set(NEXT_TEST_MAIN gtest_main)

function(next_enable_format_warnings target_name)
    if(NOT MSVC)
        target_compile_options(${target_name} PRIVATE
            -Wformat=2
            -Werror=format
        )
    endif()
endfunction()

# Foundation Tests
add_executable(test_foundation
    foundation/test_assert.cpp
    foundation/test_logger.cpp
)
target_link_libraries(test_foundation PRIVATE ${NEXT_TEST_MAIN} next_foundation)
next_enable_format_warnings(test_foundation)
add_test(NAME FoundationTest COMMAND test_foundation)

# Serialization Tests
add_executable(test_serialization
    serialization/test_serialization.cpp
)
target_link_libraries(test_serialization PRIVATE ${NEXT_TEST_MAIN} next_serialization)
add_test(NAME SerializationTest COMMAND test_serialization)

# JobSystem Tests
add_executable(test_jobsystem
    jobsystem/test_job_system.cpp
    jobsystem/test_job.cpp
)
target_link_libraries(test_jobsystem PRIVATE ${NEXT_TEST_MAIN} next_jobsystem)
add_test(NAME JobSystemTest COMMAND test_jobsystem)

# Runtime ECS Tests
add_executable(test_runtime
    runtime/test_world.cpp
    runtime/test_entity.cpp
    runtime/test_component.cpp
    runtime/test_query.cpp
    runtime/test_system.cpp
    runtime/test_asset_integration.cpp
    runtime/test_edge_cases.cpp
    runtime/test_command_buffer.cpp
)
target_link_libraries(test_runtime PRIVATE ${NEXT_TEST_MAIN} next_runtime)
add_test(NAME RuntimeTest COMMAND test_runtime)

# Asset compiler tests
add_executable(test_asset_compiler
    tools/test_asset_compiler.cpp
    ${CMAKE_SOURCE_DIR}/tools/assetc/src/asset_compiler.cpp
    ${CMAKE_SOURCE_DIR}/tools/assetc/src/obj_loader.cpp
    ${CMAKE_SOURCE_DIR}/tools/assetc/src/tga_loader.cpp
)
target_include_directories(test_asset_compiler PRIVATE
    ${CMAKE_SOURCE_DIR}/tools/assetc/src
    ${CMAKE_SOURCE_DIR}/engine/world/include
    ${CMAKE_SOURCE_DIR}/engine/compression/include
)
target_link_libraries(test_asset_compiler PRIVATE ${NEXT_TEST_MAIN} next_world)
next_enable_format_warnings(test_asset_compiler)
add_test(NAME AssetCompilerTest COMMAND test_asset_compiler)

# Platform Tests
add_executable(test_platform
    platform/test_window.cpp
    platform/test_input.cpp
)
target_link_libraries(test_platform PRIVATE ${NEXT_TEST_MAIN} next_platform next_foundation)
add_test(NAME PlatformTest COMMAND test_platform)

# Math Tests (math lives in engine/foundation; used by the headless world + sim↔UE5 boundary)
add_executable(test_math
    math/test_math.cpp
)
target_include_directories(test_math PRIVATE ${CMAKE_SOURCE_DIR}/engine/foundation/include)
target_link_libraries(test_math PRIVATE ${NEXT_TEST_MAIN})
add_test(NAME MathTest COMMAND test_math)

# World Streaming Tests (CP7)
add_executable(test_world_streaming
    world/test_streaming_manager.cpp
    world/test_interest_manager.cpp
)
target_include_directories(test_world_streaming PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/compression/include
)
target_link_libraries(test_world_streaming PRIVATE ${NEXT_TEST_MAIN} next_world)
add_test(NAME WorldStreamingTest COMMAND test_world_streaming)

# Task System Tests (CP9)
add_executable(test_task_system
    task/test_task_save_load.cpp
)
target_link_libraries(test_task_system PRIVATE ${NEXT_TEST_MAIN} next_task)
add_test(NAME TaskSystemTest COMMAND test_task_system)

# Script System Tests (CP8)
add_executable(test_script_system
    script/test_script_system.cpp
)
target_link_libraries(test_script_system PRIVATE ${NEXT_TEST_MAIN} next_script)
add_test(NAME ScriptSystemTest COMMAND test_script_system)

# Game API Tests (ADR-0007): capability gating, deterministic reads, write-as-intent, ABI dispatch
add_executable(test_gameapi
    gameapi/test_game_api.cpp
)
target_link_libraries(test_gameapi PRIVATE ${NEXT_TEST_MAIN} next_gameapi)
next_enable_format_warnings(test_gameapi)
add_test(NAME GameApiTest COMMAND test_gameapi)

# Sandbox Tests (ADR-0008): VM mechanics + traps, and the security contract (capability gating,
# zero ambient authority, determinism) verified against the real Game API gateway.
add_executable(test_sandbox
    sandbox/test_ref_vm.cpp
    sandbox/test_sandbox_security.cpp
    sandbox/test_sandbox_boundary.cpp
)
target_link_libraries(test_sandbox PRIVATE ${NEXT_TEST_MAIN} next_sandbox)
add_test(NAME SandboxTest COMMAND test_sandbox)

# Boundary Tests (ADR-0006): triple buffer, SPSC rings, snapshot publisher diffing, transport
add_executable(test_boundary
    boundary/test_boundary.cpp
)
target_link_libraries(test_boundary PRIVATE ${NEXT_TEST_MAIN} next_boundary)
add_test(NAME BoundaryTest COMMAND test_boundary)

# Vertical slice (ADR-0005): sandbox -> Game API -> authoritative world -> UE5 snapshot, headless.
add_executable(test_vertical_slice
    integration/test_vertical_slice.cpp
)
target_link_libraries(test_vertical_slice PRIVATE ${NEXT_TEST_MAIN} next_sandbox next_boundary)
add_test(NAME VerticalSliceTest COMMAND test_vertical_slice)

# Physics Tests (ADR-0009): IPhysicsWorld contract + deterministic reference backend + PhysicsSystem
add_executable(test_physics
    physics/test_physics.cpp
)
target_link_libraries(test_physics PRIVATE ${NEXT_TEST_MAIN} next_physics)
add_test(NAME PhysicsTest COMMAND test_physics)

# Gameplay actuation tests (ADR-0010): the single Transform-writer mover (intents -> physics/direct)
add_executable(test_actuation
    gameplay/test_actuation.cpp
)
target_link_libraries(test_actuation PRIVATE ${NEXT_TEST_MAIN} next_gameplay)
add_test(NAME ActuationTest COMMAND test_actuation)

# Full-stack physics slice (ADR-0005/0009/0010): sandbox -> Game API -> actuation -> physics -> snapshot
add_executable(test_physics_slice
    integration/test_physics_slice.cpp
)
target_link_libraries(test_physics_slice PRIVATE ${NEXT_TEST_MAIN} next_sandbox next_gameplay next_boundary)
add_test(NAME PhysicsSliceTest COMMAND test_physics_slice)

# Jolt-backed full vertical-slice (ADR-0009): same end-to-end shape as test_physics_slice but the
# physics world is the Jolt backend, proving the Jolt path cooperates with the rest of the engine
# (sandbox -> Game API -> resolver -> ActuationSystem -> PhysicsSystem -> SnapshotPublisher).
if(BUILD_WITH_JOLT)
    add_executable(test_jolt_physics_slice
        integration/test_jolt_physics_slice.cpp
    )
    target_link_libraries(test_jolt_physics_slice PRIVATE
        ${NEXT_TEST_MAIN}
        next_sandbox
        next_gameplay
        next_boundary
        next_physics_jolt
    )
    add_test(NAME JoltPhysicsSliceTest COMMAND test_jolt_physics_slice)
endif()

# Level-design system: validator (defect gate), loader (transactional/deterministic), win
# conditions, and an end-to-end load-run-win slice (sandbox guest drives a loaded level to Won).
add_executable(test_level
    level/test_level_validator.cpp
    level/test_level_loader.cpp
    level/test_level_win_conditions.cpp
    level/test_level_slice.cpp
)
target_link_libraries(test_level PRIVATE ${NEXT_TEST_MAIN} next_level next_sandbox)
add_test(NAME LevelTest COMMAND test_level)

# Vegetation system (ADR-0014): deterministic per-cell scatter, fail-closed validation, slope/altitude/
# mask filters, min-spacing, and the CellLayer::Vegetation blob round-trip. Pure next_vegetation —
# headless, no UE5/world/runtime coupling.
add_executable(test_vegetation
    vegetation/test_vegetation.cpp
)
target_link_libraries(test_vegetation PRIVATE ${NEXT_TEST_MAIN} next_vegetation)
next_enable_format_warnings(test_vegetation)
add_test(NAME VegetationTest COMMAND test_vegetation)

# Layered cell file format (ADR-0014): multi-layer chunk table (StaticMesh + Vegetation + ...),
# per-chunk codec, fail-closed parse. Links next_world (which pulls next_compression).
add_executable(test_layered_cell
    world/test_layered_cell_file.cpp
)
target_link_libraries(test_layered_cell PRIVATE ${NEXT_TEST_MAIN} next_world)
next_enable_format_warnings(test_layered_cell)
add_test(NAME LayeredCellTest COMMAND test_layered_cell)

# Vegetation cook (ADR-0014): def -> validate (fail-closed) -> scatter -> pack -> layered cell file,
# plus the text-def parser. Golden-hash stability + round-trip through ExtractLayer/UnpackCell.
add_executable(test_vegetation_cook
    vegetation_world/test_vegetation_cook.cpp
)
target_link_libraries(test_vegetation_cook PRIVATE ${NEXT_TEST_MAIN} next_vegetation_world)
next_enable_format_warnings(test_vegetation_cook)
add_test(NAME VegetationCookTest COMMAND test_vegetation_cook)

# Vegetation streaming integration (ADR-0014): cook a layered cell -> StreamingManager loads the
# Vegetation layer via real IO -> bytes round-trip to the cooked instances; fail-closed when missing.
add_executable(test_vegetation_streaming
    vegetation_world/test_vegetation_streaming.cpp
)
target_link_libraries(test_vegetation_streaming PRIVATE ${NEXT_TEST_MAIN} next_vegetation_world)
next_enable_format_warnings(test_vegetation_streaming)
add_test(NAME VegetationStreamingTest COMMAND test_vegetation_streaming)

# Runtime vegetation store (ADR-0014): index, radius/flags query, destructible overlay, deterministic order.
add_executable(test_vegetation_store
    vegetation_world/test_vegetation_store.cpp
)
target_link_libraries(test_vegetation_store PRIVATE ${NEXT_TEST_MAIN} next_vegetation_world)
next_enable_format_warnings(test_vegetation_store)
add_test(NAME VegetationStoreTest COMMAND test_vegetation_store)

# Vegetation gameplay queries (ADR-0014): segment LOS/cover + IWorldQuery raycast adapter (composes
# with a physics fallback) + destructible removal -> sim event.
add_executable(test_vegetation_query
    vegetation_world/test_vegetation_query.cpp
)
target_link_libraries(test_vegetation_query PRIVATE ${NEXT_TEST_MAIN} next_vegetation_world)
next_enable_format_warnings(test_vegetation_query)
add_test(NAME VegetationQueryTest COMMAND test_vegetation_query)

# Vegetation view contract (ADR-0014): bulk per-cell payload + boundary GameEvent for destruction,
# verified by a faithful UE5 consumer stand-in (groups by visual into HISM buckets; holds no authority).
add_executable(test_vegetation_view
    vegetation_world/test_vegetation_view.cpp
)
target_link_libraries(test_vegetation_view PRIVATE ${NEXT_TEST_MAIN} next_vegetation_world)
next_enable_format_warnings(test_vegetation_view)
add_test(NAME VegetationViewTest COMMAND test_vegetation_view)

# End-to-end vertical slice (ADR-0014): cook -> stream load -> store + UE-mirror -> LOS -> destroy -> unload.
add_executable(test_vegetation_slice
    vegetation_world/test_vegetation_slice.cpp
)
target_link_libraries(test_vegetation_slice PRIVATE ${NEXT_TEST_MAIN} next_vegetation_world)
next_enable_format_warnings(test_vegetation_slice)
add_test(NAME VegetationSliceTest COMMAND test_vegetation_slice)

# Scale / correctness-at-scale (ADR-0014): broadphase grid queries vs brute force over ~10k instances.
add_executable(test_vegetation_scale
    vegetation_world/test_vegetation_scale.cpp
)
target_link_libraries(test_vegetation_scale PRIVATE ${NEXT_TEST_MAIN} next_vegetation_world)
next_enable_format_warnings(test_vegetation_scale)
add_test(NAME VegetationScaleTest COMMAND test_vegetation_scale)

# Jolt backend tests — only when the optional Jolt backend is built (ADR-0009). The test target
# links next_physics_jolt (the Jolt IPhysicsWorld), next_gameplay (ActuationSystem) and next_gameapi
# (MoveTarget) so the same scenario suite as the reference backend (drop, raycast + surface normal,
# PhysicsSystem, ActuationSystem, mass override, determinism) can exercise the Jolt path end-to-end.
if(BUILD_WITH_JOLT)
    add_executable(test_jolt_physics
        physics/test_jolt_physics.cpp
    )
    target_link_libraries(test_jolt_physics PRIVATE
        ${NEXT_TEST_MAIN}
        next_physics_jolt
        next_gameplay
        next_gameapi
    )
    add_test(NAME JoltPhysicsTest COMMAND test_jolt_physics)
endif()

# Run all tests with CTest
set(NEXT_TEST_TARGETS
    test_foundation
    test_jobsystem
    test_runtime
    test_asset_compiler
    test_platform
    test_math
    test_world_streaming
    test_task_system
    test_script_system
    test_gameapi
    test_sandbox
    test_boundary
    test_vertical_slice
    test_physics
    test_actuation
    test_physics_slice
    test_level
    test_vegetation
    test_layered_cell
    test_vegetation_cook
    test_vegetation_streaming
    test_vegetation_store
    test_vegetation_query
    test_vegetation_view
    test_vegetation_slice
    test_vegetation_scale
)
if(BUILD_WITH_JOLT)
    list(APPEND NEXT_TEST_TARGETS test_jolt_physics test_jolt_physics_slice)
endif()

set_target_properties(${NEXT_TEST_TARGETS}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
)
