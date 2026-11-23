if(NOT DEFINED HBG_EXE OR NOT DEFINED HBG_BACKEND OR NOT DEFINED HBG_WORKDIR)
    message(FATAL_ERROR "hbg_vfx_empty: missing required variables")
endif()

set(output "${CMAKE_CURRENT_BINARY_DIR}/vfx_empty_${HBG_BACKEND}.png")
set(baseline "${CMAKE_CURRENT_BINARY_DIR}/vfx_empty_off_${HBG_BACKEND}.png")
set(common_args --backend=${HBG_BACKEND}
        --render-test=${CMAKE_CURRENT_LIST_DIR}/assets/vfx_empty.gltf
        --vfx-test --frames=30 --width=640 --height=360 --no-vsync)

execute_process(
    COMMAND "${HBG_EXE}" ${common_args} "--screenshot=${output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE empty_result
    OUTPUT_VARIABLE empty_output
    ERROR_VARIABLE empty_error)
if(NOT empty_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_empty: empty-effect render failed (${empty_result})\n${empty_output}\n${empty_error}")
endif()
if(NOT empty_output MATCHES "vfx_effects=1" OR NOT empty_output MATCHES "vfx_particles=0")
    message(FATAL_ERROR "hbg_vfx_empty: effect was not loaded as an empty 0-particle effect\n${empty_output}")
endif()

execute_process(
    COMMAND "${HBG_EXE}" ${common_args} "--no-vfx" "--screenshot=${baseline}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE baseline_result
    OUTPUT_VARIABLE baseline_output
    ERROR_VARIABLE baseline_error)
if(NOT baseline_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_empty: no-VFX render failed (${baseline_result})\n${baseline_output}\n${baseline_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${baseline}" "${output}"
    RESULT_VARIABLE compare_result)
if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_empty: empty effect differs from the no-VFX baseline")
endif()
