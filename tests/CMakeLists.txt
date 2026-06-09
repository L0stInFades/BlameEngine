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

# WASM backend security-contract tests (ADR-0011 + B2 fix) — only when the optional wasm3 backend
# is built. The guest modules are hand-encoded, so this needs no wasm toolchain and runs in CI
# (including the sanitizer matrix, closing the "WASM backend outside the ASan gate" hole).
if(BUILD_WITH_WASM)
    add_executable(test_sandbox_wasm sandbox/test_wasm_sandbox.cpp)
    target_link_libraries(test_sandbox_wasm PRIVATE ${NEXT_TEST_MAIN} next_sandbox_wasm)
    add_test(NAME WasmSandboxTest COMMAND test_sandbox_wasm)
endif()

# Boundary Tests (ADR-0006): triple buffer, SPSC rings, snapshot publisher diffing, transport
add_executable(test_boundary
    boundary/test_boundary.cpp
)
target_link_libraries(test_boundary PRIVATE ${NEXT_TEST_MAIN} next_boundary)
add_test(NAME BoundaryTest COMMAND test_boundary)

# Two-thread stress for the lock-free primitives (W16). Run under the `tsan` preset to verify the
# acquire/release ordering that pairs the atomics with the non-atomic slot memory (a real producer +
# consumer thread); also checks the logical SPSC/triple-buffer contract under any build.
find_package(Threads REQUIRED)
add_executable(test_boundary_concurrency boundary/test_boundary_concurrency.cpp)
target_link_libraries(test_boundary_concurrency PRIVATE ${NEXT_TEST_MAIN} next_boundary Threads::Threads)
add_test(NAME BoundaryConcurrencyTest COMMAND test_boundary_concurrency)

# Reliable snapshot protocol (B1 fix, W13): delta-against-acked-baseline + keyframe fallback so the UE5
# mirror converges to the authoritative state even when the latest-wins transport drops snapshots.
add_executable(test_boundary_reliability boundary/test_boundary_reliability.cpp)
target_link_libraries(test_boundary_reliability PRIVATE ${NEXT_TEST_MAIN} next_boundary)
add_test(NAME BoundaryReliabilityTest COMMAND test_boundary_reliability)

# Cross-process transport (W15): flat wire codec + DatagramTransport carrying the reliable protocol over
# a serialized, lossy datagram link; the mirror converges across the "wire" despite dropped datagrams.
add_executable(test_boundary_transport boundary/test_boundary_transport.cpp)
target_link_libraries(test_boundary_transport PRIVATE ${NEXT_TEST_MAIN} next_boundary)
add_test(NAME BoundaryTransportTest COMMAND test_boundary_transport)

# Real UDP loopback (W15): DatagramTransport over an actual 127.0.0.1 UDP socket. Best-effort — skips
# gracefully where the sandbox blocks UDP, so CI stays green on locked-down runners.
add_executable(test_boundary_udp boundary/test_boundary_udp.cpp)
target_link_libraries(test_boundary_udp PRIVATE ${NEXT_TEST_MAIN} next_boundary Threads::Threads)
add_test(NAME BoundaryUdpTest COMMAND test_boundary_udp)

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

# Vegetation on real terrain (ADR-0014): HeightmapTerrainSampler — on-surface placement, altitude
# bands, slope exclusion over an actual sloped heightmap (not flat).
add_executable(test_vegetation_terrain
    vegetation/test_vegetation_terrain.cpp
)
target_link_libraries(test_vegetation_terrain PRIVATE ${NEXT_TEST_MAIN} next_vegetation)
next_enable_format_warnings(test_vegetation_terrain)
add_test(NAME VegetationTerrainTest COMMAND test_vegetation_terrain)

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

# Running system (ADR-0014): VegetationStreamingSystem auto-ingests/evicts the store from streaming.
add_executable(test_vegetation_streaming_system
    vegetation_world/test_vegetation_streaming_system.cpp
)
target_link_libraries(test_vegetation_streaming_system PRIVATE ${NEXT_TEST_MAIN} next_vegetation_world)
next_enable_format_warnings(test_vegetation_streaming_system)
add_test(NAME VegetationStreamingSystemTest COMMAND test_vegetation_streaming_system)

# Adversarial parser fuzz (ADR-0014): random/truncated/bit-flipped bytes vs every parser under ASan/UBSan.
add_executable(test_vegetation_fuzz
    vegetation_world/test_vegetation_fuzz.cpp
)
target_link_libraries(test_vegetation_fuzz PRIVATE ${NEXT_TEST_MAIN} next_vegetation_world)
next_enable_format_warnings(test_vegetation_fuzz)
add_test(NAME VegetationFuzzTest COMMAND test_vegetation_fuzz)

