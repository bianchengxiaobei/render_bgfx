if(NOT DEFINED HBG_EXECUTABLE OR NOT EXISTS "${HBG_EXECUTABLE}")
    message(FATAL_ERROR "HBG_EXECUTABLE is missing: ${HBG_EXECUTABLE}")
endif()
if(NOT DEFINED HBG_SCENE OR NOT EXISTS "${HBG_SCENE}")
    message(FATAL_ERROR "HBG_SCENE is missing: ${HBG_SCENE}")
endif()
if(NOT DEFINED HBG_GRAPH OR NOT EXISTS "${HBG_GRAPH}")
    message(FATAL_ERROR "HBG_GRAPH is missing: ${HBG_GRAPH}")
endif()
if(NOT DEFINED HBG_WORK_DIR)
    message(FATAL_ERROR "HBG_WORK_DIR is required")
endif()

file(REMOVE_RECURSE "${HBG_WORK_DIR}")
file(MAKE_DIRECTORY "${HBG_WORK_DIR}")
get_filename_component(asset_name "${HBG_GRAPH}" NAME)
set(copied_graph "${HBG_WORK_DIR}/${asset_name}")
file(COPY "${HBG_GRAPH}" DESTINATION "${HBG_WORK_DIR}")

string(REPLACE ".vfx.json" ".vfxb" binary_name "${asset_name}")
set(binary_path "${HBG_WORK_DIR}/${binary_name}")

set(command
    "${HBG_EXECUTABLE}"
    "--backend=d3d11"
    "--scene=${HBG_SCENE}"
    "--vfx-editor=${copied_graph}"
    "--vfx-editor-compile"
    "--no-vsync")
execute_process(
    COMMAND ${command}
    WORKING_DIRECTORY "${HBG_SOURCE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error_output)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "editor compile failed (${result})\n${output}\n${error_output}")
endif()
if(NOT EXISTS "${binary_path}")
    message(FATAL_ERROR "editor compile did not write ${binary_path}\n${output}\n${error_output}")
endif()
file(SIZE "${binary_path}" binary_size)
if(binary_size EQUAL 0)
    message(FATAL_ERROR "editor compile wrote an empty ${binary_path}")
endif()
message(STATUS "editor compile wrote ${binary_path} (${binary_size} bytes)")

file(SHA256 "${copied_graph}" graph_hash_before)
file(SHA256 "${binary_path}" binary_hash_before)

set(preview_command
    "${HBG_EXECUTABLE}"
    "--backend=d3d11"
    "--scene=${HBG_SCENE}"
    "--vfx-editor=${copied_graph}"
    "--vfx-editor-preview"
    "--no-vsync")
execute_process(
    COMMAND ${preview_command}
    WORKING_DIRECTORY "${HBG_SOURCE_DIR}"
    RESULT_VARIABLE preview_result
    OUTPUT_VARIABLE preview_output
    ERROR_VARIABLE preview_error_output)
if(NOT preview_result EQUAL 0)
    message(FATAL_ERROR "editor preview failed (${preview_result})\n${preview_output}\n${preview_error_output}")
endif()

file(SHA256 "${copied_graph}" graph_hash_after)
file(SHA256 "${binary_path}" binary_hash_after)
if(NOT graph_hash_before STREQUAL graph_hash_after)
    message(FATAL_ERROR "editor preview modified the source graph")
endif()
if(NOT binary_hash_before STREQUAL binary_hash_after)
    message(FATAL_ERROR "editor preview modified the canonical .vfxb")
endif()
message(STATUS "editor preview left graph and canonical .vfxb unchanged")
