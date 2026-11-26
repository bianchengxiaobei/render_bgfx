if(NOT DEFINED HBG_EXE OR NOT DEFINED HBG_BACKEND OR NOT DEFINED HBG_GOLDEN OR
   NOT DEFINED HBG_OUTPUT OR NOT DEFINED HBG_WORKDIR OR NOT DEFINED HBG_PYTHON)
    message(FATAL_ERROR "hbg_vfx_transparent_batch_sort: missing required variables")
endif()

set(scene "${CMAKE_CURRENT_LIST_DIR}/assets/vfx_transparent_batch_order.gltf")
set(effect_output "${HBG_OUTPUT}.effect.png")
file(REMOVE "${effect_output}" "${HBG_OUTPUT}")

execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}" "--render-test=${scene}" "--vfx-test"
            "--vfx-instancing" "--vfx-transparent-sort"
            "--frames=30" "--width=640" "--height=360" "--no-vsync"
            "--screenshot=${effect_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE effect_result
    OUTPUT_VARIABLE effect_log
    ERROR_VARIABLE effect_error)
if(NOT effect_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_transparent_batch_sort: effect-level render failed (${effect_result})\n${effect_log}\n${effect_error}")
endif()
string(FIND "${effect_log}" "VFX transparent order: effects=1 batches=1" effect_order)
if(effect_order EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_transparent_batch_sort: effect-level ordering log missing\n${effect_log}")
endif()
string(FIND "${effect_log}" "vfx_draws=2" effect_draws)
if(effect_draws EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_transparent_batch_sort: effect-level draw count missing\n${effect_log}")
endif()

execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}" "--render-test=${scene}" "--vfx-test"
            "--vfx-instancing" "--vfx-transparent-sort" "--vfx-instance-batch=1"
            "--frames=30" "--width=640" "--height=360" "--no-vsync"
            "--screenshot=${HBG_OUTPUT}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE batch_result
    OUTPUT_VARIABLE batch_log
    ERROR_VARIABLE batch_error)
if(NOT batch_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_transparent_batch_sort: batch render failed (${batch_result})\n${batch_log}\n${batch_error}")
endif()
string(FIND "${batch_log}" "VFX transparent order: effects=1 batches=3" batch_order)
if(batch_order EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_transparent_batch_sort: per-batch ordering log missing\n${batch_log}")
endif()
string(FIND "${batch_log}" "vfx_draws=6" batch_draws)
if(batch_draws EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_transparent_batch_sort: per-batch draw count missing\n${batch_log}")
endif()

if(NOT EXISTS "${effect_output}" OR NOT EXISTS "${HBG_OUTPUT}")
    message(FATAL_ERROR "hbg_vfx_transparent_batch_sort: screenshot was not written")
endif()

execute_process(
    COMMAND "${HBG_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/compare_png.py"
            "${effect_output}" "${HBG_OUTPUT}" 1000000 0 1
    RESULT_VARIABLE order_compare
    OUTPUT_VARIABLE order_output)
if(NOT order_compare EQUAL 0)
    message(FATAL_ERROR
            "hbg_vfx_transparent_batch_sort: per-batch ordering did not change pixels\n${order_output}")
endif()

execute_process(
    COMMAND "${HBG_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/compare_png.py"
            "${HBG_GOLDEN}" "${HBG_OUTPUT}" 4 1
    RESULT_VARIABLE golden_compare
    OUTPUT_VARIABLE golden_output)
if(NOT golden_compare EQUAL 0)
    message(FATAL_ERROR
            "hbg_vfx_transparent_batch_sort: ${HBG_BACKEND} screenshot differs from ${HBG_GOLDEN}\n${golden_output}")
endif()
