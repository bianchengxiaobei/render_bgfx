cmake_minimum_required(VERSION 3.24)

project(hdrp_bgfx VERSION 0.1.0 LANGUAGES C CXX)

if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum policy version for third-party dependencies" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HBG_BUILD_TESTS "Build unit tests" ON)
option(HBG_BUILD_SHADER_TOOLS "Build bgfx shaderc tools" ON)
option(HBG_BUILD_DAWNBAKE "Build the Dawn GI protocol tool" ON)
option(HBG_BUILD_SHADER_ANALYSIS "Build the shader analysis tool" ON)
option(HBG_BUILD_DAWN_GRPC "Build the optional Dawn Agent gRPC client" OFF)
option(HBG_BUILD_VFX "Build the standalone VFX runtime library" ON)
option(HBG_ENABLE_DRACO "Enable KHR_draco_mesh_compression decoding" ON)
option(HBG_IDL_CODEGEN "Regenerate uniform declarations from config/uniforms/*.idl" ON)
set(HBG_SHADERC "" CACHE FILEPATH "Path to an existing bgfx shaderc executable (optional)")

set(HBG_IDL_PYTHON_AVAILABLE OFF)
set(HBG_PYTHON_EXECUTABLE "")
set(HBG_PYTHON_ARGS "")

