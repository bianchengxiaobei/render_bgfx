if(NOT DEFINED HBG_ROUNDTRIP OR NOT EXISTS "${HBG_ROUNDTRIP}")
    message(FATAL_ERROR "hbg_vfx_editor_roundtrip is missing: ${HBG_ROUNDTRIP}")
endif()
if(NOT DEFINED HBG_ASSET_DIR OR NOT IS_DIRECTORY "${HBG_ASSET_DIR}")
    message(FATAL_ERROR "HBG_ASSET_DIR is missing: ${HBG_ASSET_DIR}")
endif()
if(NOT DEFINED HBG_WORK_DIR)
    message(FATAL_ERROR "HBG_WORK_DIR is required")
endif()
if(NOT DEFINED HBG_EXPECTED_ASSETS)
    set(HBG_EXPECTED_ASSETS 0)
endif()

file(REMOVE_RECURSE "${HBG_WORK_DIR}")
file(MAKE_DIRECTORY "${HBG_WORK_DIR}")

set(roundtrip_command
    "${HBG_ROUNDTRIP}"
    "--assets" "${HBG_ASSET_DIR}"
    "--work" "${HBG_WORK_DIR}")
if(NOT HBG_EXPECTED_ASSETS EQUAL 0)
    list(APPEND roundtrip_command "--expect" "${HBG_EXPECTED_ASSETS}")
endif()

execute_process(
    COMMAND ${roundtrip_command}
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
    RESULT_VARIABLE roundtrip_result
    OUTPUT_VARIABLE roundtrip_output
    ERROR_VARIABLE roundtrip_error)
if(NOT roundtrip_result EQUAL 0)
    message(FATAL_ERROR "editor roundtrip failed (${roundtrip_result})\n${roundtrip_output}\n${roundtrip_error}")
endif()
message(STATUS "${roundtrip_output}")

if(DEFINED HBG_VFXBAKE AND NOT "${HBG_VFXBAKE}" STREQUAL "")
    if(NOT EXISTS "${HBG_VFXBAKE}")
        message(FATAL_ERROR "vfxbake is missing: ${HBG_VFXBAKE}")
    endif()
    if(NOT DEFINED HBG_SHADERC OR NOT EXISTS "${HBG_SHADERC}")
        message(FATAL_ERROR "HBG_SHADERC is missing: ${HBG_SHADERC}")
    endif()
    if(NOT DEFINED HBG_BGFX_SOURCE OR NOT IS_DIRECTORY "${HBG_BGFX_SOURCE}")
        message(FATAL_ERROR "HBG_BGFX_SOURCE is missing: ${HBG_BGFX_SOURCE}")
    endif()

    file(GLOB_RECURSE roundtrip_assets "${HBG_WORK_DIR}/*.vfx.json")
    list(SORT roundtrip_assets)
    if(roundtrip_assets STREQUAL "")
        message(FATAL_ERROR "editor roundtrip: no assets to check under ${HBG_WORK_DIR}")
    endif()

    set(checked_assets 0)
    foreach(asset_path IN LISTS roundtrip_assets)
        string(REPLACE ".vfx.json" ".vfxb" binary_path "${asset_path}")
        set(bake_command
            "${HBG_VFXBAKE}"
            "--input" "${asset_path}"
            "--output" "${binary_path}"
            "--check"
            "--shaderc" "${HBG_SHADERC}"
            "--bgfx-src" "${HBG_BGFX_SOURCE}")
        if(DEFINED HBG_SHADER_PACK AND NOT "${HBG_SHADER_PACK}" STREQUAL "")
            list(APPEND bake_command "--shader-pack" "${HBG_SHADER_PACK}")
        endif()

        execute_process(
            COMMAND ${bake_command}
            WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
            RESULT_VARIABLE bake_result
            OUTPUT_VARIABLE bake_output
            ERROR_VARIABLE bake_error)
        if(NOT bake_result EQUAL 0)
            message(STATUS "editor roundtrip: retrying ${asset_path} after shaderc failure")
            execute_process(
                COMMAND ${bake_command}
                WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
                RESULT_VARIABLE bake_result
                OUTPUT_VARIABLE bake_output
                ERROR_VARIABLE bake_error)
        endif()
        if(NOT bake_result EQUAL 0)
            message(FATAL_ERROR "editor roundtrip asset check failed (${bake_result}) for ${asset_path}\n${bake_output}\n${bake_error}")
        endif()
        math(EXPR checked_assets "${checked_assets} + 1")
    endforeach()
    message(STATUS "editor roundtrip: vfxbake --check passed for ${checked_assets} assets")
else()
    message(STATUS "editor roundtrip: vfxbake toolchain unavailable; save/hash checks only")
endif()
