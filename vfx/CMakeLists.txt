add_library(hbg_vfx STATIC
    src/version.cpp
    src/core/graph.cpp
    src/core/asset_json.cpp
    src/core/block_catalog.cpp
    src/core/subgraph.cpp
    src/core/custom_hlsl.cpp
    src/editor/document.cpp
    src/compiler/attribute_layout.cpp
    src/compiler/expression.cpp
    src/reference/cpu_reference.cpp
)
target_include_directories(hbg_vfx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(hbg_vfx PRIVATE ${HBG_NLOHMANN_JSON_TARGET})
hbg_target_options(hbg_vfx)

add_library(hbg_vfx_compiler STATIC
    src/compiler/codegen.cpp
    src/compiler/binary.cpp
    src/compiler/compiler.cpp
)
target_link_libraries(hbg_vfx_compiler PUBLIC hbg_vfx)
hbg_target_options(hbg_vfx_compiler)
add_library(vfx_compiler ALIAS hbg_vfx_compiler)

add_library(hbg_vfx_runtime STATIC
    src/runtime/runtime_asset.cpp
    src/runtime/runtime.cpp
)
target_link_libraries(hbg_vfx_runtime PUBLIC hbg_vfx)
hbg_target_options(hbg_vfx_runtime)

add_library(hbg_vfx_adapter STATIC
    ${CMAKE_SOURCE_DIR}/src/render/vfx/hbg_vfx_backend.cpp
)
target_include_directories(hbg_vfx_adapter PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(hbg_vfx_adapter PUBLIC hbg_vfx hbg_gfx)
target_compile_definitions(hbg_vfx_adapter PRIVATE HBG_SHADER_ROOT="${CMAKE_SOURCE_DIR}/shaders")
hbg_target_options(hbg_vfx_adapter)

add_executable(hbg_vfx_attributes_dump tools/attributes_dump.cpp)
target_link_libraries(hbg_vfx_attributes_dump PRIVATE hbg_vfx)
hbg_target_options(hbg_vfx_attributes_dump)

add_executable(hbg_vfx_expression_dump tools/expression_dump.cpp)
target_link_libraries(hbg_vfx_expression_dump PRIVATE hbg_vfx)
hbg_target_options(hbg_vfx_expression_dump)

add_executable(vfxbake tools/vfxbake.cpp)
target_link_libraries(vfxbake PRIVATE hbg_vfx_compiler)
hbg_target_options(vfxbake)

if(HBG_BUILD_TESTS)
    add_executable(hbg_vfx_tests
        tests/test_vfx_smoke.cpp
        tests/test_vfx_asset_json.cpp
        tests/test_vfx_editor.cpp
        tests/test_vfx_attribute_layout.cpp
        tests/test_vfx_expression.cpp
        tests/test_vfx_backend.cpp
        tests/test_vfx_reference.cpp
    )
    target_link_libraries(hbg_vfx_tests PRIVATE hbg_vfx doctest::doctest)
    target_compile_definitions(hbg_vfx_tests PRIVATE VFX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    hbg_target_options(hbg_vfx_tests)
    add_test(NAME hbg_vfx_tests COMMAND hbg_vfx_tests)

    add_executable(hbg_vfx_editor_roundtrip tests/editor_roundtrip.cpp)
    target_link_libraries(hbg_vfx_editor_roundtrip PRIVATE hbg_vfx)
    add_dependencies(hbg_vfx_editor_roundtrip vfxbake)
    hbg_target_options(hbg_vfx_editor_roundtrip)

    set(vfx_editor_roundtrip_bake_args)
    if(HBG_SHADER_COMPILER AND HBG_BGFX_SOURCE_DIR)
        list(APPEND vfx_editor_roundtrip_bake_args
            "-DHBG_VFXBAKE=$<TARGET_FILE:vfxbake>"
            "-DHBG_SHADERC=${HBG_SHADER_COMPILER}"
            "-DHBG_BGFX_SOURCE=${HBG_BGFX_SOURCE_DIR}"
            "-DHBG_SHADER_PACK=${CMAKE_SOURCE_DIR}/shaders")
    endif()
    add_test(NAME hbg_vfx_editor_roundtrip
        COMMAND ${CMAKE_COMMAND}
                "-DHBG_ROUNDTRIP=$<TARGET_FILE:hbg_vfx_editor_roundtrip>"
                "-DHBG_ASSET_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets"
                "-DHBG_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/editor_roundtrip_test"
                "-DHBG_EXPECTED_ASSETS=31"
                ${vfx_editor_roundtrip_bake_args}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_editor_roundtrip.cmake)
    set_tests_properties(hbg_vfx_editor_roundtrip PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 1800)

    add_executable(hbg_vfx_adapter_tests
        tests/test_vfx_hbg_backend.cpp
    )
    target_link_libraries(hbg_vfx_adapter_tests PRIVATE hbg_vfx_adapter hbg_vfx_runtime doctest::doctest)
    target_compile_definitions(hbg_vfx_adapter_tests PRIVATE VFX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    hbg_target_options(hbg_vfx_adapter_tests)
    add_test(NAME hbg_vfx_adapter_tests COMMAND hbg_vfx_adapter_tests)

    add_executable(hbg_vfx_compiler_tests
        tests/test_vfx_compiler.cpp
    )
    target_include_directories(hbg_vfx_compiler_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/compiler ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime)
    target_link_libraries(hbg_vfx_compiler_tests PRIVATE hbg_vfx_compiler hbg_vfx_runtime doctest::doctest)
    target_compile_definitions(hbg_vfx_compiler_tests PRIVATE VFX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    hbg_target_options(hbg_vfx_compiler_tests)
    if(HBG_SHADER_COMPILER AND HBG_BGFX_SOURCE_DIR)
        add_test(NAME hbg_vfx_compiler_tests
            COMMAND ${CMAKE_COMMAND} -E env
                    "VFX_TEST_SHADERC=${HBG_SHADER_COMPILER}"
                    "VFX_TEST_BGFX_SOURCE=${HBG_BGFX_SOURCE_DIR}"
                    "VFX_TEST_SHADER_PACK=${CMAKE_SOURCE_DIR}/shaders"
                    $<TARGET_FILE:hbg_vfx_compiler_tests>)
    else()
        add_test(NAME hbg_vfx_compiler_tests COMMAND hbg_vfx_compiler_tests)
    endif()

    if(HBG_SHADER_COMPILER AND HBG_BGFX_SOURCE_DIR)
        add_test(NAME hbg_vfx_asset_check
            COMMAND $<TARGET_FILE:vfxbake> --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/assets
                    --shaderc "${HBG_SHADER_COMPILER}"
                    --bgfx-src "${HBG_BGFX_SOURCE_DIR}"
                    --shader-pack "${CMAKE_SOURCE_DIR}/shaders")
        set_tests_properties(hbg_vfx_asset_check PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 900)
    endif()

    add_executable(hbg_vfx_runtime_tests
        tests/test_vfx_runtime.cpp
    )
    target_link_libraries(hbg_vfx_runtime_tests PRIVATE hbg_vfx_runtime doctest::doctest)
    target_include_directories(hbg_vfx_runtime_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime)
    target_compile_definitions(hbg_vfx_runtime_tests PRIVATE VFX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    hbg_target_options(hbg_vfx_runtime_tests)
    add_test(NAME hbg_vfx_runtime_tests COMMAND hbg_vfx_runtime_tests)

    if(WIN32)
        add_test(NAME hbg_vfx_reference_d3d11
            COMMAND $<TARGET_FILE:hbg> --backend=d3d11
                    --vfx-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_1024.vfxb
                    --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_1024.vfx.json
                    --no-vsync --width=64 --height=64)
        set_tests_properties(hbg_vfx_reference_d3d11 PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endif()
    add_test(NAME hbg_vfx_reference_vulkan
        COMMAND $<TARGET_FILE:hbg> --backend=vulkan
                --vfx-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_1024.vfxb
                --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_1024.vfx.json
                --no-vsync --width=64 --height=64)
    set_tests_properties(hbg_vfx_reference_vulkan PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)

    if(WIN32)
        add_test(NAME hbg_vfx_reference_auto_bounds_d3d11
            COMMAND ${CMAKE_COMMAND}
                    "-DHBG_EXE=$<TARGET_FILE:hbg>"
                    "-DHBG_BACKEND=d3d11"
                    "-DHBG_WORKDIR=${CMAKE_SOURCE_DIR}"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_auto_bounds.cmake)
        set_tests_properties(hbg_vfx_reference_auto_bounds_d3d11 PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endif()
    add_test(NAME hbg_vfx_reference_auto_bounds_vulkan
        COMMAND ${CMAKE_COMMAND}
                "-DHBG_EXE=$<TARGET_FILE:hbg>"
                "-DHBG_BACKEND=vulkan"
                "-DHBG_WORKDIR=${CMAKE_SOURCE_DIR}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_auto_bounds.cmake)
    set_tests_properties(hbg_vfx_reference_auto_bounds_vulkan PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)

    if(WIN32)
        add_test(NAME hbg_vfx_reference_forces_d3d11
            COMMAND $<TARGET_FILE:hbg> --backend=d3d11
                    --vfx-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/forces_1024.vfxb
                    --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/forces_1024.vfx.json
                    --no-vsync --width=64 --height=64)
        set_tests_properties(hbg_vfx_reference_forces_d3d11 PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endif()
    add_test(NAME hbg_vfx_reference_forces_vulkan
        COMMAND $<TARGET_FILE:hbg> --backend=vulkan
                --vfx-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/forces_1024.vfxb
                --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/forces_1024.vfx.json
                --no-vsync --width=64 --height=64)
    set_tests_properties(hbg_vfx_reference_forces_vulkan PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)

    if(WIN32)
        add_test(NAME hbg_vfx_reference_collision_d3d11
            COMMAND $<TARGET_FILE:hbg> --backend=d3d11
                    --vfx-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/collision_1024.vfxb
                    --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/collision_1024.vfx.json
                    --no-vsync --width=64 --height=64)
        set_tests_properties(hbg_vfx_reference_collision_d3d11 PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endif()
    add_test(NAME hbg_vfx_reference_collision_vulkan
        COMMAND $<TARGET_FILE:hbg> --backend=vulkan
                --vfx-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/collision_1024.vfxb
                --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/collision_1024.vfx.json
                --no-vsync --width=64 --height=64)
    set_tests_properties(hbg_vfx_reference_collision_vulkan PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)

    if(WIN32)
        add_test(NAME hbg_vfx_reference_curves_d3d11
            COMMAND $<TARGET_FILE:hbg> --backend=d3d11
                    --vfx-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/curve_1024.vfxb
                    --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/curve_1024.vfx.json
                    --no-vsync --width=64 --height=64)
        set_tests_properties(hbg_vfx_reference_curves_d3d11 PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endif()
    add_test(NAME hbg_vfx_reference_curves_vulkan
        COMMAND $<TARGET_FILE:hbg> --backend=vulkan
                --vfx-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/curve_1024.vfxb
                --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/curve_1024.vfx.json
                --no-vsync --width=64 --height=64)
    set_tests_properties(hbg_vfx_reference_curves_vulkan PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)

    if(WIN32)
        add_test(NAME hbg_vfx_reference_sorted_1024_d3d11
            COMMAND $<TARGET_FILE:hbg> --backend=d3d11
                    --vfx-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_sorted_1024.vfxb
                    --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_sorted_1024.vfx.json
                    --no-vsync --width=64 --height=64)
        set_tests_properties(hbg_vfx_reference_sorted_1024_d3d11 PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endif()
    add_test(NAME hbg_vfx_reference_sorted_1024_vulkan
        COMMAND $<TARGET_FILE:hbg> --backend=vulkan
                --vfx-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_sorted_1024.vfxb
                --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_sorted_1024.vfx.json
                --no-vsync --width=64 --height=64)
    set_tests_properties(hbg_vfx_reference_sorted_1024_vulkan PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)

    if(WIN32)
        add_test(NAME hbg_vfx_reference_sorted_8192_d3d11
            COMMAND $<TARGET_FILE:hbg> --backend=d3d11
                    --vfx-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_sorted_8192.vfxb
                    --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_sorted_8192.vfx.json
                    --no-vsync --width=64 --height=64)
        set_tests_properties(hbg_vfx_reference_sorted_8192_d3d11 PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180)
    endif()
    add_test(NAME hbg_vfx_reference_sorted_8192_vulkan
        COMMAND $<TARGET_FILE:hbg> --backend=vulkan
                --vfx-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_sorted_8192.vfxb
                --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_sorted_8192.vfx.json
                --no-vsync --width=64 --height=64)
    set_tests_properties(hbg_vfx_reference_sorted_8192_vulkan PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180)

    if(WIN32)
        add_test(NAME hbg_vfx_reference_sorted_262144_d3d11
            COMMAND $<TARGET_FILE:hbg> --backend=d3d11
                    --vfx-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_sorted_262144.vfxb
                    --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_sorted_262144.vfx.json
                    --no-vsync --width=64 --height=64)
        set_tests_properties(hbg_vfx_reference_sorted_262144_d3d11 PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 900)
    endif()
    add_test(NAME hbg_vfx_reference_sorted_262144_vulkan
        COMMAND $<TARGET_FILE:hbg> --backend=vulkan
                --vfx-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_sorted_262144.vfxb
                --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/reference_sorted_262144.vfx.json
                --no-vsync --width=64 --height=64)
    set_tests_properties(hbg_vfx_reference_sorted_262144_vulkan PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 900)

    function(add_vfx_event_reference name backend)
        add_test(NAME hbg_vfx_event_reference_${name}_${backend}
            COMMAND $<TARGET_FILE:hbg> --backend=${backend}
                    --vfx-event-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/${name}.vfxb
                    --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/${name}.vfx.json
                    --no-vsync --width=64 --height=64)
        set_tests_properties(hbg_vfx_event_reference_${name}_${backend}
            PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endfunction()

    function(add_vfx_cpu_event_reference backend)
        add_test(NAME hbg_vfx_cpu_event_reference_${backend}
            COMMAND $<TARGET_FILE:hbg> --backend=${backend}
                    --vfx-cpu-event-reference=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_cpu_event.vfxb
                    --vfx-reference-graph=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_cpu_event.vfx.json
                    --no-vsync --width=64 --height=64)
        set_tests_properties(hbg_vfx_cpu_event_reference_${backend}
            PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endfunction()

    if(WIN32)
        add_vfx_cpu_event_reference(d3d11)
        add_vfx_event_reference(vfx_event d3d11)
        add_vfx_event_reference(vfx_events_count d3d11)
        add_vfx_event_reference(vfx_events_multi d3d11)
        add_vfx_event_reference(vfx_events_overflow d3d11)
    endif()
    add_vfx_cpu_event_reference(vulkan)
    add_vfx_event_reference(vfx_event vulkan)
    add_vfx_event_reference(vfx_events_count vulkan)
    add_vfx_event_reference(vfx_events_multi vulkan)
    add_vfx_event_reference(vfx_events_overflow vulkan)

    function(add_vfx_perf_smoke name backend)
        add_test(NAME hbg_vfx_perf_${name}_${backend}
            COMMAND $<TARGET_FILE:hbg> --backend=${backend}
                    --render-test=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_perf_${name}.gltf
                    --vfx-test --frames=3 --width=640 --height=360 --no-vsync)
        set_tests_properties(hbg_vfx_perf_${name}_${backend} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endfunction()

    function(add_vfx_sort_outputs_smoke backend)
        add_test(NAME hbg_vfx_sort_outputs_${backend}
            COMMAND $<TARGET_FILE:hbg> --backend=${backend}
                    --render-test=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_sort_outputs.gltf
                    --vfx-test --frames=2 --width=320 --height=180 --no-vsync
                    --screenshot=${CMAKE_CURRENT_BINARY_DIR}/vfx_sort_outputs_${backend}.png)
        set_tests_properties(hbg_vfx_sort_outputs_${backend} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120
            FAIL_REGULAR_EXPRESSION "bgfx fatal;was already set")
    endfunction()

    function(add_vfx_instancing backend)
        add_test(NAME hbg_vfx_instancing_${backend}
            COMMAND ${CMAKE_COMMAND}
                    "-DHBG_EXE=$<TARGET_FILE:hbg>"
                    "-DHBG_BACKEND=${backend}"
                    "-DHBG_WORKDIR=${CMAKE_SOURCE_DIR}"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_instancing.cmake)
        set_tests_properties(hbg_vfx_instancing_${backend} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180)
    endfunction()

    function(add_vfx_instancing_cull backend)
        add_test(NAME hbg_vfx_instancing_cull_${backend}
            COMMAND ${CMAKE_COMMAND}
                    "-DHBG_EXE=$<TARGET_FILE:hbg>"
                    "-DHBG_BACKEND=${backend}"
                    "-DHBG_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
                    "-DHBG_WORKDIR=${CMAKE_SOURCE_DIR}"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_instancing_cull.cmake)
        set_tests_properties(hbg_vfx_instancing_cull_${backend} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180)
    endfunction()

    function(add_vfx_transparent_sort backend)
        add_test(NAME hbg_vfx_transparent_sort_${backend}
            COMMAND ${CMAKE_COMMAND}
                    "-DHBG_EXE=$<TARGET_FILE:hbg>"
                    "-DHBG_BACKEND=${backend}"
                    "-DHBG_GOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/vfx_transparent_sort_${backend}.png"
                    "-DHBG_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vfx_transparent_sort_${backend}.png"
                    "-DHBG_PYTHON=${HBG_PYTHON_EXECUTABLE}"
                    "-DHBG_WORKDIR=${CMAKE_SOURCE_DIR}"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_transparent_sort.cmake)
        set_tests_properties(hbg_vfx_transparent_sort_${backend} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180)
    endfunction()

    function(add_vfx_transparent_batch_sort backend)
        add_test(NAME hbg_vfx_transparent_batch_sort_${backend}
            COMMAND ${CMAKE_COMMAND}
                    "-DHBG_EXE=$<TARGET_FILE:hbg>"
                    "-DHBG_BACKEND=${backend}"
                    "-DHBG_GOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/vfx_transparent_batch_sort_${backend}.png"
                    "-DHBG_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vfx_transparent_batch_sort_${backend}.png"
                    "-DHBG_PYTHON=${HBG_PYTHON_EXECUTABLE}"
                    "-DHBG_WORKDIR=${CMAKE_SOURCE_DIR}"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_transparent_batch_sort.cmake)
        set_tests_properties(hbg_vfx_transparent_batch_sort_${backend} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180)
    endfunction()
    function(add_vfx_editor_smoke backend)
        add_test(NAME hbg_vfx_editor_${backend}
            COMMAND $<TARGET_FILE:hbg> --backend=${backend}
                    --scene=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_scene.gltf
                    --vfx-editor=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_unlit.vfx.json
                    --frames=3 --width=640 --height=360 --no-vsync)
        set_tests_properties(hbg_vfx_editor_${backend} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endfunction()

    function(add_vfx_editor_subgraph_smoke backend)
        add_test(NAME hbg_vfx_editor_subgraph_${backend}
            COMMAND $<TARGET_FILE:hbg> --backend=${backend}
                    --scene=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_scene.gltf
                    --vfx-editor=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/subgraph_visual.vfx.json
                    --frames=3 --width=640 --height=360 --no-vsync)
        set_tests_properties(hbg_vfx_editor_subgraph_${backend} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endfunction()

    function(add_vfx_editor_screenshot backend)
        add_test(NAME hbg_vfx_editor_screenshot_${backend}
            COMMAND ${CMAKE_COMMAND}
                    "-DHBG_EXE=$<TARGET_FILE:hbg>"
                    "-DHBG_BACKEND=${backend}"
                    "-DHBG_SCENE=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_scene.gltf"
                    "-DHBG_GRAPH=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/subgraph_visual.vfx.json"
                    "-DHBG_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vfx_editor_screenshot_${backend}.png"
                    "-DHBG_WORKDIR=${CMAKE_SOURCE_DIR}"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_editor_screenshot.cmake)
        set_tests_properties(hbg_vfx_editor_screenshot_${backend} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endfunction()

    function(add_vfx_shadows backend)
        set(test_name hbg_vfx_shadows_${backend})
        set(deferred_args)
        if(ARGC GREATER 1 AND ARGV1 STREQUAL "deferred")
            set(test_name hbg_vfx_shadows_deferred_${backend})
            list(APPEND deferred_args "-DHBG_DEFERRED=ON")
        endif()
        add_test(NAME ${test_name}
            COMMAND ${CMAKE_COMMAND}
                    "-DHBG_EXE=$<TARGET_FILE:hbg>"
                    "-DHBG_BACKEND=${backend}"
                    "-DHBG_SHADOW_SCENE=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_shadow.gltf"
                    "-DHBG_OFF_SCENE=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_lit.gltf"
                    "-DHBG_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
                    "-DHBG_WORKDIR=${CMAKE_SOURCE_DIR}"
                    "-DHBG_PYTHON=${HBG_PYTHON_EXECUTABLE}"
                    ${deferred_args}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_shadows.cmake)
        set_tests_properties(${test_name} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endfunction()

    function(add_vfx_motion_vectors backend)
        add_test(NAME hbg_vfx_motion_vectors_${backend}
            COMMAND ${CMAKE_COMMAND}
                    "-DHBG_EXE=$<TARGET_FILE:hbg>"
                    "-DHBG_BACKEND=${backend}"
                    "-DHBG_SCENE=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_forces.gltf"
                    "-DHBG_STATIC_SCENE=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_sort.gltf"
                    "-DHBG_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
                    "-DHBG_WORKDIR=${CMAKE_SOURCE_DIR}"
                    "-DHBG_PYTHON=${HBG_PYTHON_EXECUTABLE}"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_motion_vectors.cmake)
        set_tests_properties(hbg_vfx_motion_vectors_${backend} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endfunction()

    function(add_vfx_motion_gate backend)
        add_test(NAME hbg_vfx_motion_gate_${backend}
            COMMAND ${CMAKE_COMMAND}
                    "-DHBG_EXE=$<TARGET_FILE:hbg>"
                    "-DHBG_BACKEND=${backend}"
                    "-DHBG_SCENE=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_shadow.gltf"
                    "-DHBG_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
                    "-DHBG_WORKDIR=${CMAKE_SOURCE_DIR}"
                    "-DHBG_PYTHON=${HBG_PYTHON_EXECUTABLE}"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_motion_gate.cmake)
        set_tests_properties(hbg_vfx_motion_gate_${backend} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endfunction()

    if(WIN32 AND HBG_SHADER_COMPILER AND HBG_BGFX_SOURCE_DIR)
        add_test(NAME hbg_vfx_editor_compile
            COMMAND ${CMAKE_COMMAND}
                    -DHBG_EXECUTABLE=$<TARGET_FILE:hbg>
                    -DHBG_SCENE=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_scene.gltf
                    -DHBG_GRAPH=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_unlit.vfx.json
                    -DHBG_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/editor_compile_test
                    -DHBG_SOURCE_DIR=${CMAKE_SOURCE_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_editor_compile.cmake)
        set_tests_properties(hbg_vfx_editor_compile PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 900)
    endif()

    function(add_vfx_feature_golden name backend scene frames)
        set(tolerance_args)
        if(ARGC GREATER 4)
            list(APPEND tolerance_args -DHBG_MAX_DIFF_PIXELS=4 -DHBG_MAX_CHANNEL_DELTA=1)
        endif()
        add_test(NAME hbg_vfx_golden_${name}_${backend}
            COMMAND ${CMAKE_COMMAND}
                    -DHBG_EXE=$<TARGET_FILE:hbg>
                    -DHBG_BACKEND=${backend}
                    -DHBG_SCENE=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/${scene}
                    -DHBG_GOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/vfx_${name}_${backend}.png
                    -DHBG_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vfx_${name}_${backend}.png
                    -DHBG_FRAMES=${frames}
                    -DHBG_PYTHON=${HBG_PYTHON_EXECUTABLE}
                    -DHBG_WORKDIR=${CMAKE_SOURCE_DIR}
                    ${tolerance_args}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_golden.cmake)
        set_tests_properties(hbg_vfx_golden_${name}_${backend} PROPERTIES TIMEOUT 180)
    endfunction()

    function(add_vfx_deferred_golden name backend scene frames)
        add_test(NAME hbg_vfx_golden_${name}_deferred_${backend}
            COMMAND ${CMAKE_COMMAND}
                    -DHBG_EXE=$<TARGET_FILE:hbg>
                    -DHBG_BACKEND=${backend}
                    -DHBG_SCENE=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/${scene}
                    -DHBG_GOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/vfx_${name}_deferred_${backend}.png
                    -DHBG_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vfx_${name}_deferred_${backend}.png
                    -DHBG_FRAMES=${frames}
                    -DHBG_PIPELINE_SETTINGS=${CMAKE_SOURCE_DIR}/config/pipeline.json
                    -DHBG_DEFERRED=ON
                    -DHBG_PYTHON=${HBG_PYTHON_EXECUTABLE}
                    -DHBG_MAX_DIFF_PIXELS=4
                    -DHBG_MAX_CHANNEL_DELTA=1
                    -DHBG_WORKDIR=${CMAKE_SOURCE_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_golden.cmake)
        set_tests_properties(hbg_vfx_golden_${name}_deferred_${backend} PROPERTIES TIMEOUT 180)
    endfunction()

    function(add_vfx_determinism name backend scene frames)
        add_test(NAME hbg_vfx_determinism_${name}_${backend}
            COMMAND ${CMAKE_COMMAND}
                    -DHBG_EXE=$<TARGET_FILE:hbg>
                    -DHBG_BACKEND=${backend}
                    -DHBG_SCENE=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/${scene}
                    -DHBG_NAME=${name}
                    -DHBG_FRAMES=${frames}
                    -DHBG_WORKDIR=${CMAKE_SOURCE_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_determinism.cmake)
        set_tests_properties(hbg_vfx_determinism_${name}_${backend} PROPERTIES TIMEOUT 180)
    endfunction()

    function(add_vfx_empty_test backend)
        add_test(NAME hbg_vfx_empty_${backend}
            COMMAND ${CMAKE_COMMAND}
                    -DHBG_EXE=$<TARGET_FILE:hbg>
                    -DHBG_BACKEND=${backend}
                    -DHBG_WORKDIR=${CMAKE_SOURCE_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_empty.cmake)
        set_tests_properties(hbg_vfx_empty_${backend} PROPERTIES TIMEOUT 120)
    endfunction()

    if(WIN32)
        add_vfx_feature_golden(bursts d3d11 vfx_bursts.gltf 90)
        add_vfx_feature_golden(forces d3d11 vfx_forces.gltf 30)
        add_vfx_feature_golden(collision d3d11 vfx_collision.gltf 30)
        add_vfx_feature_golden(curves d3d11 vfx_curves.gltf 300)
        add_vfx_feature_golden(sort d3d11 vfx_sort.gltf 10)
        add_vfx_feature_golden(events d3d11 vfx_events.gltf 30)
        add_vfx_feature_golden(subgraph d3d11 vfx_subgraph.gltf 30)
        add_vfx_feature_golden(custom_hlsl d3d11 vfx_custom_hlsl.gltf 30)
        add_vfx_feature_golden(lit d3d11 vfx_lit.gltf 30 tolerant)
        add_vfx_deferred_golden(lit d3d11 vfx_lit.gltf 30)
        add_vfx_feature_golden(order d3d11 vfx_order.gltf 30 tolerant)
        add_vfx_deferred_golden(order d3d11 vfx_order.gltf 30)
        add_vfx_feature_golden(order_off d3d11 vfx_order_off.gltf 30)
        add_test(NAME hbg_vfx_order_off_d3d11
            COMMAND ${CMAKE_COMMAND}
                    -DHBG_EXE=$<TARGET_FILE:hbg>
                    -DHBG_BACKEND=d3d11
                    -DHBG_WORKDIR=${CMAKE_SOURCE_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_order_off.cmake)
        set_tests_properties(hbg_vfx_order_off_d3d11 PROPERTIES TIMEOUT 120)
        add_vfx_empty_test(d3d11)
        add_test(NAME hbg_vfx_resize_d3d11
            COMMAND $<TARGET_FILE:hbg> --backend=d3d11
                    --render-test=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_order.gltf
                    --vfx-test --frames=4 --width=640 --height=360 --no-vsync --resize-test)
        set_tests_properties(hbg_vfx_resize_d3d11 PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
        add_vfx_feature_golden(soft d3d11 vfx_soft.gltf 30)
        add_vfx_feature_golden(soft_off d3d11 vfx_soft_off.gltf 30)
        add_vfx_perf_smoke(10k d3d11)
        add_vfx_perf_smoke(100k d3d11)
        add_vfx_perf_smoke(1m d3d11)
        add_vfx_editor_smoke(d3d11)
        add_vfx_editor_subgraph_smoke(d3d11)
        add_vfx_editor_screenshot(d3d11)
        add_vfx_shadows(d3d11)
        add_vfx_shadows(d3d11 deferred)
        add_vfx_motion_vectors(d3d11)
        add_vfx_motion_gate(d3d11)
        add_vfx_sort_outputs_smoke(d3d11)
        add_vfx_instancing(d3d11)
        add_vfx_instancing_cull(d3d11)
        add_vfx_transparent_sort(d3d11)
        add_vfx_transparent_batch_sort(d3d11)
        add_vfx_determinism(sort d3d11 vfx_sort.gltf 10)
        add_vfx_determinism(events d3d11 vfx_events.gltf 30)
    endif()
    add_vfx_feature_golden(bursts vulkan vfx_bursts.gltf 90)
    add_vfx_feature_golden(forces vulkan vfx_forces.gltf 30)
    add_vfx_feature_golden(collision vulkan vfx_collision.gltf 30)
    add_vfx_feature_golden(curves vulkan vfx_curves.gltf 300)
    add_vfx_feature_golden(sort vulkan vfx_sort.gltf 10)
    add_vfx_feature_golden(events vulkan vfx_events.gltf 30)
    add_vfx_feature_golden(subgraph vulkan vfx_subgraph.gltf 30)
    add_vfx_feature_golden(custom_hlsl vulkan vfx_custom_hlsl.gltf 30)
    add_vfx_feature_golden(lit vulkan vfx_lit.gltf 30 tolerant)
    add_vfx_deferred_golden(lit vulkan vfx_lit.gltf 30)
    add_vfx_feature_golden(order vulkan vfx_order.gltf 30 tolerant)
    add_vfx_deferred_golden(order vulkan vfx_order.gltf 30)
    add_vfx_feature_golden(order_off vulkan vfx_order_off.gltf 30)
    add_test(NAME hbg_vfx_order_off_vulkan
        COMMAND ${CMAKE_COMMAND}
                -DHBG_EXE=$<TARGET_FILE:hbg>
                -DHBG_BACKEND=vulkan
                -DHBG_WORKDIR=${CMAKE_SOURCE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_order_off.cmake)
    set_tests_properties(hbg_vfx_order_off_vulkan PROPERTIES TIMEOUT 120)
    add_vfx_empty_test(vulkan)
    add_test(NAME hbg_vfx_resize_vulkan
        COMMAND $<TARGET_FILE:hbg> --backend=vulkan
                --render-test=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_order.gltf
                --vfx-test --frames=4 --width=640 --height=360 --no-vsync --resize-test)
    set_tests_properties(hbg_vfx_resize_vulkan PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    add_vfx_feature_golden(soft vulkan vfx_soft.gltf 30)
    add_vfx_feature_golden(soft_off vulkan vfx_soft_off.gltf 30)
    add_vfx_perf_smoke(10k vulkan)
    add_vfx_perf_smoke(100k vulkan)
    add_vfx_perf_smoke(1m vulkan)
    add_vfx_editor_smoke(vulkan)
    add_vfx_editor_subgraph_smoke(vulkan)
    add_vfx_editor_screenshot(vulkan)
    add_vfx_shadows(vulkan)
    add_vfx_shadows(vulkan deferred)
    add_vfx_motion_vectors(vulkan)
    add_vfx_motion_gate(vulkan)
    add_vfx_sort_outputs_smoke(vulkan)
    add_vfx_instancing(vulkan)
    add_vfx_instancing_cull(vulkan)
    add_vfx_transparent_sort(vulkan)
    add_vfx_transparent_batch_sort(vulkan)
    add_vfx_determinism(sort vulkan vfx_sort.gltf 10)
    add_vfx_determinism(events vulkan vfx_events.gltf 30)

    if(WIN32)
        add_test(NAME hbg_vfx_sort_on_off_d3d11
            COMMAND ${CMAKE_COMMAND}
                    -DHBG_EXE=$<TARGET_FILE:hbg>
                    -DHBG_BACKEND=d3d11
                    -DHBG_WORKDIR=${CMAKE_SOURCE_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_sort_compare.cmake)
        set_tests_properties(hbg_vfx_sort_on_off_d3d11 PROPERTIES TIMEOUT 120)

        add_test(NAME hbg_vfx_golden_d3d11
            COMMAND ${CMAKE_COMMAND}
                    -DHBG_EXE=$<TARGET_FILE:hbg>
                    -DHBG_BACKEND=d3d11
                    -DHBG_SCENE=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_scene.gltf
                    -DHBG_GOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/vfx_scene.png
                    -DHBG_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vfx_scene_d3d11.png
                    -DHBG_WORKDIR=${CMAKE_SOURCE_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_golden.cmake)
        set_tests_properties(hbg_vfx_golden_d3d11 PROPERTIES TIMEOUT 120)
    endif()
    add_test(NAME hbg_vfx_golden_vulkan
        COMMAND ${CMAKE_COMMAND}
                -DHBG_EXE=$<TARGET_FILE:hbg>
                -DHBG_BACKEND=vulkan
                -DHBG_SCENE=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/vfx_scene.gltf
                -DHBG_GOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/vfx_scene.png
                -DHBG_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/vfx_scene_vulkan.png
                -DHBG_WORKDIR=${CMAKE_SOURCE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_vfx_golden.cmake)
    set_tests_properties(hbg_vfx_golden_vulkan PROPERTIES TIMEOUT 120)

    function(hbg_vfx_label_tests label regex)
        get_property(vfx_tests DIRECTORY PROPERTY TESTS)
        foreach(vfx_test IN LISTS vfx_tests)
            if(vfx_test MATCHES "${regex}")
                set_property(TEST "${vfx_test}" APPEND PROPERTY LABELS "${label}")
            endif()
        endforeach()
    endfunction()

    hbg_vfx_label_tests(vfx "^hbg_vfx")
    hbg_vfx_label_tests(vfx_unit "^(hbg_vfx_tests|hbg_vfx_runtime_tests|hbg_vfx_compiler_tests|hbg_vfx_adapter_tests)$")
    hbg_vfx_label_tests(vfx_asset_unit "^hbg_vfx_tests$")
    hbg_vfx_label_tests(vfx_runtime "^hbg_vfx_runtime_tests$")
    hbg_vfx_label_tests(vfx_compiler "^hbg_vfx_compiler_tests$")
    hbg_vfx_label_tests(vfx_adapter "^hbg_vfx_adapter_tests$")
    hbg_vfx_label_tests(vfx_slow "^(hbg_vfx_asset_check|hbg_vfx_editor_roundtrip|hbg_vfx_perf_.*|hbg_vfx_reference_sorted_262144_.*)$")
    hbg_vfx_label_tests(vfx_instancing "^hbg_vfx_instancing")
    hbg_vfx_label_tests(vfx_bounds "auto_bounds")
    hbg_vfx_label_tests(vfx_motion "^hbg_vfx_motion")
    hbg_vfx_label_tests(vfx_shadow "^hbg_vfx_shadows")
    hbg_vfx_label_tests(vfx_sort "^hbg_vfx_(sort|determinism|transparent)")
    hbg_vfx_label_tests(vfx_reference "reference")
    hbg_vfx_label_tests(vfx_golden "golden")
    hbg_vfx_label_tests(vfx_editor "^hbg_vfx_editor")
endif()
