#include "render/material/shader_pass.h"
#include "render/passes/prepass_utils.h"
#include "render/pipeline/frame_settings.h"
#include "render/pipeline/pipeline_settings.h"

#include <doctest/doctest.h>

using namespace hbg;
using namespace hbg::render;

TEST_CASE("depth pyramid mip count covers the full 2x chain") {
    CHECK(depthPyramidMipCount(1, 1) == 1);
    CHECK(depthPyramidMipCount(2, 1) == 2);
    CHECK(depthPyramidMipCount(1280, 720) == 11);
    CHECK(depthPyramidMipCount(1920, 1080) == 11);
    CHECK(depthPyramidMipCount(1024, 1024) == 11);
    CHECK(depthPyramidMipCount(0, 0) == 1);

    CHECK(depthPyramidMipDimension(1280, 0) == 1280);
    CHECK(depthPyramidMipDimension(1280, 1) == 640);
    CHECK(depthPyramidMipDimension(720, 1) == 360);
    CHECK(depthPyramidMipDimension(1, 4) == 1);
    CHECK(depthPyramidMipDimension(0, 0) == 1);
}

TEST_CASE("depth prepass pass mask matches HDRP forward opaque tags") {
    asset::Material lit;
    lit.type = asset::MaterialType::Lit;
    lit.alphaMode = asset::AlphaMode::Opaque;
    CHECK((defaultShaderPassMask(lit) & shaderPassMask(ShaderPass::DepthOnly)) != 0);

    asset::Material unlit;
    unlit.type = asset::MaterialType::Unlit;
    unlit.alphaMode = asset::AlphaMode::Opaque;
    CHECK((defaultShaderPassMask(unlit) & shaderPassMask(ShaderPass::DepthForwardOnly)) != 0);

    CHECK((kDepthPrepassPassMask & shaderPassMask(ShaderPass::DepthOnly)) != 0);
    CHECK((kDepthPrepassPassMask & shaderPassMask(ShaderPass::DepthForwardOnly)) != 0);
    CHECK((kDepthPrepassPassMask & shaderPassMask(ShaderPass::Forward)) == 0);
    CHECK((kMotionVectorsPassMask & shaderPassMask(ShaderPass::MotionVectors)) != 0);
}

TEST_CASE("motion vectors require both pipeline and frame setting support") {
    PipelineSettings pipeline = PipelineSettings::defaults();
    CHECK(pipeline.supportMotionVectors);

    FrameSettings settings = FrameSettings::defaults(FrameSettingsRenderType::Camera);
    settings.sanitize(pipeline);
    CHECK(settings.isEnabled(FrameSettingsField::MotionVectors));
    CHECK(settings.isEnabled(FrameSettingsField::ObjectMotionVectors));

    pipeline.supportMotionVectors = false;
    settings.sanitize(pipeline);
    CHECK_FALSE(settings.isEnabled(FrameSettingsField::MotionVectors));
    CHECK_FALSE(settings.isEnabled(FrameSettingsField::ObjectMotionVectors));
}
