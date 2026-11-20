if(NOT DEFINED HBG_EXE OR NOT DEFINED HBG_BACKEND OR NOT DEFINED HBG_SCENE OR NOT DEFINED HBG_GOLDEN OR
   NOT DEFINED HBG_OUTPUT OR NOT DEFINED HBG_WORKDIR)
    message(FATAL_ERROR "hbg_vfx_golden: missing required variables")
endif()

execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}" "--render-test=${HBG_SCENE}" "--vfx-test"
            "--frames=3" "--width=640" "--height=360" "--no-vsync" "--screenshot=${HBG_OUTPUT}"
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

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${HBG_GOLDEN}" "${HBG_OUTPUT}"
    RESULT_VARIABLE compare_result)
if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_golden: ${HBG_BACKEND} screenshot differs from ${HBG_GOLDEN}")
endif()
