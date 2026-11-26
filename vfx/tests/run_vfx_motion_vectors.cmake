if(NOT DEFINED HBG_EXE OR NOT EXISTS "${HBG_EXE}")
    message(FATAL_ERROR "hbg_vfx_motion_vectors: HBG_EXE is missing: ${HBG_EXE}")
endif()
if(NOT DEFINED HBG_BACKEND OR NOT DEFINED HBG_SCENE OR NOT DEFINED HBG_STATIC_SCENE OR
   NOT DEFINED HBG_OUTPUT_DIR OR NOT DEFINED HBG_WORKDIR OR NOT DEFINED HBG_PYTHON)
    message(FATAL_ERROR "hbg_vfx_motion_vectors: missing required variables")
endif()

file(MAKE_DIRECTORY "${HBG_OUTPUT_DIR}")
set(on_output "${HBG_OUTPUT_DIR}/vfx_motion_vectors_on_${HBG_BACKEND}.png")
set(off_output "${HBG_OUTPUT_DIR}/vfx_motion_vectors_off_${HBG_BACKEND}.png")
set(static_on_output "${HBG_OUTPUT_DIR}/vfx_motion_vectors_static_on_${HBG_BACKEND}.png")
set(static_off_output "${HBG_OUTPUT_DIR}/vfx_motion_vectors_static_off_${HBG_BACKEND}.png")
file(REMOVE "${on_output}" "${off_output}" "${static_on_output}" "${static_off_output}")

set(render_args
    "--backend=${HBG_BACKEND}"
    "--render-test=${HBG_SCENE}"
    "--debug-view=motion-vectors"
    "--frames=30"
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
    message(FATAL_ERROR "hbg_vfx_motion_vectors: VFX render failed (${on_result})\n${on_log}\n${on_error}")
endif()
execute_process(
    COMMAND "${HBG_EXE}" ${render_args} "--no-vfx" "--screenshot=${off_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE off_result
    OUTPUT_VARIABLE off_log
    ERROR_VARIABLE off_error)
if(NOT off_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_motion_vectors: no-VFX render failed (${off_result})\n${off_log}\n${off_error}")
endif()
if(NOT EXISTS "${on_output}" OR NOT EXISTS "${off_output}")
    message(FATAL_ERROR "hbg_vfx_motion_vectors: screenshot was not written")
endif()

execute_process(
    COMMAND "${HBG_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/compare_png.py"
            "${off_output}" "${on_output}" 1000000 0 100
    RESULT_VARIABLE compare_result
    OUTPUT_VARIABLE compare_output)
if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_motion_vectors: VFX motion vectors did not change the debug image\n${compare_output}")
endif()

set(static_render_args
    "--backend=${HBG_BACKEND}"
    "--render-test=${HBG_STATIC_SCENE}"
    "--debug-view=motion-vectors"
    "--frames=5"
    "--width=640"
    "--height=360"
    "--no-vsync")
execute_process(
    COMMAND "${HBG_EXE}" ${static_render_args} "--vfx-test" "--screenshot=${static_on_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE static_on_result
    OUTPUT_VARIABLE static_on_log
    ERROR_VARIABLE static_on_error)
if(NOT static_on_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_motion_vectors: static VFX render failed (${static_on_result})\n${static_on_log}\n${static_on_error}")
endif()
execute_process(
    COMMAND "${HBG_EXE}" ${static_render_args} "--no-vfx" "--screenshot=${static_off_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE static_off_result
    OUTPUT_VARIABLE static_off_log
    ERROR_VARIABLE static_off_error)
if(NOT static_off_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_motion_vectors: static no-VFX render failed (${static_off_result})\n${static_off_log}\n${static_off_error}")
endif()
execute_process(
    COMMAND "${HBG_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/compare_png.py"
            "${static_off_output}" "${static_on_output}" 0 0 0
    RESULT_VARIABLE static_compare_result
    OUTPUT_VARIABLE static_compare_output)
if(NOT static_compare_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_motion_vectors: static particles produced motion\n${static_compare_output}")
endif()
message(STATUS "VFX motion vectors: ${compare_output}; static: ${static_compare_output}")
