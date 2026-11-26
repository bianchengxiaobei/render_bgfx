if(NOT DEFINED HBG_EXE OR NOT DEFINED HBG_BACKEND OR NOT DEFINED HBG_WORKDIR)
    message(FATAL_ERROR "hbg_vfx_instancing: missing required variables")
endif()

set(off_output "${CMAKE_CURRENT_BINARY_DIR}/vfx_instancing_off_${HBG_BACKEND}.png")
set(on_output "${CMAKE_CURRENT_BINARY_DIR}/vfx_instancing_on_${HBG_BACKEND}.png")
file(REMOVE "${off_output}" "${on_output}")

execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}"
            "--render-test=${CMAKE_CURRENT_LIST_DIR}/assets/vfx_instancing.gltf" "--vfx-test"
            "--frames=10" "--width=320" "--height=180" "--no-vsync" "--screenshot=${off_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE off_result
    OUTPUT_VARIABLE off_log
    ERROR_VARIABLE off_error)
if(NOT off_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_instancing: ungrouped render failed (${off_result})\n${off_log}\n${off_error}")
endif()

execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}"
            "--render-test=${CMAKE_CURRENT_LIST_DIR}/assets/vfx_instancing.gltf" "--vfx-test"
            "--vfx-instancing" "--frames=10" "--width=320" "--height=180" "--no-vsync"
            "--screenshot=${on_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE on_result
    OUTPUT_VARIABLE on_log
    ERROR_VARIABLE on_error)
if(NOT on_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_instancing: grouped render failed (${on_result})\n${on_log}\n${on_error}")
endif()

if(NOT EXISTS "${off_output}" OR NOT EXISTS "${on_output}")
    message(FATAL_ERROR "hbg_vfx_instancing: screenshot was not written")
endif()

string(FIND "${off_log}" "active effects=2 instances=2" off_effects)
if(off_effects EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_instancing: expected two ungrouped effects\n${off_log}")
endif()
string(FIND "${on_log}" "active effects=1 instances=2" on_effects)
if(on_effects EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_instancing: expected one grouped effect\n${on_log}")
endif()
string(FIND "${off_log}" "vfx_draws=4" off_draws)
if(off_draws EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_instancing: expected four ungrouped VFX draws\n${off_log}")
endif()
string(FIND "${on_log}" "vfx_draws=2" on_draws)
if(on_draws EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_instancing: expected two batched VFX draws\n${on_log}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${off_output}" "${on_output}"
    RESULT_VARIABLE compare_result)
if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_instancing: grouped and ungrouped screenshots differ")
endif()

set(split_output "${CMAKE_CURRENT_BINARY_DIR}/vfx_instancing_split_${HBG_BACKEND}.png")
file(REMOVE "${split_output}")
execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}"
            "--render-test=${CMAKE_CURRENT_LIST_DIR}/assets/vfx_instancing.gltf" "--vfx-test"
            "--vfx-instancing" "--vfx-instance-batch=1" "--frames=10" "--width=320" "--height=180"
            "--no-vsync" "--screenshot=${split_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE split_result
    OUTPUT_VARIABLE split_log
    ERROR_VARIABLE split_error)
if(NOT split_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_instancing: split render failed (${split_result})\n${split_log}\n${split_error}")
endif()
string(FIND "${split_log}" "active effects=1 instances=2" split_effects)
if(split_effects EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_instancing: expected one split effect\n${split_log}")
endif()
string(FIND "${split_log}" "vfx_draws=4" split_draws)
if(split_draws EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_instancing: expected four split VFX draws\n${split_log}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${off_output}" "${split_output}"
    RESULT_VARIABLE split_compare_result)
if(NOT split_compare_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_instancing: split and ungrouped screenshots differ")
endif()

set(motion_off_output "${CMAKE_CURRENT_BINARY_DIR}/vfx_instancing_motion_off_${HBG_BACKEND}.png")
set(motion_on_output "${CMAKE_CURRENT_BINARY_DIR}/vfx_instancing_motion_on_${HBG_BACKEND}.png")
file(REMOVE "${motion_off_output}" "${motion_on_output}")
execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}"
            "--render-test=${CMAKE_CURRENT_LIST_DIR}/assets/vfx_instancing.gltf" "--vfx-test"
            "--debug-view=motion-vectors" "--frames=10" "--width=320" "--height=180" "--no-vsync"
            "--screenshot=${motion_off_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE motion_off_result
    OUTPUT_VARIABLE motion_off_log
    ERROR_VARIABLE motion_off_error)
if(NOT motion_off_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_instancing: ungrouped motion-vector render failed (${motion_off_result})\n${motion_off_log}\n${motion_off_error}")
endif()
execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}"
            "--render-test=${CMAKE_CURRENT_LIST_DIR}/assets/vfx_instancing.gltf" "--vfx-test"
            "--vfx-instancing" "--debug-view=motion-vectors" "--frames=10" "--width=320" "--height=180"
            "--no-vsync" "--screenshot=${motion_on_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE motion_on_result
    OUTPUT_VARIABLE motion_on_log
    ERROR_VARIABLE motion_on_error)
if(NOT motion_on_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_instancing: grouped motion-vector render failed (${motion_on_result})\n${motion_on_log}\n${motion_on_error}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${motion_off_output}" "${motion_on_output}"
    RESULT_VARIABLE motion_compare_result)
if(NOT motion_compare_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_instancing: grouped and ungrouped motion-vector screenshots differ")
endif()

set(motion_split_output "${CMAKE_CURRENT_BINARY_DIR}/vfx_instancing_motion_split_${HBG_BACKEND}.png")
file(REMOVE "${motion_split_output}")
execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}"
            "--render-test=${CMAKE_CURRENT_LIST_DIR}/assets/vfx_instancing.gltf" "--vfx-test"
            "--vfx-instancing" "--vfx-instance-batch=1" "--debug-view=motion-vectors"
            "--frames=10" "--width=320" "--height=180" "--no-vsync"
            "--screenshot=${motion_split_output}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE motion_split_result
    OUTPUT_VARIABLE motion_split_log
    ERROR_VARIABLE motion_split_error)
if(NOT motion_split_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_instancing: split motion-vector render failed (${motion_split_result})\n${motion_split_log}\n${motion_split_error}")
endif()
string(FIND "${motion_split_log}" "vfx_draws=4" motion_split_draws)
if(motion_split_draws EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_instancing: expected four split motion-vector VFX draws\n${motion_split_log}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${motion_off_output}" "${motion_split_output}"
    RESULT_VARIABLE motion_split_compare_result)
if(NOT motion_split_compare_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_instancing: split and ungrouped motion-vector screenshots differ")
endif()