if(HBG_IDL_CODEGEN)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        set(HBG_IDL_PYTHON_AVAILABLE ON)
        set(HBG_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
    else()
        find_program(HBG_PY_LAUNCHER NAMES py)
        if(HBG_PY_LAUNCHER)
            set(HBG_IDL_PYTHON_AVAILABLE ON)
            set(HBG_PYTHON_EXECUTABLE "${HBG_PY_LAUNCHER}")
            set(HBG_PYTHON_ARGS "-3")
        endif()
    endif()

    if(NOT HBG_IDL_PYTHON_AVAILABLE)
        message(WARNING "hbg: Python 3 not found; using checked-in generated uniform files")
    endif()
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

include(cmake/Dependencies.cmake)

file(GLOB HBG_IDL_INPUTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/config/uniforms/*.idl)
set(HBG_IDL_OUTPUTS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gfx/generated/uniforms.h
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/include/generated/uniforms.sc
)

if(HBG_IDL_PYTHON_AVAILABLE)
    add_custom_command(
        OUTPUT ${HBG_IDL_OUTPUTS}
        COMMAND ${HBG_PYTHON_EXECUTABLE} ${HBG_PYTHON_ARGS}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/idl_gen.py
                --root ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/idl_gen.py ${HBG_IDL_INPUTS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating uniform declarations from IDL"
        VERBATIM
    )
    add_custom_target(hbg_idl ALL DEPENDS ${HBG_IDL_OUTPUTS})
endif()

function(hbg_target_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:preprocessor)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(hbg_core STATIC
    src/core/log.cpp
    src/core/profiler.cpp
)
target_include_directories(hbg_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(hbg_core PUBLIC bgfx)
hbg_target_options(hbg_core)

add_library(hbg_gfx STATIC
    src/gfx/bgfx_callback.cpp
    src/gfx/buffer.cpp
    src/gfx/device.cpp
    src/gfx/encoder.cpp
    src/gfx/framebuffer_cache.cpp
    src/gfx/instance_data.cpp
    src/gfx/program_cache.cpp
    src/gfx/resource_manager.cpp
    src/gfx/texture.cpp
    src/gfx/uniform_block.cpp
)
target_link_libraries(hbg_gfx PUBLIC hbg_core bgfx PRIVATE bimg_encode bimg bx)
hbg_target_options(hbg_gfx)

if(HBG_IDL_PYTHON_AVAILABLE)
    add_dependencies(hbg_gfx hbg_idl)
endif()

add_library(hbg_shader STATIC
    src/shader/shader_library.cpp
)
target_link_libraries(hbg_shader
    PUBLIC hbg_gfx
    PRIVATE ${HBG_NLOHMANN_JSON_TARGET}
)
hbg_target_options(hbg_shader)

if(HBG_BGFX_SOURCE_DIR AND EXISTS "${HBG_BGFX_SOURCE_DIR}/3rdparty/dear-imgui/imgui.cpp")
    set(HBG_IMGUI_DIR "${HBG_BGFX_SOURCE_DIR}/3rdparty/dear-imgui")
    add_library(hbg_imgui STATIC
        src/app/imgui_stb.cpp
        ${HBG_IMGUI_DIR}/imgui.cpp
        ${HBG_IMGUI_DIR}/imgui_draw.cpp
        ${HBG_IMGUI_DIR}/imgui_tables.cpp
        ${HBG_IMGUI_DIR}/imgui_widgets.cpp
    )
    target_include_directories(hbg_imgui PUBLIC
        ${HBG_IMGUI_DIR}
        ${HBG_BGFX_SOURCE_DIR}/3rdparty
    )
    target_link_libraries(hbg_imgui PUBLIC bx)
    target_compile_definitions(hbg_imgui PRIVATE IMGUI_DISABLE_DEMO_WINDOWS)
    if(MSVC)
        target_compile_options(hbg_imgui PRIVATE /w)
    endif()
else()
    message(FATAL_ERROR "hbg: Dear ImGui sources not found under HBG_BGFX_SOURCE_DIR")
endif()

add_library(hbg_asset STATIC
    src/asset/animation.cpp
    src/asset/gltf_loader.cpp
    src/asset/material.cpp
    src/asset/mesh.cpp
    src/asset/scene_loader.cpp
    src/asset/texture_loader.cpp
    src/render/atmosphere/local_fog_volume.cpp
    src/render/scene/decal.cpp
    src/render/scene/reflection_probe.cpp
    src/render/scene/render_scene.cpp
)
target_include_directories(hbg_asset PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${HBG_CGLTF_SOURCE_DIR}
)
target_link_libraries(hbg_asset
    PUBLIC hbg_gfx
    PRIVATE hbg_mikktspace bimg_decode ${HBG_NLOHMANN_JSON_TARGET}
)
if(HBG_ENABLE_DRACO)
    target_link_libraries(hbg_asset PRIVATE ${HBG_DRACO_TARGET})
    target_compile_definitions(hbg_asset PRIVATE HBG_HAS_DRACO=1)
endif()
hbg_target_options(hbg_asset)
if(MSVC)
    target_compile_definitions(hbg_asset PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_library(hbg_app STATIC
    src/app/asset_test.cpp
    src/app/backend.cpp
    src/app/debug_ui.cpp
    src/app/render_test.cpp
    src/app/window.cpp
    src/app/application.cpp
    src/app/gfx_test.cpp
    src/render/atmosphere/volumetric_fog_settings.cpp
    src/render/camera/resolution_group.cpp
    src/render/camera/view_state.cpp
    src/render/culling/draw_list.cpp
    src/render/culling/frustum.cpp
    src/render/culling/sort_key.cpp
    src/render/debug/debug_display.cpp
    src/render/graph/render_graph.cpp
    src/render/lighting/cookie_atlas.cpp
    src/render/lighting/light_builder.cpp
    src/render/lighting/light_list_builder.cpp
    src/render/lighting/ltc_lut.cpp
    src/render/lightmap/lightmap.cpp
    src/render/material/material_system.cpp
    src/render/material/render_queue.cpp
    src/render/material/shader_pass.cpp
    src/render/passes/forward_renderer.cpp
    src/render/passes/prepass_utils.cpp
    src/render/vfx/vfx_reference.cpp
    src/render/vfx/vfx_spike.cpp
    src/render/pipeline/frame_settings.cpp
    src/render/pipeline/pipeline_settings.cpp
    src/render/postprocess/color_grading_lut.cpp
    src/render/postprocess/post_process_chain.cpp
    src/render/postprocess/post_process_profile.cpp
    src/render/postprocess/post_process_settings.cpp
    src/render/postprocess/ssao_settings.cpp
    src/render/postprocess/ssr_settings.cpp
    src/render/postprocess/ssgi_settings.cpp
    src/render/shadows/contact_shadow.cpp
    src/render/shadows/shadow_settings.cpp
    src/render/shadows/shadow_manager.cpp
    src/render/sky/environment_processor.cpp
    src/render/sky/environment_texture.cpp
    src/render/sky/probe_manager.cpp
    src/render/sky/sky_manager.cpp
)
target_link_libraries(hbg_app
    PUBLIC hbg_asset hbg_shader hbg_imgui
    PRIVATE glfw bimg_decode ${HBG_NLOHMANN_JSON_TARGET}
)
target_compile_definitions(hbg_app PRIVATE
    HBG_SHADER_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)
hbg_target_options(hbg_app)

set(HBG_SHADER_COMPILER "")
if(HBG_BUILD_SHADER_TOOLS AND TARGET shaderc)
    set(HBG_SHADER_COMPILER "$<TARGET_FILE:shaderc>")
elseif(HBG_SHADERC AND EXISTS "${HBG_SHADERC}")
    set(HBG_SHADER_COMPILER "${HBG_SHADERC}")
else()
    find_program(HBG_SHADERC_EXECUTABLE NAMES shaderc)
    if(HBG_SHADERC_EXECUTABLE)
        set(HBG_SHADER_COMPILER "${HBG_SHADERC_EXECUTABLE}")
    endif()
endif()

if(HBG_SHADER_COMPILER AND HBG_PYTHON_EXECUTABLE AND HBG_BGFX_SOURCE_DIR)
    add_custom_target(hbg_shaders ALL
        COMMAND ${HBG_PYTHON_EXECUTABLE} ${HBG_PYTHON_ARGS}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/shader_build.py
                --root ${CMAKE_CURRENT_SOURCE_DIR}
                --shaderc "${HBG_SHADER_COMPILER}"
                --bgfx-src "${HBG_BGFX_SOURCE_DIR}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Compiling HBG shader variants"
        VERBATIM
    )
    if(TARGET shaderc)
        add_dependencies(hbg_shaders shaderc)
    endif()
    if(TARGET hbg_idl)
        add_dependencies(hbg_shaders hbg_idl)
    endif()
    add_dependencies(hbg_app hbg_shaders)
else()
    message(WARNING "hbg: shader compiler unavailable; enable HBG_BUILD_SHADER_TOOLS or set HBG_SHADERC")
endif()

if(HBG_BGFX_SOURCE_DIR AND EXISTS "${HBG_BGFX_SOURCE_DIR}/examples/runtime/shaders")
    target_compile_definitions(hbg_app PRIVATE
        HBG_BGFX_EXAMPLE_SHADERS="${HBG_BGFX_SOURCE_DIR}/examples/runtime/shaders")
    message(STATUS "hbg: bgfx example shaders: ${HBG_BGFX_SOURCE_DIR}/examples/runtime/shaders")
endif()

add_executable(hbg
    src/app/main.cpp
)
target_link_libraries(hbg PRIVATE hbg_app)
hbg_target_options(hbg)

set_target_properties(hbg PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

if(HBG_BUILD_DAWNBAKE)
    add_subdirectory(tools/dawnbake)
endif()

if(HBG_BUILD_SHADER_ANALYSIS)
    add_subdirectory(tools/shader_analysis)
endif()

if(HBG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(HBG_BUILD_VFX)
    add_subdirectory(vfx)
    target_sources(hbg_app PRIVATE src/app/vfx_editor_ui.cpp)
    target_compile_definitions(hbg_app PRIVATE
        HBG_VFX_SHADERC_DEFAULT="${HBG_SHADER_COMPILER}"
        HBG_VFX_BGFX_SOURCE_DEFAULT="${HBG_BGFX_SOURCE_DIR}"
        HBG_VFX_SHADER_PACK_DEFAULT="${CMAKE_SOURCE_DIR}/shaders"
    )
    if(TARGET hbg_vfx_adapter)
        target_link_libraries(hbg_app PUBLIC hbg_vfx_adapter)
    endif()
    if(TARGET hbg_vfx_runtime)
        target_link_libraries(hbg_app PUBLIC hbg_vfx_runtime)
    endif()
    if(TARGET hbg_vfx_compiler)
        target_link_libraries(hbg_app PUBLIC hbg_vfx_compiler)
    endif()
    if(TARGET hbg_vfx)
        target_link_libraries(hbg_app PUBLIC hbg_vfx)
    endif()
else()
    target_sources(hbg_app PRIVATE src/app/vfx_editor_ui_stub.cpp)
endif()
