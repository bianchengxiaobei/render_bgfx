add_executable(hbg_dawnbake
    main.cpp
    src/dawn_export.cpp
    src/dawn_gzip.cpp
    src/dawn_package.cpp
    src/dawn_serialize.cpp
    src/dawn_atlas.cpp
    src/dawn_launch.cpp
    src/dawn_config.cpp
    src/dawn_uv_dump.cpp
    src/dawn_unwrap.cpp
    src/lightmap_atlas.cpp
    src/lightmap_dds.cpp
    src/lightmap_result.cpp
)

target_include_directories(hbg_dawnbake PRIVATE include)
target_compile_definitions(hbg_dawnbake PRIVATE HBG_DAWN_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(hbg_dawnbake PRIVATE hbg_asset hbg_xatlas ${HBG_NLOHMANN_JSON_TARGET} bimg_encode bimg bx)
if(MSVC)
    target_compile_options(hbg_dawnbake PRIVATE /wd4324)
endif()
hbg_target_options(hbg_dawnbake)

find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(hbg_dawnbake PRIVATE ZLIB::ZLIB)
    target_compile_definitions(hbg_dawnbake PRIVATE HBG_DAWN_HAVE_ZLIB=1)
endif()

if(HBG_BUILD_DAWN_GRPC)
    find_package(Protobuf CONFIG REQUIRED)
    find_package(gRPC CONFIG REQUIRED)

    set(DAWN_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
    set(DAWN_GRPC_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/dawn_grpc)
    file(MAKE_DIRECTORY ${DAWN_GRPC_GEN_DIR})

    set(DAWN_PROTO_FILES
        ${DAWN_PROTO_DIR}/DAWNCommon.proto
        ${DAWN_PROTO_DIR}/ClientAgent.proto
        ${DAWN_PROTO_DIR}/Agent.proto
    )
    set(DAWN_GRPC_SOURCES
        ${DAWN_GRPC_GEN_DIR}/DAWNCommon.pb.cc
        ${DAWN_GRPC_GEN_DIR}/ClientAgent.pb.cc
        ${DAWN_GRPC_GEN_DIR}/ClientAgent.grpc.pb.cc
        ${DAWN_GRPC_GEN_DIR}/Agent.pb.cc
        ${DAWN_GRPC_GEN_DIR}/Agent.grpc.pb.cc
    )
    add_custom_command(
        OUTPUT ${DAWN_GRPC_SOURCES}
        COMMAND protobuf::protoc
                --proto_path=${DAWN_PROTO_DIR}
                --cpp_out=${DAWN_GRPC_GEN_DIR}
                --grpc_out=${DAWN_GRPC_GEN_DIR}
                --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
                DAWNCommon.proto ClientAgent.proto Agent.proto
        DEPENDS ${DAWN_PROTO_FILES}
        COMMENT "Generating Dawn Agent gRPC C++ sources"
        VERBATIM
    )

    add_library(hbg_dawn_grpc STATIC
        ${DAWN_GRPC_SOURCES}
        src/dawn_agent_client.cpp
    )
    target_include_directories(hbg_dawn_grpc PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${DAWN_GRPC_GEN_DIR}
        ${DAWN_PROTO_DIR}
    )
    target_link_libraries(hbg_dawn_grpc PUBLIC gRPC::grpc++ protobuf::libprotobuf hbg_core)
    hbg_target_options(hbg_dawn_grpc)
    if(MSVC)
        target_compile_options(hbg_dawn_grpc PRIVATE /wd4996)
    endif()
    target_compile_definitions(hbg_dawnbake PRIVATE HBG_DAWN_GRPC=1)
    target_link_libraries(hbg_dawnbake PRIVATE hbg_dawn_grpc)
    if(MSVC)
        target_compile_options(hbg_dawnbake PRIVATE /wd4996)
        list(GET CMAKE_PREFIX_PATH 0 HBG_DAWN_VCPKG_ROOT)
        set(HBG_DAWN_RUNTIME_SUBDIR "$<IF:$<CONFIG:Debug>,debug/bin,bin>")
        set(HBG_DAWN_ZLIB_DLL "${HBG_DAWN_VCPKG_ROOT}/bin/z.dll")
        add_custom_command(TARGET hbg_dawnbake POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:hbg_dawnbake>
                    "${HBG_DAWN_VCPKG_ROOT}/${HBG_DAWN_RUNTIME_SUBDIR}/libssl-3-x64.dll"
                    "${HBG_DAWN_VCPKG_ROOT}/${HBG_DAWN_RUNTIME_SUBDIR}/libcrypto-3-x64.dll"
                    "${HBG_DAWN_ZLIB_DLL}"
                    $<TARGET_FILE_DIR:hbg_dawnbake>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()

set_target_properties(hbg_dawnbake PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
