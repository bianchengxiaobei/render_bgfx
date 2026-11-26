add_library(hbg_analysis_core STATIC
    src/compile_command.cpp
    src/compiler.cpp
    src/diff.cpp
    src/dxbc_parser.cpp
    src/export_csv.cpp
    src/filters.cpp
    src/hashing.cpp
    src/liveness.cpp
    src/process_runner.cpp
    src/reference.cpp
    src/report.cpp
    src/report_files.cpp
    src/spirv_blob.cpp
    src/spirv_parser.cpp
    src/variant_manifest.cpp
)
target_include_directories(hbg_analysis_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(hbg_analysis_core PRIVATE ${HBG_NLOHMANN_JSON_TARGET})
hbg_target_options(hbg_analysis_core)

add_executable(hbg_shader_analysis
    main.cpp
)
target_link_libraries(hbg_shader_analysis PRIVATE
    hbg_analysis_core
    ${HBG_NLOHMANN_JSON_TARGET}
)
target_compile_definitions(hbg_shader_analysis PRIVATE
    HBG_ANALYSIS_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    HBG_ANALYSIS_VERSION="${PROJECT_VERSION}"
)
if(HBG_SHADER_COMPILER)
    target_compile_definitions(hbg_shader_analysis PRIVATE
        HBG_ANALYSIS_SHADERC_DEFAULT="${HBG_SHADER_COMPILER}"
    )
endif()
if(HBG_BGFX_SOURCE_DIR)
    target_compile_definitions(hbg_shader_analysis PRIVATE
        HBG_ANALYSIS_BGFX_SRC_DEFAULT="${HBG_BGFX_SOURCE_DIR}"
    )
endif()
hbg_target_options(hbg_shader_analysis)
set_target_properties(hbg_shader_analysis PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

if(HBG_BUILD_TESTS)
    add_executable(hbg_shader_analysis_fake_compiler
        tests/fake_compiler.cpp
    )
    hbg_target_options(hbg_shader_analysis_fake_compiler)
    set_target_properties(hbg_shader_analysis_fake_compiler PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    )

    add_executable(hbg_shader_analysis_tests
        tests/test_main.cpp
        tests/test_compiler.cpp
        tests/test_csv.cpp
        tests/test_diff.cpp
        tests/test_dxbc_liveness.cpp
        tests/test_dxbc_parser.cpp
        tests/test_filters.cpp
        tests/test_hashing.cpp
        tests/test_process_runner.cpp
        tests/test_reference.cpp
        tests/test_report_json.cpp
        tests/test_spirv_blob.cpp
        tests/test_spirv_parser.cpp
        tests/test_variant_manifest.cpp
    )
    add_dependencies(hbg_shader_analysis_tests hbg_shader_analysis_fake_compiler)
    target_link_libraries(hbg_shader_analysis_tests PRIVATE
        hbg_analysis_core
        doctest::doctest
        ${HBG_NLOHMANN_JSON_TARGET}
    )
    target_compile_definitions(hbg_shader_analysis_tests PRIVATE
        HBG_ANALYSIS_TEST_ROOT="${CMAKE_SOURCE_DIR}"
        HBG_ANALYSIS_FAKE_COMPILER="$<TARGET_FILE:hbg_shader_analysis_fake_compiler>"
    )
    hbg_target_options(hbg_shader_analysis_tests)
    add_test(NAME shader_analysis_tests COMMAND hbg_shader_analysis_tests)
    add_test(NAME shader_analysis_self_test
        COMMAND $<TARGET_FILE:hbg_shader_analysis> --self-test)
    set_tests_properties(shader_analysis_self_test PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )

    if(HBG_SHADER_COMPILER AND HBG_BGFX_SOURCE_DIR)
        add_test(NAME shader_analysis_dry_run
            COMMAND $<TARGET_FILE:hbg_shader_analysis>
                    build --dry-run
                    --root ${CMAKE_SOURCE_DIR}
                    --variants ${CMAKE_SOURCE_DIR}/shaders/variants.json
                    --backend dxbc
                    --pass forward)
        set_tests_properties(shader_analysis_dry_run PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
    endif()
endif()
