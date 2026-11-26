if(NOT DEFINED HBG_EXE OR NOT EXISTS "${HBG_EXE}")
    message(FATAL_ERROR "hbg_vfx_shadows: HBG_EXE is missing: ${HBG_EXE}")
endif()
if(NOT DEFINED HBG_BACKEND OR NOT DEFINED HBG_SHADOW_SCENE OR NOT DEFINED HBG_OFF_SCENE OR
   NOT DEFINED HBG_OUTPUT_DIR OR NOT DEFINED HBG_WORKDIR OR NOT DEFINED HBG_PYTHON)
    message(FATAL_ERROR "hbg_vfx_shadows: missing required variables")
endif()

if(DEFINED HBG_DEFERRED AND HBG_DEFERRED)
    set(mode_suffix "_deferred")
else()
    set(mode_suffix "")
endif()

file(MAKE_DIRECTORY "${HBG_OUTPUT_DIR}")
set(on_output "${HBG_OUTPUT_DIR}/vfx_shadow_on_${HBG_BACKEND}${mode_suffix}.png")
set(off_output "${HBG_OUTPUT_DIR}/vfx_shadow_off_${HBG_BACKEND}${mode_suffix}.png")
file(REMOVE "${on_output}" "${off_output}")

set(render_args
    "--backend=${HBG_BACKEND}"
    "--debug-view=directional-shadow-atlas"
    "--frames=30"
    "--width=640"
    "--height=360"
    "--no-vsync")
if(DEFINED HBG_DEFERRED AND HBG_DEFERRED)
    list(APPEND render_args "--deferred")
endif()
execute_process(
    COMMAND "${HBG_EXE}" ${render_args} "--render-test=${HBG_SHADOW_SCENE}" "--vfx-test"
            "--screenshot=${on_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE on_result
    OUTPUT_VARIABLE on_log
    ERROR_VARIABLE on_error)
if(NOT on_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_shadows: shadow render failed (${on_result})\n${on_log}\n${on_error}")
endif()
execute_process(
    COMMAND "${HBG_EXE}" ${render_args} "--render-test=${HBG_OFF_SCENE}" "--vfx-test"
            "--screenshot=${off_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE off_result
    OUTPUT_VARIABLE off_log
    ERROR_VARIABLE off_error)
if(NOT off_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_shadows: non-shadow render failed (${off_result})\n${off_log}\n${off_error}")
endif()
if(NOT EXISTS "${on_output}" OR NOT EXISTS "${off_output}")
    message(FATAL_ERROR "hbg_vfx_shadows: screenshot was not written")
endif()

string(FIND "${on_log}" "vfx_shadow_draws=4" on_draws)
if(on_draws EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_shadows: expected four VFX shadow draws\n${on_log}")
endif()
string(FIND "${off_log}" "vfx_shadow_draws=0" off_draws)
if(off_draws EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_shadows: expected no VFX shadow draws for the off asset\n${off_log}")
endif()

execute_process(
    COMMAND "${HBG_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/compare_png.py"
            "${off_output}" "${on_output}" 1000000 0 100
    RESULT_VARIABLE compare_result
    OUTPUT_VARIABLE compare_output)
if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_shadows: particle shadows did not change the shadow atlas\n${compare_output}")
endif()
message(STATUS "VFX shadows: ${compare_output}")
