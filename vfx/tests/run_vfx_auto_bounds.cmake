if(NOT DEFINED HBG_EXE OR NOT DEFINED HBG_BACKEND OR NOT DEFINED HBG_WORKDIR)
    message(FATAL_ERROR "hbg_vfx_auto_bounds: missing required variables")
endif()

execute_process(
    COMMAND "${HBG_EXE}" "--backend=${HBG_BACKEND}"
            "--vfx-reference=${CMAKE_CURRENT_LIST_DIR}/assets/auto_bounds.vfxb"
            "--vfx-reference-graph=${CMAKE_CURRENT_LIST_DIR}/assets/auto_bounds.vfx.json"
            "--no-vsync" "--width=64" "--height=64"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE log
    ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_auto_bounds: reference failed (${result})\n${log}\n${error}")
endif()

string(FIND "${log}" "vfx reference automatic bounds: valid=true" valid)
if(valid EQUAL -1)
    message(FATAL_ERROR "hbg_vfx_auto_bounds: automatic bounds invalid\n${log}")
endif()
string(REGEX MATCH "min=[(][^)]*,-0[.]6,-0[.]75[)]" min_match "${log}")
if(NOT min_match)
    message(FATAL_ERROR "hbg_vfx_auto_bounds: expected tight min y/z extents\n${log}")
endif()
string(REGEX MATCH "max=[(][^)]*,0[.]6,0[.]75[)]" max_match "${log}")
if(NOT max_match)
    message(FATAL_ERROR "hbg_vfx_auto_bounds: expected tight max y/z extents\n${log}")
endif()
