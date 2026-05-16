# NEXT Engine Unit Tests
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

# Math Tests
add_executable(test_math
    math/test_math.cpp
)
target_include_directories(test_math PRIVATE ${CMAKE_SOURCE_DIR}/engine/renderer/include)
target_link_libraries(test_math PRIVATE ${NEXT_TEST_MAIN})
add_test(NAME MathTest COMMAND test_math)

# Renderer API Tests
add_executable(test_renderer_api
    renderer/test_renderer_api.cpp
)
target_include_directories(test_renderer_api PRIVATE ${CMAKE_SOURCE_DIR}/engine/renderer/include)
target_link_libraries(test_renderer_api PRIVATE ${NEXT_TEST_MAIN} next_renderer)
add_test(NAME RendererApiTest COMMAND test_renderer_api)

if(APPLE AND BUILD_RENDERER)
    add_executable(test_metal_release_queue
        renderer/test_metal_release_queue.mm
    )
    target_include_directories(test_metal_release_queue PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/renderer/src/metal
    )
    target_link_libraries(test_metal_release_queue PRIVATE ${NEXT_TEST_MAIN} next_renderer)
    set_source_files_properties(renderer/test_metal_release_queue.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    add_test(NAME MetalReleaseQueueTest COMMAND test_metal_release_queue)

    add_executable(test_metal_device
        renderer/test_metal_device.mm
    )
    target_include_directories(test_metal_device PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/renderer/src/metal
    )
    target_link_libraries(test_metal_device PRIVATE ${NEXT_TEST_MAIN} next_renderer)
    set_source_files_properties(renderer/test_metal_device.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    add_test(NAME MetalDeviceTest COMMAND test_metal_device)

    add_executable(test_metal_resource_pool
        renderer/test_metal_resource_pool.mm
    )
    target_include_directories(test_metal_resource_pool PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/renderer/src/metal
    )
    target_link_libraries(test_metal_resource_pool PRIVATE ${NEXT_TEST_MAIN} next_renderer)
    set_source_files_properties(renderer/test_metal_resource_pool.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    add_test(NAME MetalResourcePoolTest COMMAND test_metal_resource_pool)

    add_executable(test_metal_gpu_resource
        renderer/test_metal_gpu_resource.mm
    )
    target_include_directories(test_metal_gpu_resource PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/renderer/src/metal
    )
    target_link_libraries(test_metal_gpu_resource PRIVATE ${NEXT_TEST_MAIN} next_renderer)
    set_source_files_properties(renderer/test_metal_gpu_resource.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    add_test(NAME MetalGpuResourceTest COMMAND test_metal_gpu_resource)

    add_executable(test_metal_upload_queue
        renderer/test_metal_upload_queue.mm
    )
    target_include_directories(test_metal_upload_queue PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/renderer/src/metal
    )
    target_link_libraries(test_metal_upload_queue PRIVATE ${NEXT_TEST_MAIN} next_renderer)
    set_source_files_properties(renderer/test_metal_upload_queue.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    add_test(NAME MetalUploadQueueTest COMMAND test_metal_upload_queue)

    add_executable(test_metal_scene_resources
        renderer/test_metal_scene_resources.mm
    )
    target_include_directories(test_metal_scene_resources PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/renderer/src/metal
    )
    target_link_libraries(test_metal_scene_resources PRIVATE ${NEXT_TEST_MAIN} next_renderer)
    set_source_files_properties(renderer/test_metal_scene_resources.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    add_test(NAME MetalSceneResourcesTest COMMAND test_metal_scene_resources)

    add_executable(test_metal_swapchain
        renderer/test_metal_swapchain.mm
    )
    target_include_directories(test_metal_swapchain PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/renderer/src/metal
    )
    target_link_libraries(test_metal_swapchain PRIVATE ${NEXT_TEST_MAIN} next_renderer)
    set_source_files_properties(renderer/test_metal_swapchain.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    add_test(NAME MetalSwapchainTest COMMAND test_metal_swapchain)

    add_executable(test_metal_render_pass
        renderer/test_metal_render_pass.mm
    )
    target_include_directories(test_metal_render_pass PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/renderer/src/metal
    )
    target_link_libraries(test_metal_render_pass PRIVATE ${NEXT_TEST_MAIN} next_renderer)
    set_source_files_properties(renderer/test_metal_render_pass.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    add_test(NAME MetalRenderPassBuilderTest COMMAND test_metal_render_pass)

    add_executable(test_metal_sampler_cache
        renderer/test_metal_sampler_cache.mm
    )
    target_include_directories(test_metal_sampler_cache PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/renderer/src/metal
    )
    target_link_libraries(test_metal_sampler_cache PRIVATE ${NEXT_TEST_MAIN} next_renderer)
    set_source_files_properties(renderer/test_metal_sampler_cache.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    add_test(NAME MetalSamplerCacheTest COMMAND test_metal_sampler_cache)

    add_executable(test_metal_conversions
        renderer/test_metal_conversions.mm
    )
    target_include_directories(test_metal_conversions PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/renderer/src/metal
    )
    target_link_libraries(test_metal_conversions PRIVATE ${NEXT_TEST_MAIN} next_renderer)
    set_source_files_properties(renderer/test_metal_conversions.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    add_test(NAME MetalConversionsTest COMMAND test_metal_conversions)

    add_executable(test_metal_command
        renderer/test_metal_command.mm
    )
    target_include_directories(test_metal_command PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/renderer/src/metal
    )
    target_link_libraries(test_metal_command PRIVATE ${NEXT_TEST_MAIN} next_renderer)
    set_source_files_properties(renderer/test_metal_command.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    add_test(NAME MetalCommandContextTest COMMAND test_metal_command)

    add_executable(test_metal_pipeline
        renderer/test_metal_pipeline.mm
    )
    target_include_directories(test_metal_pipeline PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/renderer/src/metal
    )
    target_link_libraries(test_metal_pipeline PRIVATE ${NEXT_TEST_MAIN} next_renderer)
    set_source_files_properties(renderer/test_metal_pipeline.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    add_test(NAME MetalPipelineTest COMMAND test_metal_pipeline)

    add_executable(test_metal_shader_library
        renderer/test_metal_shader_library.mm
    )
    target_include_directories(test_metal_shader_library PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/renderer/src/metal
    )
    target_link_libraries(test_metal_shader_library PRIVATE ${NEXT_TEST_MAIN} next_renderer)
    set_source_files_properties(renderer/test_metal_shader_library.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    add_test(NAME MetalShaderLibraryTest COMMAND test_metal_shader_library)
endif()

# RHI Tests
add_executable(test_rhi
    rhi/test_rhi_types.cpp
)
target_link_libraries(test_rhi PRIVATE ${NEXT_TEST_MAIN} next_rhi)
add_test(NAME RHITest COMMAND test_rhi)

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
    test_renderer_api
    test_rhi
    test_world_streaming
    test_task_system
    test_script_system
)

if(TARGET test_metal_release_queue)
    list(APPEND NEXT_TEST_TARGETS
        test_metal_release_queue
        test_metal_device
        test_metal_resource_pool
        test_metal_gpu_resource
        test_metal_upload_queue
        test_metal_scene_resources
        test_metal_swapchain
        test_metal_render_pass
        test_metal_sampler_cache
        test_metal_conversions
        test_metal_command
        test_metal_pipeline
        test_metal_shader_library
    )
endif()

set_target_properties(${NEXT_TEST_TARGETS}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
)
