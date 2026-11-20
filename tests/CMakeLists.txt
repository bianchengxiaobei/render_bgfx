add_executable(hbg_tests
    test_main.cpp
    app/test_backend.cpp
    asset/test_gltf.cpp
    core/test_handles.cpp
    core/test_math.cpp
    gfx/test_gfx_types.cpp
    gfx/test_uniform_layout.cpp
    render/test_contact_shadow.cpp
    render/test_cookie_atlas.cpp
    render/test_debug_display.cpp
    render/test_decal.cpp
    render/test_frame_settings.cpp
    render/test_draw_list.cpp
    render/test_environment.cpp
    render/test_light_shadow.cpp
    render/test_lighting.cpp
    render/test_light_list.cpp
    render/test_lightmap.cpp
    render/test_local_fog_volume.cpp
    render/test_post_process.cpp
    render/test_reflection_probe.cpp
    render/test_prepass.cpp
    render/test_render_graph.cpp
    render/test_shadow.cpp
    render/test_shadow_settings.cpp
    render/test_ssao.cpp
    render/test_ssr.cpp
    render/test_ssgi.cpp
    render/test_view_state.cpp
    render/test_volumetric_fog.cpp
)

target_link_libraries(hbg_tests PRIVATE hbg_app bimg_encode doctest::doctest)
target_compile_definitions(hbg_tests PRIVATE HBG_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
hbg_target_options(hbg_tests)

add_test(NAME hbg_tests COMMAND hbg_tests)

if(TARGET hbg_dawnbake)
    add_executable(hbg_fake_dawn dawn/fake_dawn.cpp)
    hbg_target_options(hbg_fake_dawn)
    add_test(NAME dawnbake_result_poll
        COMMAND hbg_dawnbake --result-test $<TARGET_FILE:hbg_fake_dawn>)
    add_test(NAME dawnbake_job_lifecycle COMMAND hbg_dawnbake --job-lifecycle-test)
    add_test(NAME dawnbake_proto_sizes COMMAND hbg_dawnbake --proto-test)
    add_test(NAME dawnbake_mesh_export COMMAND hbg_dawnbake --mesh-test)
    add_test(NAME dawnbake_material_export COMMAND hbg_dawnbake --material-test)
    add_test(NAME dawnbake_light_export COMMAND hbg_dawnbake --light-test)
    add_test(NAME dawnbake_sky_export COMMAND hbg_dawnbake --sky-test)
    add_test(NAME dawnbake_uv_unwrap COMMAND hbg_dawnbake --uv-test)
    add_test(NAME dawnbake_atlas_pack COMMAND hbg_dawnbake --atlas-test)
    add_test(NAME dawnbake_uv_dump COMMAND hbg_dawnbake --dump-uv=${CMAKE_CURRENT_BINARY_DIR}/dawn_uv_debug.png)
    add_test(NAME dawnbake_lmgz_reader COMMAND hbg_dawnbake --lmgz-test)
    add_test(NAME dawnbake_lmgz_binding COMMAND hbg_dawnbake --lmgz-bind-test)
    add_test(NAME dawnbake_sh_atlas COMMAND hbg_dawnbake --sh-atlas-test)
    add_test(NAME dawnbake_sh_atlas_dds COMMAND hbg_dawnbake --sh-atlas-dds-test)
    add_test(NAME dawnbake_sh_atlas_dds_verify COMMAND hbg_dawnbake --sh-atlas-dds-verify-test)
    add_test(NAME dawnbake_sidecar_merge COMMAND hbg_dawnbake --sidecar-merge-test)
    add_test(NAME dawnbake_package_export COMMAND hbg_dawnbake --package-test)
    add_test(NAME dawnbake_process_driver COMMAND hbg_dawnbake --process-test)
    add_test(NAME dawnbake_golden_structure
        COMMAND hbg_dawnbake --golden-test
                ${CMAKE_SOURCE_DIR}/tests/dawn/golden/min_scene)
    add_test(NAME dawnbake_golden_roundtrip
        COMMAND hbg_dawnbake --roundtrip
                ${CMAKE_SOURCE_DIR}/tests/dawn/golden/min_scene/11111111222222223333333344444444.scene
                ${CMAKE_SOURCE_DIR}/tests/dawn/golden/min_scene/11111111222222223333333344444444.jobs)
endif()

if(HBG_IDL_PYTHON_AVAILABLE)
    add_test(NAME hbg_idl_check
        COMMAND ${HBG_PYTHON_EXECUTABLE} ${HBG_PYTHON_ARGS}
                ${CMAKE_SOURCE_DIR}/tools/idl_gen.py
                --root ${CMAKE_SOURCE_DIR}
                --check)
endif()

