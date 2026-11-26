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
option(HBG_SKIP_MAIN_SHADER_BUILD "Skip recompiling the existing main shader variants" OFF)
option(HBG_ENABLE_DRACO "Enable KHR_draco_mesh_compression decoding" ON)
option(HBG_ENABLE_BASISU "Enable KTX2 BasisU supercompression transcoding" ON)
option(HBG_IDL_CODEGEN "Regenerate uniform declarations from config/uniforms/*.idl" ON)
option(HBG_ENABLE_PARCEL "Enable Parcel import + RenderScene bridge for non-glTF scene sources" OFF)
set(HBG_PARCEL_ROOT "" CACHE PATH "Path to the Parcel source root")
set(HBG_PARCEL_ENTT_ROOT "" CACHE PATH "Path to the EnTT source root used by Parcel")
set(HBG_PARCEL_ASSIMP_ROOT "" CACHE PATH "Path to the Assimp root used by Parcel")
set(HBG_BLENDER_EXECUTABLE "" CACHE FILEPATH "Optional Blender executable used to convert .blend scenes")
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
if(HBG_ENABLE_BASISU)
    target_link_libraries(hbg_asset PRIVATE ${HBG_BASISU_TARGET})
    target_compile_definitions(hbg_asset PRIVATE HBG_HAS_BASISU=1)
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
    src/app/sg_editor_host.cpp
    src/app/sg_preview_renderer.cpp
    src/app/sg_http_spike.cpp
    src/app/sg_jpeg.cpp
    src/app/sg_spike.cpp
    shadergraph/spike/sgb.cpp
    src/render/atmosphere/volumetric_fog_settings.cpp
    src/render/camera/dynamic_resolution.cpp
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
    src/render/material/material_texture_array.cpp
    src/render/material/render_queue.cpp
    src/render/material/shader_pass.cpp
    src/render/passes/forward_renderer.cpp
    src/render/passes/prepass_utils.cpp
    src/render/shadergraph/graph_binding.cpp
    src/render/shadergraph/graph_material_system.cpp
    src/render/shadergraph/graph_program_cache.cpp
    src/render/shadergraph/sgb_asset.cpp
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
if(WIN32)
    target_link_libraries(hbg_app PRIVATE ws2_32)
