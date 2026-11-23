if(NOT DEFINED HBG_EXE OR NOT DEFINED HBG_BACKEND OR NOT DEFINED HBG_WORKDIR)
    message(FATAL_ERROR "hbg_vfx_sort_compare: missing required variables")
endif()

set(on_output "${CMAKE_CURRENT_BINARY_DIR}/vfx_sort_on_${HBG_BACKEND}.png")
set(off_output "${CMAKE_CURRENT_BINARY_DIR}/vfx_sort_off_${HBG_BACKEND}.png")

execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}"
            "--render-test=${CMAKE_CURRENT_LIST_DIR}/assets/vfx_sort.gltf" "--vfx-test"
            "--frames=10" "--width=640" "--height=360" "--no-vsync" "--screenshot=${on_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE on_result
    OUTPUT_VARIABLE on_output_log
    ERROR_VARIABLE on_error_log)
if(NOT on_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_sort_compare: sorted render failed (${on_result})\n${on_output_log}\n${on_error_log}")
endif()

execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}"
            "--render-test=${CMAKE_CURRENT_LIST_DIR}/assets/vfx_sort_off.gltf" "--vfx-test"
            "--frames=10" "--width=640" "--height=360" "--no-vsync" "--screenshot=${off_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE off_result
    OUTPUT_VARIABLE off_output_log
    ERROR_VARIABLE off_error_log)
if(NOT off_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_sort_compare: unsorted render failed (${off_result})\n${off_output_log}\n${off_error_log}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${on_output}" "${off_output}"
    RESULT_VARIABLE compare_result)
if(compare_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_sort_compare: sorted and unsorted screenshots are identical")
endif()
