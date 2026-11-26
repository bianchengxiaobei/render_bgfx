if(NOT DEFINED HBG_EXE OR NOT EXISTS "${HBG_EXE}")
    message(FATAL_ERROR "hbg_vfx_motion_gate: HBG_EXE is missing: ${HBG_EXE}")
endif()
if(NOT DEFINED HBG_BACKEND OR NOT DEFINED HBG_SCENE OR NOT DEFINED HBG_OUTPUT_DIR OR
   NOT DEFINED HBG_WORKDIR OR NOT DEFINED HBG_PYTHON)
    message(FATAL_ERROR "hbg_vfx_motion_gate: missing required variables")
endif()

file(MAKE_DIRECTORY "${HBG_OUTPUT_DIR}")
set(on_output "${HBG_OUTPUT_DIR}/vfx_motion_gate_on_${HBG_BACKEND}.png")
set(off_output "${HBG_OUTPUT_DIR}/vfx_motion_gate_off_${HBG_BACKEND}.png")
file(REMOVE "${on_output}" "${off_output}")

set(render_args
    "--backend=${HBG_BACKEND}"
    "--render-test=${HBG_SCENE}"
    "--debug-view=motion-vectors"
    "--frames=5"
    "--width=640"
    "--height=360"
    "--no-vsync")
execute_process(
    COMMAND "${HBG_EXE}" ${render_args} "--vfx-test" "--screenshot=${on_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE on_result
    OUTPUT_VARIABLE on_log
    ERROR_VARIABLE on_error)
if(NOT on_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_motion_gate: VFX render failed (${on_result})\n${on_log}\n${on_error}")
endif()
execute_process(
    COMMAND "${HBG_EXE}" ${render_args} "--no-vfx" "--screenshot=${off_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE off_result
    OUTPUT_VARIABLE off_log
    ERROR_VARIABLE off_error)
if(NOT off_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_motion_gate: no-VFX render failed (${off_result})\n${off_log}\n${off_error}")
endif()
if(NOT EXISTS "${on_output}" OR NOT EXISTS "${off_output}")
    message(FATAL_ERROR "hbg_vfx_motion_gate: screenshot was not written")
endif()
string(FIND "${on_log}" "vfx_shadow_draws=4" on_shadow_draws)
if(on_shadow_draws EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_motion_gate: expected the shadow fixture to be active\n${on_log}")
endif()

execute_process(
    COMMAND "${HBG_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/compare_png.py"
            "${off_output}" "${on_output}" 0 0 0
    RESULT_VARIABLE compare_result
    OUTPUT_VARIABLE compare_output)
if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_motion_gate: gated VFX output wrote motion vectors\n${compare_output}")
endif()
message(STATUS "VFX motion gate: ${compare_output}")
