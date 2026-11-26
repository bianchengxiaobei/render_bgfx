if(NOT DEFINED HBG_EXE OR NOT EXISTS "${HBG_EXE}")
    message(FATAL_ERROR "HBG_EXE is missing: ${HBG_EXE}")
endif()
if(NOT DEFINED HBG_BACKEND OR NOT DEFINED HBG_SCENE OR NOT DEFINED HBG_GRAPH OR NOT DEFINED HBG_OUTPUT OR
   NOT DEFINED HBG_WORKDIR)
    message(FATAL_ERROR "hbg_vfx_editor_screenshot: missing required variables")
endif()

get_filename_component(output_dir "${HBG_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(REMOVE "${HBG_OUTPUT}")

execute_process(
    COMMAND "${HBG_EXE}"
            "--backend=${HBG_BACKEND}"
            "--scene=${HBG_SCENE}"
            "--vfx-editor=${HBG_GRAPH}"
            "--frames=3"
            "--width=1280"
            "--height=720"
            "--no-vsync"
            "--screenshot=${HBG_OUTPUT}"
    WORKING_DIRECTORY "${HBG_WORKDIR}"
    RESULT_VARIABLE render_result
    OUTPUT_VARIABLE render_output
    ERROR_VARIABLE render_error)
if(NOT render_result EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_editor_screenshot: ${HBG_BACKEND} failed (${render_result})\n${render_output}\n${render_error}")
endif()
if(NOT EXISTS "${HBG_OUTPUT}")
    message(FATAL_ERROR "hbg_vfx_editor_screenshot: screenshot was not written: ${HBG_OUTPUT}")
endif()

file(SIZE "${HBG_OUTPUT}" screenshot_size)
if(screenshot_size EQUAL 0)
    message(FATAL_ERROR "hbg_vfx_editor_screenshot: screenshot is empty: ${HBG_OUTPUT}")
endif()
file(READ "${HBG_OUTPUT}" png_header HEX OFFSET 0 LIMIT 24)
string(SUBSTRING "${png_header}" 0 16 signature)
string(SUBSTRING "${png_header}" 32 8 width_hex)
string(SUBSTRING "${png_header}" 40 8 height_hex)
if(NOT signature STREQUAL "89504e470d0a1a0a")
    message(FATAL_ERROR "hbg_vfx_editor_screenshot: not a PNG: ${HBG_OUTPUT}")
endif()
if(NOT width_hex STREQUAL "00000500" OR NOT height_hex STREQUAL "000002d0")
    message(FATAL_ERROR "hbg_vfx_editor_screenshot: expected 1280x720, got ${width_hex}x${height_hex}")
endif()

message(STATUS "editor screenshot: ${HBG_OUTPUT} (${screenshot_size} bytes, 1280x720, ${HBG_BACKEND})")
