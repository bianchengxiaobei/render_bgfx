#include "gfx/generated/uniforms.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>

using namespace hbg::gfx;
using namespace hbg::gfx::generated;

static_assert(std::is_standard_layout_v<GlobalUniforms>);
static_assert(std::is_standard_layout_v<PassUniforms>);
static_assert(std::is_standard_layout_v<MaterialUniforms>);
static_assert(offsetof(GlobalUniforms, view) == 0);
static_assert(offsetof(GlobalUniforms, invView) == 64);
static_assert(offsetof(GlobalUniforms, proj) == 128);
static_assert(offsetof(GlobalUniforms, invProj) == 192);
static_assert(offsetof(GlobalUniforms, viewProj) == 256);
static_assert(offsetof(GlobalUniforms, invViewProj) == 320);
static_assert(offsetof(GlobalUniforms, nonJitteredViewProj) == 384);
static_assert(offsetof(GlobalUniforms, nonJitteredInvViewProj) == 448);
static_assert(offsetof(GlobalUniforms, prevViewProj) == 512);
static_assert(offsetof(GlobalUniforms, prevInvViewProj) == 576);
static_assert(offsetof(GlobalUniforms, screenSize) == 640);
static_assert(offsetof(GlobalUniforms, cameraPos) == 656);
static_assert(offsetof(GlobalUniforms, prevCameraPos) == 672);
static_assert(offsetof(GlobalUniforms, taaJitter) == 688);
static_assert(offsetof(GlobalUniforms, taaFrameInfo) == 704);
static_assert(offsetof(GlobalUniforms, rtScale) == 720);
static_assert(offsetof(GlobalUniforms, dynamicResolutionScale) == 736);
static_assert(offsetof(GlobalUniforms, time) == 752);
static_assert(offsetof(GlobalUniforms, frustumPlanes) == 768);
static_assert(sizeof(GlobalUniforms) == 864);
static_assert(offsetof(PassUniforms, shadowViewProj) == 64);
static_assert(offsetof(PassUniforms, shadowParams) == 128);
static_assert(offsetof(PassUniforms, shadowCascadeSplits) == 144);
static_assert(offsetof(PassUniforms, lightDirection) == 160);
static_assert(offsetof(PassUniforms, lightColor) == 176);
static_assert(offsetof(PassUniforms, ambientColor) == 192);
static_assert(offsetof(PassUniforms, lightCounts) == 208);
static_assert(offsetof(PassUniforms, lightListParams) == 224);
static_assert(sizeof(PassUniforms) == 240);
static_assert(sizeof(MaterialUniforms) == 48);
static_assert(std::is_standard_layout_v<ObjectUniforms>);
static_assert(offsetof(ObjectUniforms, prevModel) == 0);
static_assert(sizeof(ObjectUniforms) == 64);
static_assert(std::is_standard_layout_v<EnvironmentUniforms>);
static_assert(offsetof(EnvironmentUniforms, skyParams) == 0);
static_assert(offsetof(EnvironmentUniforms, shAr) == 16);
static_assert(offsetof(EnvironmentUniforms, shCr) == 112);
static_assert(sizeof(EnvironmentUniforms) == 128);
static_assert(std::is_standard_layout_v<PrepassUniforms>);
static_assert(offsetof(PrepassUniforms, depthPyramidParams) == 0);
static_assert(sizeof(PrepassUniforms) == 16);
static_assert(std::is_standard_layout_v<PostProcessUniforms>);
static_assert(offsetof(PostProcessUniforms, exposureParams) == 0);
static_assert(offsetof(PostProcessUniforms, autoExposureParams) == 16);
static_assert(offsetof(PostProcessUniforms, tonemapParams) == 32);
static_assert(sizeof(PostProcessUniforms) == 48);
static_assert(std::is_standard_layout_v<DebugUniforms>);
static_assert(offsetof(DebugUniforms, depthParams) == 0);
static_assert(sizeof(DebugUniforms) == 16);

TEST_CASE("generated global uniform descriptors match the IDL") {
    const auto& descriptors = GlobalUniformBinder::kDescriptors;
    REQUIRE(descriptors.size() == 19);

    CHECK(descriptors[0].name == "u_global_view");
    CHECK(descriptors[0].type == UniformType::Mat4);
    CHECK(descriptors[0].num == 1);
    CHECK(descriptors[1].name == "u_global_invView");
    CHECK(descriptors[2].name == "u_global_proj");
    CHECK(descriptors[3].name == "u_global_invProj");
    CHECK(descriptors[4].name == "u_global_viewProj");
    CHECK(descriptors[5].name == "u_global_invViewProj");
    CHECK(descriptors[6].name == "u_global_nonJitteredViewProj");
    CHECK(descriptors[7].name == "u_global_nonJitteredInvViewProj");
    CHECK(descriptors[8].name == "u_global_prevViewProj");
    CHECK(descriptors[9].name == "u_global_prevInvViewProj");
    CHECK(descriptors[10].name == "u_global_screenSize");
    CHECK(descriptors[10].type == UniformType::Vec4);
    CHECK(descriptors[11].name == "u_global_cameraPos");
    CHECK(descriptors[12].name == "u_global_prevCameraPos");
    CHECK(descriptors[13].name == "u_global_taaJitter");
    CHECK(descriptors[14].name == "u_global_taaFrameInfo");
    CHECK(descriptors[15].name == "u_global_rtScale");
    CHECK(descriptors[16].name == "u_global_dynamicResolutionScale");
    CHECK(descriptors[17].name == "u_global_time");
    CHECK(descriptors[18].name == "u_global_frustumPlanes");
    CHECK(descriptors[18].type == UniformType::Vec4);
    CHECK(descriptors[18].num == 6);
}