# Water core (ADR-0015): deterministic Gerstner surface + deterministic trig, analytic submerged
# volume (sphere cap / box slab), Archimedes + velocity-clamped-drag buoyancy, fail-closed validator,
# NWTR cell round-trip, fluent builder, and the ocean spectrum. Pure next_water — headless, no coupling.
add_executable(test_water
    water/test_water.cpp
)
target_link_libraries(test_water PRIVATE ${NEXT_TEST_MAIN} next_water)
next_enable_format_warnings(test_water)
add_test(NAME WaterTest COMMAND test_water)

# Water cook (ADR-0015): scene -> validate -> per-cell select -> pack -> layered Water cell; golden
# bytes, fail-closed, the text-def parser, and round-trip through ExtractLayer/UnpackCell.
add_executable(test_water_cook water_world/test_water_cook.cpp)
target_link_libraries(test_water_cook PRIVATE ${NEXT_TEST_MAIN} next_water_world)
next_enable_format_warnings(test_water_cook)
add_test(NAME WaterCookTest COMMAND test_water_cook)

# Runtime water store (ADR-0015): de-dup across cells, broadphase, topmost-body, submersion, Ocean global.
add_executable(test_water_store water_world/test_water_store.cpp)
target_link_libraries(test_water_store PRIVATE ${NEXT_TEST_MAIN} next_water_world)
next_enable_format_warnings(test_water_store)
add_test(NAME WaterStoreTest COMMAND test_water_store)

# Water view contract (ADR-0015): Mock UE5 consumer receives the full surface params; splash GameEvents.
add_executable(test_water_view water_world/test_water_view.cpp)
target_link_libraries(test_water_view PRIVATE ${NEXT_TEST_MAIN} next_water_world)
next_enable_format_warnings(test_water_view)
add_test(NAME WaterViewTest COMMAND test_water_view)

# Buoyancy system (ADR-0015) — the headline: a real per-tick force sim on the reference backend.
# Archimedes equilibrium, no rocket-launch (drag clamp), current sweep, determinism, splash events.
add_executable(test_water_buoyancy water_world/test_water_buoyancy.cpp)
target_link_libraries(test_water_buoyancy PRIVATE ${NEXT_TEST_MAIN} next_water_world)
next_enable_format_warnings(test_water_buoyancy)
add_test(NAME WaterBuoyancyTest COMMAND test_water_buoyancy)

# Water gameplay queries (ADR-0015): WaterWorldQuery surface raycast (composes with a fallback) +
# height/submersion/stealth/conductivity helpers.
add_executable(test_water_query water_world/test_water_query.cpp)
target_link_libraries(test_water_query PRIVATE ${NEXT_TEST_MAIN} next_water_world)
next_enable_format_warnings(test_water_query)
add_test(NAME WaterQueryTest COMMAND test_water_query)

# Water streaming integration (ADR-0015): cook -> StreamingManager loads the Water layer via real IO ->
# WaterStreamingSystem.Sync ingests it; eviction + idempotent Sync.
add_executable(test_water_streaming water_world/test_water_streaming.cpp)
target_link_libraries(test_water_streaming PRIVATE ${NEXT_TEST_MAIN} next_water_world)
next_enable_format_warnings(test_water_streaming)
add_test(NAME WaterStreamingTest COMMAND test_water_streaming)

# End-to-end vertical slice (ADR-0015): cook -> stream load -> Sync -> sim floats a sphere -> splash
# event -> WaterWorldQuery composes -> unload. Proves the whole chain is wired, not just unit-green.
add_executable(test_water_slice water_world/test_water_slice.cpp)
target_link_libraries(test_water_slice PRIVATE ${NEXT_TEST_MAIN} next_water_world)
next_enable_format_warnings(test_water_slice)
add_test(NAME WaterSliceTest COMMAND test_water_slice)

# Scale / stress / determinism (ADR-0015): broadphase bounded+correct over many bodies; hundreds of
# varied-density bodies all settle to equilibrium with no rocket-launch over thousands of ticks; a
# multi-body sim is bit-deterministic across runs. Surpasses vegetation's scale test (no dynamics).
add_executable(test_water_scale water_world/test_water_scale.cpp)
target_link_libraries(test_water_scale PRIVATE ${NEXT_TEST_MAIN} next_water_world)
next_enable_format_warnings(test_water_scale)
add_test(NAME WaterScaleTest COMMAND test_water_scale)

