add_library(hbg_vfx STATIC
    src/version.cpp
    src/core/graph.cpp
    src/core/asset_json.cpp
    src/compiler/attribute_layout.cpp
    src/compiler/expression.cpp
    src/reference/cpu_reference.cpp
)
target_include_directories(hbg_vfx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(hbg_vfx PRIVATE ${HBG_NLOHMANN_JSON_TARGET})
hbg_target_options(hbg_vfx)

add_library(hbg_vfx_compiler STATIC
    src/compiler/codegen.cpp
    src/compiler/binary.cpp
    src/compiler/compiler.cpp
)
target_link_libraries(hbg_vfx_compiler PUBLIC hbg_vfx)
hbg_target_options(hbg_vfx_compiler)

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
        tests/test_vfx_attribute_layout.cpp
        tests/test_vfx_expression.cpp
        tests/test_vfx_backend.cpp
        tests/test_vfx_reference.cpp
    )
    target_link_libraries(hbg_vfx_tests PRIVATE hbg_vfx doctest::doctest)
    target_compile_definitions(hbg_vfx_tests PRIVATE VFX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    hbg_target_options(hbg_vfx_tests)
    add_test(NAME hbg_vfx_tests COMMAND hbg_vfx_tests)

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
    add_test(NAME hbg_vfx_compiler_tests COMMAND hbg_vfx_compiler_tests)

    add_executable(hbg_vfx_runtime_tests
        tests/test_vfx_runtime.cpp
    )
    target_link_libraries(hbg_vfx_runtime_tests PRIVATE hbg_vfx_runtime doctest::doctest)
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
endif()
