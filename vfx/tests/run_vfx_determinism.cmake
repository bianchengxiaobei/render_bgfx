if(NOT DEFINED HBG_EXE OR NOT DEFINED HBG_BACKEND OR NOT DEFINED HBG_SCENE OR NOT DEFINED HBG_WORKDIR OR
   NOT DEFINED HBG_NAME)
    message(FATAL_ERROR "hbg_vfx_determinism: missing required variables")
endif()
if(NOT DEFINED HBG_FRAMES)
    set(HBG_FRAMES 30)
endif()

set(first_output "${CMAKE_CURRENT_BINARY_DIR}/vfx_determinism_${HBG_NAME}_${HBG_BACKEND}_1.png")
set(second_output "${CMAKE_CURRENT_BINARY_DIR}/vfx_determinism_${HBG_NAME}_${HBG_BACKEND}_2.png")

execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}" "--render-test=${HBG_SCENE}" "--vfx-test"
            "--frames=${HBG_FRAMES}" "--width=640" "--height=360" "--no-vsync" "--screenshot=${first_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE first_result
    OUTPUT_VARIABLE first_log
    ERROR_VARIABLE first_error)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_determinism: first render failed (${first_result})\n${first_log}\n${first_error}")
endif()

execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}" "--render-test=${HBG_SCENE}" "--vfx-test"
            "--frames=${HBG_FRAMES}" "--width=640" "--height=360" "--no-vsync" "--screenshot=${second_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_log
    ERROR_VARIABLE second_error)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_determinism: second render failed (${second_result})\n${second_log}\n${second_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${first_output}" "${second_output}"
    RESULT_VARIABLE compare_result)
if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_determinism: ${HBG_NAME}/${HBG_BACKEND} screenshots differ between identical runs")
endif()
