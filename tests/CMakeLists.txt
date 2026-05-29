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
)

set_target_properties(${NEXT_TEST_TARGETS}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
)
