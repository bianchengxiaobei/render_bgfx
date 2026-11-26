if(NOT DEFINED HBG_EXE OR NOT DEFINED HBG_BACKEND OR NOT DEFINED HBG_OUTPUT_DIR OR NOT DEFINED HBG_WORKDIR)
    message(FATAL_ERROR "hbg_vfx_instancing_cull: missing required variables")
endif()

function(run_instancing_cull_pair scene_name prefix)
    set(scene "${CMAKE_CURRENT_LIST_DIR}/assets/${scene_name}")
    set(off_output "${HBG_OUTPUT_DIR}/${prefix}_off_${HBG_BACKEND}.png")
    set(on_output "${HBG_OUTPUT_DIR}/${prefix}_on_${HBG_BACKEND}.png")
    file(REMOVE "${off_output}" "${on_output}")
    execute_process(
        COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}"
                "--render-test=${scene}" "--vfx-test"
                "--frames=10" "--width=320" "--height=180" "--no-vsync"
                "--screenshot=${off_output}"
        WORKING_DIRECTORY "${HBG_WORKDIR}"
        RESULT_VARIABLE off_result
        OUTPUT_VARIABLE off_log
        ERROR_VARIABLE off_error)
    if(NOT off_result EQUAL 0)
        message(FATAL_ERROR "${prefix}: ungrouped render failed (${off_result})\n${off_log}\n${off_error}")
    endif()

    execute_process(
        COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}"
                "--render-test=${scene}" "--vfx-test" "--vfx-instancing"
                "--frames=10" "--width=320" "--height=180" "--no-vsync"
                "--screenshot=${on_output}"
        WORKING_DIRECTORY "${HBG_WORKDIR}"
        RESULT_VARIABLE on_result
        OUTPUT_VARIABLE on_log
        ERROR_VARIABLE on_error)
    if(NOT on_result EQUAL 0)
        message(FATAL_ERROR "${prefix}: grouped render failed (${on_result})\n${on_log}\n${on_error}")
    endif()

    if(NOT EXISTS "${off_output}" OR NOT EXISTS "${on_output}")
        message(FATAL_ERROR "${prefix}: screenshot was not written")
    endif()

    string(FIND "${off_log}" "active effects=3 instances=3" off_effects)
    if(off_effects EQUAL -1)
        message(FATAL_ERROR "${prefix}: expected three ungrouped effects\n${off_log}")
    endif()
    string(FIND "${on_log}" "active effects=1 instances=3" on_effects)
    if(on_effects EQUAL -1)
        message(FATAL_ERROR "${prefix}: expected one grouped effect\n${on_log}")
    endif()
    string(FIND "${off_log}" "vfx_draws=4" off_draws)
    if(off_draws EQUAL -1)
        message(FATAL_ERROR "${prefix}: expected four culled ungrouped VFX draws\n${off_log}")
    endif()
    string(FIND "${on_log}" "vfx_draws=2" on_draws)
    if(on_draws EQUAL -1)
        message(FATAL_ERROR "${prefix}: expected two partially batched VFX draws\n${on_log}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files "${off_output}" "${on_output}"
        RESULT_VARIABLE compare_result)
    if(NOT compare_result EQUAL 0)
        message(FATAL_ERROR "${prefix}: partially grouped and ungrouped screenshots differ")
    endif()

    set(split_output "${HBG_OUTPUT_DIR}/${prefix}_split_${HBG_BACKEND}.png")
    file(REMOVE "${split_output}")
    execute_process(
        COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}"
                "--render-test=${scene}" "--vfx-test" "--vfx-instancing" "--vfx-instance-batch=1"
                "--frames=10" "--width=320" "--height=180" "--no-vsync"
                "--screenshot=${split_output}"
        WORKING_DIRECTORY "${HBG_WORKDIR}"
        RESULT_VARIABLE split_result
        OUTPUT_VARIABLE split_log
        ERROR_VARIABLE split_error)
    if(NOT split_result EQUAL 0)
        message(FATAL_ERROR "${prefix}: split partial render failed (${split_result})\n${split_log}\n${split_error}")
    endif()
    string(FIND "${split_log}" "vfx_draws=4" split_draws)
    if(split_draws EQUAL -1)
        message(FATAL_ERROR "${prefix}: expected four split partial VFX draws\n${split_log}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files "${off_output}" "${split_output}"
        RESULT_VARIABLE split_compare_result)
    if(NOT split_compare_result EQUAL 0)
        message(FATAL_ERROR "${prefix}: split partial and ungrouped screenshots differ")
    endif()

    set(motion_off_output "${HBG_OUTPUT_DIR}/${prefix}_motion_off_${HBG_BACKEND}.png")
    set(motion_on_output "${HBG_OUTPUT_DIR}/${prefix}_motion_on_${HBG_BACKEND}.png")
    file(REMOVE "${motion_off_output}" "${motion_on_output}")
    execute_process(
        COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}"
                "--render-test=${scene}" "--vfx-test"
                "--debug-view=motion-vectors" "--frames=10" "--width=320" "--height=180"
                "--no-vsync" "--screenshot=${motion_off_output}"
        WORKING_DIRECTORY "${HBG_WORKDIR}"
        RESULT_VARIABLE motion_off_result
        OUTPUT_VARIABLE motion_off_log
        ERROR_VARIABLE motion_off_error)
    if(NOT motion_off_result EQUAL 0)
        message(FATAL_ERROR "${prefix}: ungrouped motion-vector render failed (${motion_off_result})\n${motion_off_log}\n${motion_off_error}")
    endif()
    execute_process(
        COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}"
                "--render-test=${scene}" "--vfx-test" "--vfx-instancing"
                "--debug-view=motion-vectors" "--frames=10" "--width=320" "--height=180"
                "--no-vsync" "--screenshot=${motion_on_output}"
        WORKING_DIRECTORY "${HBG_WORKDIR}"
        RESULT_VARIABLE motion_on_result
        OUTPUT_VARIABLE motion_on_log
        ERROR_VARIABLE motion_on_error)
    if(NOT motion_on_result EQUAL 0)
        message(FATAL_ERROR "${prefix}: grouped motion-vector render failed (${motion_on_result})\n${motion_on_log}\n${motion_on_error}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files "${motion_off_output}" "${motion_on_output}"
        RESULT_VARIABLE motion_compare_result)
    if(NOT motion_compare_result EQUAL 0)
        message(FATAL_ERROR "${prefix}: partially grouped and ungrouped motion-vector screenshots differ")
    endif()
endfunction()

run_instancing_cull_pair("vfx_instancing_cull.gltf" "vfx_instancing_cull")
run_instancing_cull_pair("vfx_instancing_cull_gap.gltf" "vfx_instancing_cull_gap")
