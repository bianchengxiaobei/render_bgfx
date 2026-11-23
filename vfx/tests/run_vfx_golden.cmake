if(NOT DEFINED HBG_EXE OR NOT DEFINED HBG_BACKEND OR NOT DEFINED HBG_SCENE OR NOT DEFINED HBG_GOLDEN OR
   NOT DEFINED HBG_OUTPUT OR NOT DEFINED HBG_WORKDIR)
    message(FATAL_ERROR "hbg_vfx_golden: missing required variables")
endif()
if(NOT DEFINED HBG_FRAMES)
    set(HBG_FRAMES 3)
endif()

set(render_command
    "${HBG_EXE}" "--backend=${HBG_BACKEND}" "--render-test=${HBG_SCENE}" "--vfx-test"
    "--frames=${HBG_FRAMES}" "--width=640" "--height=360" "--no-vsync" "--screenshot=${HBG_OUTPUT}")
if(DEFINED HBG_PIPELINE_SETTINGS AND NOT "${HBG_PIPELINE_SETTINGS}" STREQUAL "")
    list(APPEND render_command "--pipeline-settings=${HBG_PIPELINE_SETTINGS}")
endif()
if(HBG_DEFERRED)
    list(APPEND render_command "--deferred")
endif()
execute_process(
    COMMAND ${render_command}
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE render_result
    OUTPUT_VARIABLE render_output
    ERROR_VARIABLE render_error)
if(NOT render_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_golden: render failed (${render_result})\n${render_output}\n${render_error}")
endif()
if(NOT EXISTS "${HBG_OUTPUT}")
    message(FATAL_ERROR "hbg_vfx_golden: screenshot was not written: ${HBG_OUTPUT}")
endif()

if(DEFINED HBG_MAX_DIFF_PIXELS AND DEFINED HBG_MAX_CHANNEL_DELTA)
    execute_process(
        COMMAND "${HBG_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/compare_png.py"
                "${HBG_GOLDEN}" "${HBG_OUTPUT}" "${HBG_MAX_DIFF_PIXELS}" "${HBG_MAX_CHANNEL_DELTA}"
        RESULT_VARIABLE compare_result
        OUTPUT_VARIABLE compare_output)
    if(NOT compare_result EQUAL 0)
        message(FATAL_ERROR "hbg_vfx_golden: ${HBG_BACKEND} screenshot exceeds tolerance from ${HBG_GOLDEN}\n${compare_output}")
    endif()
else()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files "${HBG_GOLDEN}" "${HBG_OUTPUT}"
        RESULT_VARIABLE compare_result)
    if(NOT compare_result EQUAL 0)
        message(FATAL_ERROR "hbg_vfx_golden: ${HBG_BACKEND} screenshot differs from ${HBG_GOLDEN}")
    endif()
endif()