# Adversarial parser fuzz (ADR-0015): random/truncated/header-corrupted bytes vs UnpackCell, random/
# truncated text vs ParseWaterDefText — all fail-closed, no OOB, under ASan/UBSan.
add_executable(test_water_fuzz water_world/test_water_fuzz.cpp)
target_link_libraries(test_water_fuzz PRIVATE ${NEXT_TEST_MAIN} next_water_world)
next_enable_format_warnings(test_water_fuzz)
add_test(NAME WaterFuzzTest COMMAND test_water_fuzz)

# Water x Actuation composition (W29): a player-driven boat (ActuationSystem -> body velocity) crossing a
# pool stays afloat (WaterForceSystem -> buoyancy force) — both run before PhysicsSystem. Pins the
# single-Transform-writer invariant, order-robustness of buoyancy, and three-system determinism.
add_executable(test_water_actuation water_world/test_water_actuation.cpp)
target_link_libraries(test_water_actuation PRIVATE ${NEXT_TEST_MAIN} next_water_world next_gameplay)
next_enable_format_warnings(test_water_actuation)
add_test(NAME WaterActuationTest COMMAND test_water_actuation)

# Replay-divergence diagnostic harness (W4): per-subsystem checksums (physics/water/actuation) that both
# confirm determinism and LOCALIZE an injected divergence to the right subsystem + earliest tick.
add_executable(test_water_replay water_world/test_water_replay.cpp)
target_link_libraries(test_water_replay PRIVATE ${NEXT_TEST_MAIN} next_water_world next_gameplay)
next_enable_format_warnings(test_water_replay)
add_test(NAME WaterReplayTest COMMAND test_water_replay)

# Water x electronics short-circuit (W11): a device submerged in conductive water shorts out (latched,
# edge-triggered, deterministic); a rising flood reaches and shorts a fixed device over time.
add_executable(test_water_hazard water_world/test_water_hazard.cpp)
target_link_libraries(test_water_hazard PRIVATE ${NEXT_TEST_MAIN} next_water_world)
next_enable_format_warnings(test_water_hazard)
add_test(NAME WaterHazardTest COMMAND test_water_hazard)

# Swim / drown / oxygen (W12): head-underwater depletes air, then drowns (latched, edge-triggered);
# surfacing refills; a wader breathes; deterministic.
add_executable(test_water_swim water_world/test_water_swim.cpp)
target_link_libraries(test_water_swim PRIVATE ${NEXT_TEST_MAIN} next_water_world)
next_enable_format_warnings(test_water_swim)
add_test(NAME WaterSwimTest COMMAND test_water_swim)

# UE5 water render contract (W17-W21): a consumer reconstructs the authoritative surface (height+normal)
# from ONLY the streamed cell bytes, to sub-mm, across all body types + time — the completeness proof
# for the out-of-repo UE5 renderer (ADR-0005). Authoring is the existing text/builder + assetc path.
add_executable(test_water_render water_world/test_water_render.cpp)
target_link_libraries(test_water_render PRIVATE ${NEXT_TEST_MAIN} next_water_world)
next_enable_format_warnings(test_water_render)
add_test(NAME WaterRenderTest COMMAND test_water_render)

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

    # Headline buoyancy scenarios on the Jolt backend (W6): Archimedes equilibrium, no rocket-launch,
    # current sweep, and boat self-right — the water force model driven through Jolt's BodyInterface.
    add_executable(test_water_buoyancy_jolt water_world/test_water_buoyancy_jolt.cpp)
    target_link_libraries(test_water_buoyancy_jolt PRIVATE ${NEXT_TEST_MAIN} next_water_world next_physics_jolt)
    add_test(NAME WaterBuoyancyJoltTest COMMAND test_water_buoyancy_jolt)
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
    test_boundary_concurrency
    test_boundary_reliability
    test_boundary_transport
    test_boundary_udp
    test_vertical_slice
    test_physics
    test_actuation
    test_physics_slice
    test_level
    test_vegetation
    test_vegetation_terrain
    test_layered_cell
    test_vegetation_cook
    test_vegetation_streaming
    test_vegetation_store
    test_vegetation_query
    test_vegetation_view
    test_vegetation_slice
    test_vegetation_scale
    test_vegetation_streaming_system
    test_vegetation_fuzz
    test_water
    test_water_cook
    test_water_store
    test_water_view
    test_water_buoyancy
    test_water_query
    test_water_streaming
    test_water_slice
    test_water_scale
    test_water_fuzz
    test_water_actuation
    test_water_replay
    test_water_hazard
    test_water_swim
    test_water_render
)
if(BUILD_WITH_JOLT)
    list(APPEND NEXT_TEST_TARGETS test_jolt_physics test_jolt_physics_slice test_water_buoyancy_jolt)
endif()

set_target_properties(${NEXT_TEST_TARGETS}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
)