TEST_CASE("generated pass and material descriptors match the IDL") {
    const auto& pass = PassUniformBinder::kDescriptors;
    REQUIRE(pass.size() == 9);
    CHECK(pass[0].name == "u_pass_shadowMatrix");
    CHECK(pass[0].type == UniformType::Mat4);
    CHECK(pass[1].name == "u_pass_shadowViewProj");
    CHECK(pass[1].type == UniformType::Mat4);
    CHECK(pass[2].name == "u_pass_shadowParams");
    CHECK(pass[2].type == UniformType::Vec4);
    CHECK(pass[3].name == "u_pass_shadowCascadeSplits");
    CHECK(pass[3].type == UniformType::Vec4);
    CHECK(pass[4].name == "u_pass_lightDirection");
    CHECK(pass[5].name == "u_pass_lightColor");
    CHECK(pass[6].name == "u_pass_ambientColor");
    CHECK(pass[7].name == "u_pass_lightCounts");
    CHECK(pass[8].name == "u_pass_lightListParams");
    CHECK(pass[8].type == UniformType::Vec4);

    const auto& material = MaterialUniformBinder::kDescriptors;
    REQUIRE(material.size() == 3);
    CHECK(material[0].name == "u_material_baseColorFactor");
    CHECK(material[1].name == "u_material_metallicRoughness");
    CHECK(material[2].name == "u_material_emissiveFactor");

    const auto& object = ObjectUniformBinder::kDescriptors;
    REQUIRE(object.size() == 1);
    CHECK(object[0].name == "u_object_prevModel");
    CHECK(object[0].type == UniformType::Mat4);
    CHECK(object[0].num == 1);

    const auto& environment = EnvironmentUniformBinder::kDescriptors;
    REQUIRE(environment.size() == 8);
    CHECK(environment[0].name == "u_environment_skyParams");
    CHECK(environment[1].name == "u_environment_shAr");
    CHECK(environment[2].name == "u_environment_shAg");
    CHECK(environment[3].name == "u_environment_shAb");
    CHECK(environment[4].name == "u_environment_shBr");
    CHECK(environment[5].name == "u_environment_shBg");
    CHECK(environment[6].name == "u_environment_shBb");
    CHECK(environment[7].name == "u_environment_shCr");
    CHECK(environment[7].type == UniformType::Vec4);

    const auto& prepass = PrepassUniformBinder::kDescriptors;
    REQUIRE(prepass.size() == 1);
    CHECK(prepass[0].name == "u_prepass_depthPyramidParams");
    CHECK(prepass[0].type == UniformType::Vec4);
    CHECK(prepass[0].num == 1);

    const auto& postProcess = PostProcessUniformBinder::kDescriptors;
    REQUIRE(postProcess.size() == 3);
    CHECK(postProcess[0].name == "u_postprocess_exposureParams");
    CHECK(postProcess[1].name == "u_postprocess_autoExposureParams");
    CHECK(postProcess[2].name == "u_postprocess_tonemapParams");
    CHECK(postProcess[2].type == UniformType::Vec4);

    const auto& debug = DebugUniformBinder::kDescriptors;
    REQUIRE(debug.size() == 1);
    CHECK(debug[0].name == "u_debug_depthParams");
    CHECK(debug[0].type == UniformType::Vec4);
}

TEST_CASE("generated shader declarations are in sync with C++ descriptors") {
    const std::filesystem::path path =
        std::filesystem::path(HBG_PROJECT_ROOT) / "shaders/include/generated/uniforms.sc";
    std::ifstream file(path);
    REQUIRE(file.good());

    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(text.find("uniform mat4 u_global_view;") != std::string::npos);
    CHECK(text.find("uniform mat4 u_global_nonJitteredViewProj;") != std::string::npos);
    CHECK(text.find("uniform mat4 u_global_prevInvViewProj;") != std::string::npos);
    CHECK(text.find("uniform vec4 u_global_taaJitter;") != std::string::npos);
    CHECK(text.find("uniform vec4 u_global_rtScale;") != std::string::npos);
    CHECK(text.find("uniform vec4 u_global_frustumPlanes[6];") != std::string::npos);
    CHECK(text.find("uniform mat4 u_pass_shadowMatrix;") != std::string::npos);
    CHECK(text.find("uniform mat4 u_pass_shadowViewProj;") != std::string::npos);
    CHECK(text.find("uniform vec4 u_pass_shadowCascadeSplits;") != std::string::npos);
    CHECK(text.find("uniform vec4 u_pass_lightDirection;") != std::string::npos);
    CHECK(text.find("uniform vec4 u_pass_lightCounts;") != std::string::npos);
    CHECK(text.find("uniform vec4 u_pass_lightListParams;") != std::string::npos);
    CHECK(text.find("uniform vec4 u_material_baseColorFactor;") != std::string::npos);
    CHECK(text.find("uniform mat4 u_object_prevModel;") != std::string::npos);
    CHECK(text.find("uniform vec4 u_environment_skyParams;") != std::string::npos);
    CHECK(text.find("uniform vec4 u_environment_shCr;") != std::string::npos);
    CHECK(text.find("uniform vec4 u_prepass_depthPyramidParams;") != std::string::npos);
    CHECK(text.find("uniform vec4 u_postprocess_exposureParams;") != std::string::npos);
    CHECK(text.find("uniform vec4 u_postprocess_tonemapParams;") != std::string::npos);
    CHECK(text.find("uniform vec4 u_debug_depthParams;") != std::string::npos);
}
