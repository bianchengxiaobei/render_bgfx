if(NOT DEFINED HBG_EXE OR NOT DEFINED HBG_BACKEND OR NOT DEFINED HBG_WORKDIR)
    message(FATAL_ERROR "hbg_vfx_order_off: missing required variables")
endif()

set(output "${CMAKE_CURRENT_BINARY_DIR}/vfx_order_off_no_vfx_${HBG_BACKEND}.png")
set(golden "${CMAKE_CURRENT_LIST_DIR}/golden/vfx_order_off_${HBG_BACKEND}.png")

execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}"
            "--render-test=${CMAKE_CURRENT_LIST_DIR}/assets/vfx_order.gltf" "--vfx-test" "--no-vfx"
            "--frames=30" "--width=640" "--height=360" "--no-vsync" "--screenshot=${output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE render_result
    OUTPUT_VARIABLE render_output
    ERROR_VARIABLE render_error)
if(NOT render_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_order_off: render failed (${render_result})\n${render_output}\n${render_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${golden}" "${output}"
    RESULT_VARIABLE compare_result)
if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_order_off: VFX-off output differs from the no-VFX baseline")
endif()
