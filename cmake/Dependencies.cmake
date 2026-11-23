include(FetchContent)

set(HBG_BGFX_CMAKE_DIR "" CACHE PATH "Path to a local bgfx.cmake checkout")
set(HBG_BGFX_SOURCE_DIR "" CACHE PATH "Path to the bgfx source checkout (used for example shaders)")
set(HBG_GLFW_DIR "" CACHE PATH "Path to a local GLFW checkout")
set(HBG_CGLTF_DIR "" CACHE PATH "Path to a local cgltf checkout")
set(HBG_DRACO_DIR "" CACHE PATH "Path to a local Draco checkout")
set(HBG_MIKKTSPACE_DIR "" CACHE PATH "Path to a local MikkTSpace checkout")
set(HBG_NLOHMANN_JSON_DIR "" CACHE PATH "Path to a local nlohmann/json checkout")
set(HBG_XATLAS_DIR "" CACHE PATH "Path to a local xatlas checkout")

set(BGFX_BUILD_TOOLS ${HBG_BUILD_SHADER_TOOLS} CACHE BOOL "Build bgfx tools" FORCE)
set(BGFX_BUILD_TOOLS_SHADER ${HBG_BUILD_SHADER_TOOLS} CACHE BOOL "Build bgfx shaderc" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "Build bin2c" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "Build geometryc/geometryv" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "Build texturec/texturev" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "Build bgfx examples" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "Build bgfx tests" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "Build bgfx install target" FORCE)
set(BGFX_CONFIG_MULTITHREADED ON CACHE BOOL "Build bgfx with multithreaded renderer" FORCE)

if(HBG_BGFX_CMAKE_DIR)
    add_subdirectory(${HBG_BGFX_CMAKE_DIR} ${CMAKE_BINARY_DIR}/bgfx.cmake)
else()
    FetchContent_Declare(
        bgfx.cmake
        GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
        GIT_TAG v1.167.9621-609
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(bgfx.cmake)
endif()

if(NOT HBG_BGFX_SOURCE_DIR)
    if(EXISTS "${FETCHCONTENT_BASE_DIR}/bgfx-src/examples/runtime/shaders")
        set(HBG_BGFX_SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/bgfx-src" CACHE PATH
            "Path to the bgfx source checkout (used for example shaders)" FORCE)
    elseif(HBG_BGFX_CMAKE_DIR AND EXISTS "${HBG_BGFX_CMAKE_DIR}/bgfx/examples/runtime/shaders")
        set(HBG_BGFX_SOURCE_DIR "${HBG_BGFX_CMAKE_DIR}/bgfx" CACHE PATH
            "Path to the bgfx source checkout (used for example shaders)" FORCE)
    endif()
endif()

set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build GLFW documentation" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build GLFW tests" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build GLFW examples" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "Install GLFW" FORCE)

if(HBG_GLFW_DIR)
    add_subdirectory(${HBG_GLFW_DIR} ${CMAKE_BINARY_DIR}/glfw)
else()
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glfw)
endif()

if(HBG_CGLTF_DIR)
    set(HBG_CGLTF_SOURCE_DIR "${HBG_CGLTF_DIR}")
else()
    FetchContent_Declare(
        cgltf
        GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
        GIT_TAG v1.15
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(cgltf)
    set(HBG_CGLTF_SOURCE_DIR "${cgltf_SOURCE_DIR}")
endif()

if(HBG_ENABLE_DRACO)
    set(DRACO_TESTS OFF CACHE BOOL "Disable Draco tests" FORCE)
    set(DRACO_BUILD_EXECUTABLES OFF CACHE BOOL "Disable Draco tools" FORCE)
    set(DRACO_INSTALL OFF CACHE BOOL "Disable Draco install" FORCE)
    if(HBG_DRACO_DIR)
        add_subdirectory(${HBG_DRACO_DIR} ${CMAKE_BINARY_DIR}/draco)
    else()
        FetchContent_Declare(
            draco
            GIT_REPOSITORY https://github.com/google/draco.git
            GIT_TAG 1.5.9
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(draco)
    endif()
    if(NOT TARGET draco::draco)
        message(FATAL_ERROR "hbg: Draco target not found; set HBG_DRACO_DIR to a local Draco checkout")
    endif()
    set(HBG_DRACO_TARGET draco::draco)
endif()

if(HBG_MIKKTSPACE_DIR)
    set(HBG_MIKKTSPACE_SOURCE_DIR "${HBG_MIKKTSPACE_DIR}")
else()
    FetchContent_Declare(
        mikktspace
        GIT_REPOSITORY https://github.com/mmikk/MikkTSpace.git
        GIT_TAG 3e895b49d05ea07e4c2133156cfa94369e19e409
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(mikktspace)
    set(HBG_MIKKTSPACE_SOURCE_DIR "${mikktspace_SOURCE_DIR}")
endif()

add_library(hbg_mikktspace STATIC ${HBG_MIKKTSPACE_SOURCE_DIR}/mikktspace.c)
target_include_directories(hbg_mikktspace PUBLIC ${HBG_MIKKTSPACE_SOURCE_DIR})

if(HBG_XATLAS_DIR)
    set(HBG_XATLAS_SOURCE_DIR "${HBG_XATLAS_DIR}")
else()
    FetchContent_Declare(
        xatlas
        GIT_REPOSITORY https://github.com/jpcy/xatlas.git
        GIT_TAG f700c7790aaa030e794b52ba7791a05c085faf0c
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(xatlas)
    set(HBG_XATLAS_SOURCE_DIR "${xatlas_SOURCE_DIR}")
endif()

add_library(hbg_xatlas STATIC ${HBG_XATLAS_SOURCE_DIR}/source/xatlas/xatlas.cpp)
target_include_directories(hbg_xatlas PUBLIC ${HBG_XATLAS_SOURCE_DIR}/source/xatlas)
if(MSVC)
    target_compile_options(hbg_xatlas PRIVATE /w)
else()
    target_compile_options(hbg_xatlas PRIVATE -w)
endif()

if(HBG_NLOHMANN_JSON_DIR)
    add_library(hbg_nlohmann_json INTERFACE)
    target_include_directories(hbg_nlohmann_json INTERFACE "${HBG_NLOHMANN_JSON_DIR}/include")
    set(HBG_NLOHMANN_JSON_TARGET hbg_nlohmann_json)
else()
    set(JSON_BuildTests OFF CACHE BOOL "Disable nlohmann/json tests" FORCE)
    set(JSON_Install OFF CACHE BOOL "Disable nlohmann/json install" FORCE)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
    set(HBG_NLOHMANN_JSON_TARGET nlohmann_json::nlohmann_json)
endif()

if(HBG_BUILD_TESTS)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(doctest)
endif()