endif()
target_include_directories(hbg_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shadergraph/spike
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_compile_definitions(hbg_app PRIVATE
    HBG_SHADER_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    HBG_SG_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    HBG_SG_SPIKE_ASSET_DEFAULT="${CMAKE_CURRENT_SOURCE_DIR}/tests/shadergraph/assets/spike.sgb"
    HBG_SG_BGFX_SOURCE_DEFAULT="${HBG_BGFX_SOURCE_DIR}"
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

if(HBG_SHADER_COMPILER)
    target_compile_definitions(hbg_app PRIVATE
        HBG_SG_SHADERC_DEFAULT="${HBG_SHADER_COMPILER}")
endif()

if(HBG_SHADER_COMPILER AND HBG_PYTHON_EXECUTABLE AND HBG_BGFX_SOURCE_DIR AND NOT HBG_SKIP_MAIN_SHADER_BUILD)
    add_custom_target(hbg_shaders ALL
        COMMAND ${HBG_PYTHON_EXECUTABLE} ${HBG_PYTHON_ARGS}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/shader_build.py
                --root ${CMAKE_CURRENT_SOURCE_DIR}
                --plan-check ${CMAKE_CURRENT_SOURCE_DIR}/shaders/variants.json
        COMMAND ${HBG_PYTHON_EXECUTABLE} ${HBG_PYTHON_ARGS}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/shader_build.py
                --root ${CMAKE_CURRENT_SOURCE_DIR}
                --execute-plan ${CMAKE_CURRENT_SOURCE_DIR}/shaders/variants.json
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
elseif(NOT HBG_SKIP_MAIN_SHADER_BUILD)
    message(WARNING "hbg: shader compiler unavailable; enable HBG_BUILD_SHADER_TOOLS or set HBG_SHADERC")
endif()

if(HBG_SHADER_COMPILER AND HBG_PYTHON_EXECUTABLE AND HBG_BGFX_SOURCE_DIR)
    add_custom_target(hbg_sg_spike_shaders ALL
        COMMAND ${HBG_PYTHON_EXECUTABLE} ${HBG_PYTHON_ARGS}
                ${CMAKE_CURRENT_SOURCE_DIR}/shadergraph/spike/build_spike.py
                --root ${CMAKE_CURRENT_SOURCE_DIR}
                --shaderc "${HBG_SHADER_COMPILER}"
                --bgfx-src "${HBG_BGFX_SOURCE_DIR}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Compiling SG-0 spike shaders and packing spike.sgb"
        VERBATIM
    )
    if(TARGET shaderc)
        add_dependencies(hbg_sg_spike_shaders shaderc)
    endif()
    if(TARGET hbg_idl)
        add_dependencies(hbg_sg_spike_shaders hbg_idl)
    endif()
    add_dependencies(hbg_app hbg_sg_spike_shaders)
else()
    message(WARNING "hbg: SG-0 spike shader target skipped; shaderc/Python/bgfx source unavailable")
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

add_executable(sg_generate_surface
    shadergraph/spike/generate_surface.cpp
)
hbg_target_options(sg_generate_surface)
set_target_properties(sg_generate_surface PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

set(HBG_PARCEL_AVAILABLE OFF)
if(HBG_ENABLE_PARCEL)
    if(NOT HBG_PARCEL_ROOT OR NOT EXISTS "${HBG_PARCEL_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR "hbg: HBG_ENABLE_PARCEL requires HBG_PARCEL_ROOT")
    endif()
    set(PARCEL_BUILD_TESTS OFF CACHE BOOL "Build Parcel tests" FORCE)
    set(PARCEL_BUILD_FUZZER OFF CACHE BOOL "Build Parcel fuzzers" FORCE)
    set(PARCEL_BUILD_BENCHMARKS OFF CACHE BOOL "Build Parcel benchmarks" FORCE)
    set(PARCEL_HBG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "hdrp-bgfx root for parcel_hbg" FORCE)
    if(HBG_PARCEL_ENTT_ROOT)
        set(PARCEL_ENTT_ROOT "${HBG_PARCEL_ENTT_ROOT}" CACHE PATH "EnTT root for Parcel" FORCE)
    endif()
    if(HBG_PARCEL_ASSIMP_ROOT)
        set(PARCEL_ASSIMP_ROOT "${HBG_PARCEL_ASSIMP_ROOT}" CACHE PATH "Assimp root for Parcel" FORCE)
    endif()
    if(NOT PARCEL_HBG_BGFX_INCLUDE_DIR)
        set(PARCEL_HBG_BGFX_INCLUDE_DIR "${HBG_BGFX_SOURCE_DIR}/include" CACHE PATH
            "bgfx include directory used by hdrp-bgfx headers" FORCE)
    endif()
    add_subdirectory("${HBG_PARCEL_ROOT}" "${CMAKE_BINARY_DIR}/parcel")
    if(TARGET parcel_import)
        target_include_directories(parcel_import PRIVATE "${HBG_PARCEL_ROOT}/third_party/cgltf")
    endif()
    if(TARGET parcel_hbg)
        set(HBG_PARCEL_AVAILABLE ON)
        message(STATUS "hbg: Parcel import bridge enabled (${HBG_PARCEL_ROOT})")
    else()
        message(WARNING "hbg: Parcel bridge target was not created; check PARCEL_ENTT_ROOT/PARCEL_HBG_ROOT")
    endif()
endif()

if(HBG_BUILD_TESTS)
    enable_testing()
endif()

if(HBG_BUILD_DAWNBAKE)
    add_subdirectory(tools/dawnbake)
endif()

if(HBG_BUILD_SHADER_ANALYSIS)
    add_subdirectory(tools/shader_analysis)
endif()

add_subdirectory(shadergraph)
target_link_libraries(hbg_app PUBLIC hbg_sg)
if(HBG_BUILD_TESTS)
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
