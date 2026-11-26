add_library(hbg_sg STATIC
    src/core/types.cpp
    src/core/graph.cpp
    src/core/registry.cpp
    src/core/concretize.cpp
    src/core/validate.cpp
    src/core/serialize.cpp
    src/core/migrate.cpp
    src/core/subgraph.cpp
    src/nodes/node_library.cpp
    src/compile/generator.cpp
    src/compile/targets.cpp
    src/compile/pass_plan.cpp
    src/compile/properties.cpp
    src/compile/keywords.cpp
    src/compile/function_registry.cpp
    src/compile/emit.cpp
    src/compile/emit_vertex.cpp
    src/compile/shaderc.cpp
    src/compile/pack.cpp
    src/compile/compile.cpp
    src/editor/commands.cpp
    src/editor/document.cpp
    src/editor/layout.cpp
)

target_include_directories(hbg_sg
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(hbg_sg PRIVATE ${HBG_NLOHMANN_JSON_TARGET})
hbg_target_options(hbg_sg)

add_executable(sgbake
    tools/sgbake.cpp
)
target_link_libraries(sgbake PRIVATE hbg_sg)
target_compile_definitions(sgbake PRIVATE
    SG_ROOT="${CMAKE_SOURCE_DIR}"
    SG_SHADERC_DEFAULT="${HBG_SHADER_COMPILER}"
    SG_BGFX_SOURCE_DEFAULT="${HBG_BGFX_SOURCE_DIR}"
)
hbg_target_options(sgbake)
set_target_properties(sgbake PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>
)

if(HBG_BUILD_TESTS)
    add_executable(hbg_sg_tests
        tests/core/test_main.cpp
        tests/core/test_model.cpp
        tests/core/test_validate_serialize.cpp
        tests/core/test_subgraph.cpp
        tests/compiler/test_compiler.cpp
        tests/compiler/test_math_nodes.cpp
        tests/compiler/test_core_math_logic_nodes.cpp
        tests/compiler/test_input_basic_nodes.cpp
        tests/compiler/test_input_property_node.cpp
        tests/compiler/test_keyword_node.cpp
        tests/compiler/test_dropdown_node.cpp
        tests/compiler/test_utility_switch_preview_nodes.cpp
        tests/compiler/test_utility_subgraph_node.cpp
        tests/compiler/test_utility_custom_function_node.cpp
        tests/compiler/test_input_geometry_nodes.cpp
        tests/compiler/test_input_texture_nodes.cpp
        tests/compiler/test_input_matrix_pbr_nodes.cpp
        tests/compiler/test_input_scene_lighting_nodes.cpp
        tests/compiler/test_input_gradient_nodes.cpp
        tests/compiler/test_uv_nodes.cpp
        tests/compiler/test_uv_parallax_triplanar_nodes.cpp
        tests/compiler/test_procedural_nodes.cpp
        tests/compiler/test_procedural_noise_nodes.cpp
        tests/compiler/test_artistic_nodes.cpp
        tests/compiler/test_artistic_normal_nodes.cpp
        tests/compiler/test_artistic_filter_mask_nodes.cpp
        tests/compiler/test_ui_target_nodes.cpp
        tests/editor/test_document.cpp
    )
    target_link_libraries(hbg_sg_tests PRIVATE hbg_sg doctest::doctest)
    target_include_directories(hbg_sg_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(hbg_sg_tests PRIVATE
        SG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        SG_ROOT="${CMAKE_SOURCE_DIR}"
        SG_SHADERC_DEFAULT="${HBG_SHADER_COMPILER}"
        SG_BGFX_SOURCE_DEFAULT="${HBG_BGFX_SOURCE_DIR}"
    )
    hbg_target_options(hbg_sg_tests)
    add_test(NAME hbg_sg_core COMMAND hbg_sg_tests)
    add_test(NAME hbg_sg_compiler COMMAND hbg_sg_tests)
endif()
