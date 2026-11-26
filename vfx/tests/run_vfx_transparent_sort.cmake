if(NOT DEFINED HBG_EXE OR NOT DEFINED HBG_BACKEND OR NOT DEFINED HBG_GOLDEN OR
   NOT DEFINED HBG_OUTPUT OR NOT DEFINED HBG_WORKDIR OR NOT DEFINED HBG_PYTHON)
    message(FATAL_ERROR "hbg_vfx_transparent_sort: missing required variables")
endif()

set(scene "${CMAKE_CURRENT_LIST_DIR}/assets/vfx_transparent_order.gltf")
set(off_output "${HBG_OUTPUT}.off.png")
file(REMOVE "${off_output}" "${HBG_OUTPUT}")

execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}" "--render-test=${scene}" "--vfx-test"
            "--frames=30" "--width=640" "--height=360" "--no-vsync" "--screenshot=${off_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE off_result
    OUTPUT_VARIABLE off_log
    ERROR_VARIABLE off_error)
if(NOT off_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_transparent_sort: legacy render failed (${off_result})\n${off_log}\n${off_error}")
endif()

execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}" "--render-test=${scene}" "--vfx-test"
            "--vfx-transparent-sort" "--frames=30" "--width=640" "--height=360" "--no-vsync"
            "--screenshot=${HBG_OUTPUT}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE on_result
    OUTPUT_VARIABLE on_log
    ERROR_VARIABLE on_error)
if(NOT on_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_transparent_sort: sorted render failed (${on_result})\n${on_log}\n${on_error}")
endif()

if(NOT EXISTS "${off_output}" OR NOT EXISTS "${HBG_OUTPUT}")
    message(FATAL_ERROR "hbg_vfx_transparent_sort: screenshot was not written")
endif()

string(FIND "${on_log}" "active effects=2" on_effects)
if(on_effects EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_transparent_sort: expected one active effect\n${on_log}")
endif()
string(FIND "${on_log}" "VFX transparent order: effects=2" on_order)
if(on_order EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_transparent_sort: transparent ordering was not applied\n${on_log}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${off_output}" "${HBG_OUTPUT}"
    RESULT_VARIABLE order_compare)
if(order_compare EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_transparent_sort: legacy and sorted screenshots are identical")
endif()

execute_process(
    COMMAND "${HBG_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/compare_png.py"
            "${HBG_GOLDEN}" "${HBG_OUTPUT}" 4 1
    RESULT_VARIABLE golden_compare
    OUTPUT_VARIABLE golden_output)
if(NOT golden_compare EQUAL 0)
    message(FATAL_ERROR
            "hbg_vfx_transparent_sort: ${HBG_BACKEND} screenshot differs from ${HBG_GOLDEN}\n${golden_output}")
endif()
