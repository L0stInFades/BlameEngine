add_library(next_renderer STATIC
    src/renderer.cpp
)

if(WIN32)
    target_sources(next_renderer PRIVATE
        src/render_graph.cpp
        src/dx12/device.cpp
        src/dx12/fence.cpp
        src/dx12/command_queue.cpp
        src/dx12/command_list.cpp
        src/dx12/swapchain.cpp
        src/dx12/descriptor_heap.cpp
        src/dx12/descriptor_allocator.cpp
        src/dx12/texture.cpp
        src/dx12/sampler.cpp
        src/dx12/material.cpp
        src/dx12/dx12_renderer.cpp
        src/dx12/shader.cpp
        src/dx12/root_signature.cpp
        src/dx12/pipeline_state.cpp
        src/dx12/buffer.cpp
        src/dx12/constant_buffer.cpp
        src/dx12/depth_buffer.cpp
        src/dx12/mesh_shader_pass.cpp
        src/dx12/taa.cpp
        src/dx12/post_processing.cpp
        src/dx12/debug_views.cpp
        src/dx12/ambient_occlusion.cpp
        src/dx12/light_probe.cpp
        src/dx12/global_illumination.cpp
        src/dx12/rendering_stats.cpp
        src/dx12/shader_compiler.cpp
    )
endif()

if(APPLE)
    set(NEXT_METAL_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal")
    set(NEXT_METAL_SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders/metal")
    set(NEXT_DEMO_FORWARD_MSL "${NEXT_METAL_SHADER_SOURCE_DIR}/demo_forward.metal")
    set(NEXT_DEMO_FORWARD_MANIFEST "${NEXT_METAL_SHADER_SOURCE_DIR}/demo_forward.shader_manifest")
    set(NEXT_DEMO_FORWARD_RUNTIME_MSL "${NEXT_METAL_SHADER_BINARY_DIR}/demo_forward.metal")
    set(NEXT_DEMO_FORWARD_RUNTIME_MANIFEST "${NEXT_METAL_SHADER_BINARY_DIR}/demo_forward.shader_manifest")
    set(NEXT_DEMO_FORWARD_AIR "${NEXT_METAL_SHADER_BINARY_DIR}/demo_forward.air")
    set(NEXT_DEMO_FORWARD_METALLIB "${NEXT_METAL_SHADER_BINARY_DIR}/demo_forward.metallib")

    target_sources(next_renderer PRIVATE
        src/metal/metal_command.mm
        src/metal/metal_conversions.mm
        src/metal/metal_device.mm
        src/metal/metal_gpu_resource.mm
        src/metal/metal_pipeline.mm
        src/metal/metal_release_queue.mm
        src/metal/metal_render_pass.mm
        src/metal/metal_resource.mm
        src/metal/metal_resource_pool.mm
        src/metal/metal_renderer.mm
        src/metal/metal_shader_library.mm
        src/metal/metal_swapchain.mm
        src/metal/metal_upload_queue.mm
    )
    set_source_files_properties(
        src/metal/metal_command.mm
        src/metal/metal_conversions.mm
        src/metal/metal_device.mm
        src/metal/metal_gpu_resource.mm
        src/metal/metal_pipeline.mm
        src/metal/metal_release_queue.mm
        src/metal/metal_render_pass.mm
        src/metal/metal_resource.mm
        src/metal/metal_resource_pool.mm
        src/metal/metal_renderer.mm
        src/metal/metal_shader_library.mm
        src/metal/metal_swapchain.mm
        src/metal/metal_upload_queue.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )

    add_custom_command(
        OUTPUT
            ${NEXT_DEMO_FORWARD_RUNTIME_MSL}
            ${NEXT_DEMO_FORWARD_RUNTIME_MANIFEST}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${NEXT_METAL_SHADER_BINARY_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${NEXT_DEMO_FORWARD_MSL}
                ${NEXT_DEMO_FORWARD_RUNTIME_MSL}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${NEXT_DEMO_FORWARD_MANIFEST}
                ${NEXT_DEMO_FORWARD_RUNTIME_MANIFEST}
        DEPENDS
            ${NEXT_DEMO_FORWARD_MSL}
            ${NEXT_DEMO_FORWARD_MANIFEST}
        VERBATIM
    )
    add_custom_target(next_metal_shader_assets
        DEPENDS
            ${NEXT_DEMO_FORWARD_RUNTIME_MSL}
            ${NEXT_DEMO_FORWARD_RUNTIME_MANIFEST}
    )
    add_dependencies(next_renderer next_metal_shader_assets)

    find_program(NEXT_XCRUN_EXECUTABLE xcrun)
    if(NEXT_XCRUN_EXECUTABLE)
        execute_process(
            COMMAND ${NEXT_XCRUN_EXECUTABLE} -sdk macosx -f metal
            RESULT_VARIABLE NEXT_METAL_COMPILER_RESULT
            OUTPUT_VARIABLE NEXT_METAL_COMPILER
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
            COMMAND ${NEXT_XCRUN_EXECUTABLE} -sdk macosx -f metallib
            RESULT_VARIABLE NEXT_METALLIB_RESULT
            OUTPUT_VARIABLE NEXT_METALLIB_COMPILER
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    endif()

    if(NEXT_XCRUN_EXECUTABLE AND
       NEXT_METAL_COMPILER_RESULT EQUAL 0 AND
       NEXT_METALLIB_RESULT EQUAL 0)
        add_custom_command(
            OUTPUT ${NEXT_DEMO_FORWARD_AIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${NEXT_METAL_SHADER_BINARY_DIR}
            COMMAND ${NEXT_XCRUN_EXECUTABLE} -sdk macosx metal
                    -c ${NEXT_DEMO_FORWARD_MSL}
                    -o ${NEXT_DEMO_FORWARD_AIR}
            DEPENDS ${NEXT_DEMO_FORWARD_MSL}
            VERBATIM
        )
        add_custom_command(
            OUTPUT ${NEXT_DEMO_FORWARD_METALLIB}
            COMMAND ${NEXT_XCRUN_EXECUTABLE} -sdk macosx metallib
                    ${NEXT_DEMO_FORWARD_AIR}
                    -o ${NEXT_DEMO_FORWARD_METALLIB}
            DEPENDS ${NEXT_DEMO_FORWARD_AIR}
            VERBATIM
        )
        add_custom_target(next_metal_shaders DEPENDS ${NEXT_DEMO_FORWARD_METALLIB})
        add_dependencies(next_renderer next_metal_shaders)
        message(STATUS "next_renderer: Metal shader offline compile enabled")
        target_compile_definitions(next_renderer PRIVATE
            "NEXT_METAL_SHADER_LIBRARY_PATH=\"${NEXT_DEMO_FORWARD_METALLIB}\""
        )
    else()
        message(STATUS "next_renderer: Metal shader offline compile unavailable; using runtime MSL source fallback")
    endif()

    target_compile_definitions(next_renderer PRIVATE
        "NEXT_METAL_SHADER_DIR=\"${NEXT_METAL_SHADER_BINARY_DIR}\""
    )
endif()

target_include_directories(next_renderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(NOT MSVC)
    target_compile_options(next_renderer PRIVATE
        -Wformat=2
        -Werror=format
    )
endif()

if(WIN32)
    target_link_libraries(next_renderer PUBLIC next_rhi next_runtime next_platform d3d12 dxgi d3dcompiler)
elseif(APPLE)
    target_link_libraries(next_renderer PUBLIC
        next_rhi
        next_runtime
        next_platform
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
    )
else()
    target_link_libraries(next_renderer PUBLIC next_rhi next_runtime next_platform)
endif()
