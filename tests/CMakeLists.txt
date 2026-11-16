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

target_link_libraries(hbg_tests PRIVATE hbg_app doctest::doctest)
target_compile_definitions(hbg_tests PRIVATE HBG_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
hbg_target_options(hbg_tests)

add_test(NAME hbg_tests COMMAND hbg_tests)

if(HBG_IDL_PYTHON_AVAILABLE)
    add_test(NAME hbg_idl_check
        COMMAND ${HBG_PYTHON_EXECUTABLE} ${HBG_PYTHON_ARGS}
                ${CMAKE_SOURCE_DIR}/tools/idl_gen.py
                --root ${CMAKE_SOURCE_DIR}
                --check)
endif()

