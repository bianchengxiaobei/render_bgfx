#include "render/passes/forward_renderer.h"

#include "core/log.h"
#include "gfx/encoder.h"
#include "gfx/state.h"
#include "render/passes/prepass_utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#ifndef HBG_SHADER_ROOT
#define HBG_SHADER_ROOT ""
#endif

namespace hbg::render {
namespace {

struct FullscreenVertex {
    float position[3];
};

constexpr FullscreenVertex kFullscreenVertices[] = {
    {{-1.0f, -1.0f, 0.0f}},
    {{3.0f, -1.0f, 0.0f}},
    {{-1.0f, 3.0f, 0.0f}},
};

constexpr uint16_t kInstanceStride = static_cast<uint16_t>(sizeof(float) * 16);

uint64_t transparentDepth(float distance) {
    const float clamped = std::clamp(distance, 0.0f, 100000.0f);
    return static_cast<uint64_t>(clamped * 100.0f);
}

uint64_t forwardState(const asset::Material& material, bool transparent, bool depthPrepass) {
    if (transparent) {
        return material.doubleSided ? (BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL |
                                       BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA)
                                    : gfx::state::kTransparent;
    }
    if (depthPrepass) {
        return material.doubleSided ? gfx::state::kForwardOpaqueNoCull : gfx::state::kForwardOpaque;
    }
    if (material.alphaMode == asset::AlphaMode::Mask) {
        return material.doubleSided ? (gfx::state::kAlphaTest & ~BGFX_STATE_CULL_CW) : gfx::state::kAlphaTest;
    }
    return material.doubleSided ? gfx::state::kOpaqueNoCull : gfx::state::kOpaque;
}

uint64_t prepassState(const asset::Material& material) {
    return material.doubleSided ? gfx::state::kPrepassNoCull : gfx::state::kPrepass;
}

uint64_t motionVectorState(const asset::Material& material) {
    return material.doubleSided ? gfx::state::kMotionVectorsNoCull : gfx::state::kMotionVectors;
}

uint64_t shadowState(const asset::Material& material) {
    return material.doubleSided ? (gfx::state::kShadowCaster & ~BGFX_STATE_CULL_CW) : gfx::state::kShadowCaster;
}

uint64_t debugMaterialState(const asset::Material& material) {
    return material.doubleSided ? gfx::state::kDebugMaterialNoCull : gfx::state::kDebugMaterial;
}

bool isMaterialDebug(DebugDisplayMode mode) {
    return mode == DebugDisplayMode::Albedo || mode == DebugDisplayMode::Roughness;
}

bool framebufferFormatSupported(gfx::TextureFormat format) {
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr) {
        return false;
    }
    const bgfx::TextureFormat::Enum native = gfx::toBgfx(format);
    return native != bgfx::TextureFormat::Count && (caps->formats[native] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
}

Camera fallbackCamera(const RenderScene& scene, float aspect) {
    const Bounds& bounds = scene.bounds;
    const Vec3 center = bounds.valid() ? (bounds.min + bounds.max) * 0.5f : Vec3(0.0f, 0.0f, 0.0f);
    const float radius = bounds.valid() ? std::max(length(bounds.max - bounds.min) * 0.5f, 0.1f) : 1.0f;
    const float fov = 0.78539816339f;
    const float distance = radius / std::tan(fov * 0.5f) * 1.15f;
    const Vec3 position = center + Vec3(0.0f, 0.0f, distance);
    const Mat4 view = lookAtRH(position, center, Vec3(0.0f, 1.0f, 0.0f));

    Camera camera;
    camera.world = inverse(view);
    camera.yfov = fov;
    camera.aspect = aspect;
    camera.nearClip = std::max(radius * 0.01f, 0.01f);
    camera.farClip = radius * 20.0f + distance;
    return camera;
}

} // namespace

bool ForwardRenderer::shadowsActive() const {
    return m_shadowsEnabled && m_frameSettings.isEnabled(FrameSettingsField::ShadowMaps);
}

bool ForwardRenderer::motionVectorsActive() const {
    return m_frameSettings.isEnabled(FrameSettingsField::MotionVectors);
}

bool ForwardRenderer::objectMotionVectorsActive() const {
    return motionVectorsActive() && m_frameSettings.isEnabled(FrameSettingsField::ObjectMotionVectors);
}

gfx::TextureFormat ForwardRenderer::selectHdrFormat() const {
    const gfx::TextureFormat requested = m_pipelineSettings.colorBufferFormat == ColorBufferFormat::R16G16B16A16F
                                             ? gfx::TextureFormat::RGBA16F
                                             : gfx::TextureFormat::RG11B10F;
    if (framebufferFormatSupported(requested)) {
        return requested;
    }
    const gfx::TextureFormat fallback =
        requested == gfx::TextureFormat::RG11B10F ? gfx::TextureFormat::RGBA16F : gfx::TextureFormat::RG11B10F;
    if (!m_formatWarningLogged) {
        HBG_LOGW("pipeline color buffer format {} is not framebuffer-capable; using {}",
                 gfx::textureFormatName(requested), gfx::textureFormatName(fallback));
        m_formatWarningLogged = true;
    }
    return fallback;
}

gfx::TextureFormat ForwardRenderer::selectDepthFormat() const {
    gfx::TextureFormat requested = gfx::TextureFormat::D24S8;
    switch (m_pipelineSettings.depthBufferFormat) {
    case DepthBufferFormat::Auto:
    case DepthBufferFormat::D24S8:
        requested = gfx::TextureFormat::D24S8;
        break;
    case DepthBufferFormat::D32F:
        requested = gfx::TextureFormat::D32F;
        break;
    case DepthBufferFormat::D16:
        requested = gfx::TextureFormat::D16;
        break;
    }
    if (framebufferFormatSupported(requested)) {
        return requested;
    }
    constexpr std::array<gfx::TextureFormat, 3> kFallbacks = {
        gfx::TextureFormat::D24S8,
        gfx::TextureFormat::D32F,
        gfx::TextureFormat::D16,
    };
    for (gfx::TextureFormat format : kFallbacks) {
        if (framebufferFormatSupported(format)) {
            if (!m_formatWarningLogged) {
                HBG_LOGW("pipeline depth buffer format {} is not framebuffer-capable; using {}",
                         gfx::textureFormatName(requested), gfx::textureFormatName(format));
                m_formatWarningLogged = true;
            }
            return format;
        }
    }
    return requested;
}

gfx::TextureFormat ForwardRenderer::selectNormalFormat() const {
    if (framebufferFormatSupported(gfx::TextureFormat::RGBA8)) {
        return gfx::TextureFormat::RGBA8;
    }
    HBG_LOGW("RGBA8 normal buffer is not framebuffer-capable; using RGBA16F");
    return gfx::TextureFormat::RGBA16F;
}

gfx::TextureFormat ForwardRenderer::selectMotionVectorFormat() const {
    if (framebufferFormatSupported(gfx::TextureFormat::RG16F)) {
        return gfx::TextureFormat::RG16F;
    }
    HBG_LOGW("RG16F motion-vector buffer is not framebuffer-capable; using RGBA16F");
    return gfx::TextureFormat::RGBA16F;
}

gfx::TextureFormat ForwardRenderer::selectDepthPyramidFormat() const {
    if (framebufferFormatSupported(gfx::TextureFormat::RG32F)) {
        return gfx::TextureFormat::RG32F;
    }
    HBG_LOGW("RG32F depth pyramid is not framebuffer-capable; using RGBA32F");
    return gfx::TextureFormat::RGBA32F;
}

void ForwardRenderer::setSettings(const PipelineSettings& pipelineSettings, const FrameSettings& frameSettings) {
    m_pipelineSettings = pipelineSettings;
    m_frameSettings = frameSettings;
    m_frameSettings.sanitize(m_pipelineSettings);
    m_formatWarningLogged = false;
    HBG_LOGI("frame settings: shadows={} opaque={} transparent={} postprocess={} motionVectors={}",
             m_frameSettings.isEnabled(FrameSettingsField::ShadowMaps),
             m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects),
             m_frameSettings.isEnabled(FrameSettingsField::TransparentObjects),
             m_frameSettings.isEnabled(FrameSettingsField::Postprocess),
             m_frameSettings.isEnabled(FrameSettingsField::MotionVectors));
}

void ForwardRenderer::setPostProcessSettings(const PostProcessSettings& settings) {
    m_postProcessSettings = settings;
    sanitizePostProcessSettings(m_postProcessSettings);
}

void ForwardRenderer::setRenderScale(float scale) {
    const float sanitized = std::clamp(scale, 0.1f, 1.0f);
    if (sanitized == m_renderScale) {
        return;
    }
    m_renderScale = sanitized;
    m_viewState.resetHistory();
}

bool ForwardRenderer::init(gfx::ResourceManager& resources, const RenderScene& scene, uint16_t width, uint16_t height) {
    m_resources = &resources;
    m_width = width;
    m_height = height;
    setSettings(PipelineSettings::defaults(), FrameSettings::defaults(FrameSettingsRenderType::Camera));

    if (!m_materialSystem.init(resources, scene)) {
        HBG_LOGE("forward renderer: material system initialization failed");
        return false;
    }
    m_materialSamplers = m_materialSystem.samplers();

    if (!m_shaderLibrary.init(resources, HBG_SHADER_ROOT)) {
        HBG_LOGE("forward renderer: shader library initialization failed (root='{}')", HBG_SHADER_ROOT);
        return false;
    }
    if (!m_globalBinder.init(resources) || !m_passBinder.init(resources) || !m_materialBinder.init(resources) ||
        !m_objectBinder.init(resources) || !m_prepassBinder.init(resources) || !m_environmentBinder.init(resources) ||
        !m_postProcessBinder.init(resources) || !m_debugBinder.init(resources)) {
        HBG_LOGE("forward renderer: uniform block initialization failed");
        return false;
    }
    if (!m_shadowManager.init(resources)) {
        HBG_LOGE("forward renderer: shadow manager initialization failed");
        return false;
    }

    gfx::TextureDesc lightDataDesc;
    lightDataDesc.width = 4;
    lightDataDesc.height = static_cast<uint16_t>(kMaxGpuLights);
    lightDataDesc.format = gfx::TextureFormat::RGBA32F;
    lightDataDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    lightDataDesc.name = "lighting.lightData";
    m_lightDataTexture = resources.createTexture(lightDataDesc);
    m_lightDataSampler = resources.createUniform({"s_lightData", gfx::UniformType::Sampler, 1});
    m_lightListSampler = resources.createUniform({"s_lightList", gfx::UniformType::Sampler, 1});
    m_shadowSampler = resources.createUniform({"s_shadowMap", gfx::UniformType::Sampler, 1});
    m_shadowPunctualSampler = resources.createUniform({"s_shadowMapPunctual", gfx::UniformType::Sampler, 1});
    m_shadowContextSampler = resources.createUniform({"s_shadowContext", gfx::UniformType::Sampler, 1});
    m_depthSourceSampler = resources.createUniform({"s_depthSource", gfx::UniformType::Sampler, 1});
    m_normalSampler = resources.createUniform({"s_normalBuffer", gfx::UniformType::Sampler, 1});
    m_motionVectorSampler = resources.createUniform({"s_motionVectors", gfx::UniformType::Sampler, 1});
    m_depthPyramidSampler = resources.createUniform({"s_depthPyramid", gfx::UniformType::Sampler, 1});
    m_depthDebugSampler = resources.createUniform({"s_depthDebug", gfx::UniformType::Sampler, 1});
    m_shadowAtlasSampler = resources.createUniform({"s_shadowAtlas", gfx::UniformType::Sampler, 1});
    m_environmentSampler = resources.createUniform({"s_environmentMap", gfx::UniformType::Sampler, 1});
    constexpr std::array<float, 4> kBlackPixel = {0.0f, 0.0f, 0.0f, 1.0f};
    gfx::TextureDesc environmentFallbackDesc;
    environmentFallbackDesc.width = 1;
    environmentFallbackDesc.height = 1;
    environmentFallbackDesc.format = gfx::TextureFormat::RGBA32F;
    environmentFallbackDesc.flags = BGFX_SAMPLER_UVW_CLAMP;
    environmentFallbackDesc.data = kBlackPixel.data();
    environmentFallbackDesc.dataSize = static_cast<uint32_t>(sizeof(kBlackPixel));
    environmentFallbackDesc.name = "sky.fallback";
    m_environmentFallback = resources.createTexture(environmentFallbackDesc);

    if (!m_lightDataTexture.valid() || !m_lightDataSampler.valid() || !m_lightListSampler.valid() ||
        !m_shadowSampler.valid() || !m_shadowPunctualSampler.valid() || !m_shadowContextSampler.valid() ||
        !m_depthSourceSampler.valid() || !m_normalSampler.valid() || !m_motionVectorSampler.valid() ||
        !m_depthPyramidSampler.valid() || !m_depthDebugSampler.valid() || !m_shadowAtlasSampler.valid() ||
        !m_environmentSampler.valid() || !m_environmentFallback.valid()) {
        HBG_LOGE("forward renderer: failed to create light/shadow/prepass/environment/postprocess bindings");
        return false;
    }

    gfx::VertexLayout fullscreenLayout;
    fullscreenLayout.position().end();
    gfx::BufferDesc fullscreenDesc;
    fullscreenDesc.type = gfx::BufferType::Vertex;
    fullscreenDesc.data = kFullscreenVertices;
    fullscreenDesc.size = sizeof(kFullscreenVertices);
    fullscreenDesc.count = 3;
    fullscreenDesc.layout = &fullscreenLayout;
    fullscreenDesc.name = "tonemap.fullscreen";
    m_fullscreenVertexBuffer = resources.createBuffer(fullscreenDesc);
    m_hdrSampler = resources.createUniform({"s_hdrColor", gfx::UniformType::Sampler, 1});
    if (!m_fullscreenVertexBuffer.valid() || !m_hdrSampler.valid()) {
        HBG_LOGE("forward renderer: failed to create tonemap resources");
        return false;
    }

    updateCamera(scene, width, height);
    ensureLightListTexture(m_viewState.renderWidth(), m_viewState.renderHeight());
    updateLights(scene);
    buildDrawLists(scene);
    m_valid = true;
    HBG_LOGI("forward renderer ready: {}x{}, opaque={} transparent={} shadow={} prepass={} motion={} wireframe={} "
             "draw items, render graph",
             width, height, m_opaqueDrawList.size(), m_transparentDrawList.size(), m_shadowDrawList.size(),
             m_prepassDrawList.size(), m_motionVectorDrawList.size(), m_wireframeDrawList.size());
    return true;
}

bool ForwardRenderer::setSky(const std::string& path, float intensity, float rotationDegrees) {
    if (m_resources == nullptr) {
        return false;
    }
    if (path.empty()) {
        m_skyManager.shutdown(*m_resources);
        updateEnvironmentUniforms();
        return true;
    }

    std::string error;
    if (!m_skyManager.load(*m_resources, path, intensity, rotationDegrees, &error)) {
        HBG_LOGE("sky load failed: {}", error);
        updateEnvironmentUniforms();
        return false;
    }
    updateEnvironmentUniforms();
    return true;
}

void ForwardRenderer::resize(uint16_t width, uint16_t height) {
    if (width == 0 || height == 0) {
        return;
    }
    m_width = width;
    m_height = height;
}

void ForwardRenderer::updateCamera(const RenderScene& scene, uint16_t width, uint16_t height) {
    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.7777778f;
    m_camera = scene.cameras.empty() ? fallbackCamera(scene, aspect) : scene.cameras.front();
    m_debugUniforms.depthParams = Vec4(m_camera.nearClip, m_camera.farClip, 0.0f, 0.0f);

    const bool homogeneousDepth = bgfx::getCaps() != nullptr && bgfx::getCaps()->homogeneousDepth;
    m_viewState.update(m_camera, width, height, m_renderScale, m_jitterEnabled, homogeneousDepth);
    m_cameraPosition = m_viewState.cameraPosition();
    m_cameraForward = m_viewState.cameraForward();
    updateGlobalUniforms();
}

void ForwardRenderer::updateGlobalUniforms() {
    const ViewConstants& constants = m_viewState.constants();
    m_globalUniforms.view = constants.view;
    m_globalUniforms.invView = constants.invView;
    m_globalUniforms.proj = constants.proj;
    m_globalUniforms.invProj = constants.invProj;
    m_globalUniforms.viewProj = constants.viewProj;
    m_globalUniforms.invViewProj = constants.invViewProj;
    m_globalUniforms.nonJitteredViewProj = constants.nonJitteredViewProj;
    m_globalUniforms.nonJitteredInvViewProj = constants.nonJitteredInvViewProj;
    m_globalUniforms.prevViewProj = constants.prevViewProj;
    m_globalUniforms.prevInvViewProj = constants.prevInvViewProj;
    m_globalUniforms.screenSize = m_viewState.screenSize();
    m_globalUniforms.cameraPos =
        Vec4(constants.worldSpaceCameraPos.x, constants.worldSpaceCameraPos.y, constants.worldSpaceCameraPos.z, 1.0f);
    m_globalUniforms.prevCameraPos = Vec4(constants.prevWorldSpaceCameraPos.x, constants.prevWorldSpaceCameraPos.y,
                                          constants.prevWorldSpaceCameraPos.z, 1.0f);
    m_globalUniforms.taaJitter = m_viewState.taaJitter();
    m_globalUniforms.taaFrameInfo = m_viewState.taaFrameInfo();
    m_globalUniforms.rtScale = m_viewState.rtScale();
    m_globalUniforms.dynamicResolutionScale = m_viewState.dynamicResolutionScale();
    m_globalUniforms.frustumPlanes = constants.frustumPlanes;
}

void ForwardRenderer::updateLights(const RenderScene& scene) {
    m_lightBuilder.build(scene, m_cameraPosition, m_cameraForward, m_camera.farClip);
    const GpuLightData& lightData = m_lightBuilder.data();

    m_passUniforms = {};
    m_passUniforms.lightDirection = Vec4(lightData.primaryDirection.x, lightData.primaryDirection.y,
                                         lightData.primaryDirection.z, lightData.primaryIntensity);
    m_passUniforms.lightColor =
        Vec4(lightData.primaryColor.x, lightData.primaryColor.y, lightData.primaryColor.z, 0.0f);
    m_passUniforms.ambientColor = Vec4(m_ambientColor.x, m_ambientColor.y, m_ambientColor.z, 1.0f);
    m_passUniforms.lightCounts =
        Vec4(static_cast<float>(lightData.lightCount), static_cast<float>(lightData.directionalCount),
             static_cast<float>(lightData.punctualCount), static_cast<float>(lightData.shadowedCount));

    bool shadowValid = false;
    if (shadowsActive()) {
        shadowValid = m_shadowManager.update(scene, m_viewState.constants(), m_camera, m_lightBuilder);
    } else {
        m_shadowManager.invalidate();
    }

    if (shadowValid) {
        const ShadowRequest& shadow = m_shadowManager.requests().front();
        m_passUniforms.shadowMatrix = shadow.worldToShadow;
        m_passUniforms.shadowViewProj = shadow.viewProj;
        m_passUniforms.shadowCascadeSplits = m_shadowManager.cascadeSplits();
        m_passUniforms.shadowParams =
            Vec4(1.0f, 0.0f, m_shadowManager.directionalRequestCount() > 0 ? 1.0f : 0.0f, 0.0f);
    } else {
        m_passUniforms.shadowMatrix = Mat4::identity();
        m_passUniforms.shadowViewProj = Mat4::identity();
        m_passUniforms.shadowCascadeSplits = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        m_passUniforms.shadowParams = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    m_passUniforms.lightCounts.w = shadowValid ? static_cast<float>(m_shadowManager.shadowedLightCount()) : 0.0f;
    if (m_frameIndex == 0) {
        HBG_LOGI("shadows: requests={} directional={} punctual={} splits=({}, {}, {}, {})",
                 m_shadowManager.requests().size(), m_shadowManager.directionalRequestCount(),
                 m_shadowManager.punctualRequestCount(), m_passUniforms.shadowCascadeSplits.x,
                 m_passUniforms.shadowCascadeSplits.y, m_passUniforms.shadowCascadeSplits.z,
                 m_passUniforms.shadowCascadeSplits.w);
    }

    uploadLightData();
    updateLightList();
    updateEnvironmentUniforms();
}

void ForwardRenderer::updateEnvironmentUniforms() {
    m_environmentUniforms = {};
    if (!m_skyManager.valid()) {
        m_environmentUniforms.skyParams = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    m_environmentUniforms = m_skyManager.uniforms();
    m_environmentUniforms.skyParams.z = 1.0f;
    m_environmentUniforms.skyParams.w = m_frameSettings.isEnabled(FrameSettingsField::SkyReflection) ? 1.0f : 0.0f;
}

void ForwardRenderer::updatePostProcessUniforms() {
    const bool postProcessEnabled = m_frameSettings.isEnabled(FrameSettingsField::Postprocess);
    const bool exposureEnabled = postProcessEnabled && m_frameSettings.isEnabled(FrameSettingsField::ExposureControl);
    const bool tonemappingEnabled = postProcessEnabled && m_frameSettings.isEnabled(FrameSettingsField::Tonemapping);
    const PostProcessSettings& settings = m_postProcessSettings;

    float exposureMode = 2.0f;
    float fixedExposure = 1.0f;
    if (exposureEnabled) {
        exposureMode = settings.exposureMode == ExposureMode::Automatic ? 1.0f : 0.0f;
        fixedExposure = settings.exposureMode == ExposureMode::Automatic ? 1.0f : fixedExposureMultiplier(settings);
    }

    const TonemappingMode tonemapping = tonemappingEnabled ? settings.tonemapping : TonemappingMode::None;
    m_postProcessUniforms.exposureParams = Vec4(
        exposureMode, fixedExposure, settings.compensation + settings.debugExposure, lensImperfectionExposureScale());
    m_postProcessUniforms.autoExposureParams = Vec4(settings.limitMin, settings.limitMax, 0.0f, 0.0f);
    m_postProcessUniforms.tonemapParams = Vec4(static_cast<float>(tonemapping), 0.0f, 0.0f, 0.0f);
}

void ForwardRenderer::uploadLightData() {
    if (m_resources == nullptr || !m_lightDataTexture.valid()) {
        return;
    }
    const GpuLightData& lightData = m_lightBuilder.data();
    m_resources->updateTexture(m_lightDataTexture, lightData.texels.data(),
                               static_cast<uint32_t>(sizeof(lightData.texels)), 4, static_cast<uint16_t>(kMaxGpuLights),
                               static_cast<uint16_t>(4 * sizeof(Vec4)));
}

bool ForwardRenderer::ensureLightListTexture(uint16_t renderWidth, uint16_t renderHeight) {
    if (m_resources == nullptr) {
        return false;
    }

    const uint16_t width = LightListBuilder::textureWidthFor(renderWidth);
    const uint16_t height = LightListBuilder::textureHeightFor(renderHeight);
    if (width == 0 || height == 0 || width > 16383u) {
        releaseLightListTexture();
        return false;
    }
    const gfx::Texture* currentTexture = m_resources->texture(m_lightListTexture);
    if (currentTexture != nullptr && currentTexture->desc().width == width && currentTexture->desc().height == height) {
        return true;
    }

    releaseLightListTexture();
    gfx::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = gfx::TextureFormat::R32U;
    desc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    desc.name = "lighting.lightList";
    m_lightListTexture = m_resources->createTexture(desc);
    if (!m_lightListTexture.valid()) {
        HBG_LOGE("forward renderer: failed to create light-list texture ({}x{})", width, height);
        m_lightListBuilder.clear();
        return false;
    }
    return true;
}

void ForwardRenderer::updateLightList() {
    m_lightListBuilder.build(m_lightBuilder.data(), m_viewState.constants().viewProj, m_viewState.renderWidth(),
                             m_viewState.renderHeight());
    const GpuLightListData& list = m_lightListBuilder.data();
    const gfx::Texture* lightListTexture = m_resources != nullptr ? m_resources->texture(m_lightListTexture) : nullptr;
    if (!list.enabled || lightListTexture == nullptr || list.width != lightListTexture->desc().width ||
        list.height != lightListTexture->desc().height) {
        m_lightListBuilder.clear();
        m_passUniforms.lightListParams = Vec4(static_cast<float>(kLightTileSize), 0.0f, 0.0f, 0.0f);
        return;
    }

    const uint16_t pitch = static_cast<uint16_t>(list.width * sizeof(uint32_t));
    if (!m_resources->updateTexture(m_lightListTexture, list.texels.data(),
                                    static_cast<uint32_t>(list.texels.size() * sizeof(uint32_t)), list.width,
                                    list.height, pitch)) {
        m_lightListBuilder.clear();
        m_passUniforms.lightListParams = Vec4(static_cast<float>(kLightTileSize), 0.0f, 0.0f, 0.0f);
        return;
    }

    m_passUniforms.lightListParams = Vec4(static_cast<float>(kLightTileSize), static_cast<float>(list.tileCountX),
                                          static_cast<float>(list.tileCountY), 1.0f);
    if (m_frameIndex == 0) {
        HBG_LOGI("light list: {}x{} tiles, {}x{} big tiles, max lights tile={}, big tile={}", list.tileCountX,
                 list.tileCountY, list.bigTileCountX, list.bigTileCountY, list.maxLightsPerTile,
                 list.maxLightsPerBigTile);
    }
}

void ForwardRenderer::releaseLightListTexture() {
    if (m_resources != nullptr && m_lightListTexture.valid()) {
        m_resources->destroyTexture(m_lightListTexture);
    }
    m_lightListTexture = {};
}

const DrawList& ForwardRenderer::forwardDrawList(bool transparent) const {
    return transparent ? m_transparentDrawList : m_opaqueDrawList;
}

void ForwardRenderer::buildDrawLists(const RenderScene& scene) {
    DrawListDesc desc;
    desc.passTags = kForwardPassMask;

    if (m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects)) {
        desc.queueRange = kRenderQueueRangeAllOpaque;
        desc.sortingCriteria = kOpaqueSortingCriteria;
        desc.frustumCull = true;
        m_opaqueDrawList.build(scene, m_viewState, desc);
    } else {
        m_opaqueDrawList.clear();
    }

    if (m_frameSettings.isEnabled(FrameSettingsField::TransparentObjects)) {
        desc.queueRange = kRenderQueueRangeAllTransparentWithLowRes;
        desc.sortingCriteria = kTransparentSortingCriteria;
        desc.frustumCull = true;
        m_transparentDrawList.build(scene, m_viewState, desc);
    } else {
        m_transparentDrawList.clear();
    }

    if (m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects)) {
        desc.passTags = kDepthPrepassPassMask;
        desc.queueRange = kRenderQueueRangeAllOpaque;
        desc.sortingCriteria = kOpaqueSortingCriteria;
        desc.frustumCull = true;
        m_prepassDrawList.build(scene, m_viewState, desc);
    } else {
        m_prepassDrawList.clear();
    }
    m_depthPrepassActive = !m_prepassDrawList.empty();

    if (objectMotionVectorsActive()) {
        desc.passTags = kMotionVectorsPassMask;
        desc.queueRange = kRenderQueueRangeAllOpaque;
        desc.sortingCriteria = kOpaqueSortingCriteria;
        desc.frustumCull = true;
        m_motionVectorDrawList.build(scene, m_viewState, desc);
    } else {
        m_motionVectorDrawList.clear();
    }

    if (shadowsActive() && m_shadowManager.valid()) {
        desc.passTags = shaderPassMask(ShaderPass::ShadowCaster);
        desc.queueRange = kRenderQueueRangeAllOpaque;
        desc.sortingCriteria = kOpaqueSortingCriteria;
        desc.frustumCull = false;
        m_shadowDrawList.build(scene, m_viewState, desc);
    } else {
        m_shadowDrawList.clear();
    }

    if (m_wireframe && (m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects) ||
                        m_frameSettings.isEnabled(FrameSettingsField::TransparentObjects))) {
        desc.passTags = kForwardPassMask;
        desc.queueRange = kRenderQueueRangeAll;
        desc.sortingCriteria = kOpaqueSortingCriteria;
        desc.frustumCull = true;
        m_wireframeDrawList.build(scene, m_viewState, desc);
    } else {
        m_wireframeDrawList.clear();
    }
}

void ForwardRenderer::addShadowPass(RenderGraph& graph, RenderGraphTextureHandle directionalShadow,
                                    RenderGraphTextureHandle punctualShadow, const RenderScene& scene) {
    if (!shadowsActive() || !m_shadowManager.valid()) {
        return;
    }
    const std::vector<ShadowRequest>& requests = m_shadowManager.requests();
    for (uint32_t index = 0; index < requests.size(); ++index) {
        const RenderGraphTextureHandle atlas =
            requests[index].atlas == ShadowAtlasType::Directional ? directionalShadow : punctualShadow;
        if (!atlas.valid()) {
            continue;
        }
        RenderGraphBuilder pass = graph.addRasterPass("Shadow " + std::to_string(index));
        pass.setDepthAttachment(atlas);
        pass.setRenderFunc([this, &scene, index](RenderGraphContext& context) {
            renderShadowPass(context.encoder(), context.viewId(), context.frameBuffer(), scene, index);
        });
    }
}

void ForwardRenderer::addShadowAtlasClearPass(RenderGraph& graph, RenderGraphTextureHandle atlas, uint16_t size) {
    if (!atlas.valid() || size == 0) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("Debug Shadow Atlas Clear");
    pass.setDepthAttachment(atlas);
    pass.allowCulling(false);
    pass.setRenderFunc([size](RenderGraphContext& context) {
        gfx::Encoder& encoder = context.encoder();
        const bgfx::ViewId view = context.viewId();
        encoder.setViewRect(view, 0, 0, size, size);
        encoder.setViewFrameBuffer(view, context.frameBuffer());
        encoder.setViewClear(view, BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
        encoder.touch(view);
    });
}

void ForwardRenderer::addClearPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                                   bool materialDebug) {
    RenderGraphBuilder pass = graph.addRasterPass("Scene Clear");
    pass.setColorAttachment(color);
    pass.setDepthAttachment(depth);
    pass.setRenderFunc([materialDebug](RenderGraphContext& context) {
        gfx::Encoder& encoder = context.encoder();
        const bgfx::ViewId view = context.viewId();
        encoder.setViewRect(view, 0, 0, context.width(), context.height());
        encoder.setViewFrameBuffer(view, context.frameBuffer());
        encoder.setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, materialDebug ? 0x000000ff : 0x101018ff, 1.0f,
                             0);
        encoder.touch(view);
    });
}

void ForwardRenderer::addPrepassPass(RenderGraph& graph, RenderGraphTextureHandle normal,
                                     RenderGraphTextureHandle depth, const RenderScene& scene) {
    if (!m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects) || m_prepassDrawList.empty()) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("Forward Depth Prepass");
    pass.setColorAttachment(normal);
    pass.readTexture(depth);
    pass.setDepthAttachment(depth);
    pass.allowCulling(false);
    pass.setRenderFunc([this, &scene](RenderGraphContext& context) {
        renderPrepass(context.encoder(), context.viewId(), context.frameBuffer(), scene);
    });
}

void ForwardRenderer::addObjectsMotionVectorsPass(RenderGraph& graph, RenderGraphTextureHandle motionVector,
                                                  RenderGraphTextureHandle depth, const RenderScene& scene) {
    if (!objectMotionVectorsActive() || m_motionVectorDrawList.empty()) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("Objects Motion Vectors");
    pass.setColorAttachment(motionVector);
    pass.readTexture(depth);
    pass.setDepthAttachment(depth);
    pass.allowCulling(false);
    pass.setRenderFunc([this, &scene](RenderGraphContext& context) {
        renderObjectsMotionVectors(context.encoder(), context.viewId(), context.frameBuffer(), scene);
    });
}

void ForwardRenderer::addCameraMotionVectorsPass(RenderGraph& graph, RenderGraphTextureHandle motionVector,
                                                 RenderGraphTextureHandle depth,
                                                 RenderGraphTextureHandle depthPyramid) {
    if (!motionVectorsActive() || !depthPyramid.valid()) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("Camera Motion Vectors");
    pass.setColorAttachment(motionVector);
    pass.setDepthAttachment(depth);
    const RenderGraphTextureHandle depthSource = depthPyramid;
    pass.readTexture(depthSource);
    pass.allowCulling(false);
    const bool cameraOnly = !objectMotionVectorsActive() || m_motionVectorDrawList.empty();
    pass.setRenderFunc([this, depthSource, cameraOnly](RenderGraphContext& context) {
        renderCameraMotionVectors(context.encoder(), context.viewId(), context.frameBuffer(),
                                  context.texture(depthSource), cameraOnly);
    });
}

void ForwardRenderer::addDepthPyramidPasses(RenderGraph& graph, RenderGraphTextureHandle depth,
                                            RenderGraphTextureHandle depthPyramid) {
    if (!m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects) || m_prepassDrawList.empty()) {
        return;
    }
    const uint16_t renderWidth = m_viewState.renderWidth();
    const uint16_t renderHeight = m_viewState.renderHeight();
    m_depthPyramidMipCount = depthPyramidMipCount(renderWidth, renderHeight);

    for (uint16_t mip = 0; mip < m_depthPyramidMipCount; ++mip) {
        const uint16_t targetWidth = depthPyramidMipDimension(renderWidth, static_cast<uint8_t>(mip));
        const uint16_t targetHeight = depthPyramidMipDimension(renderHeight, static_cast<uint8_t>(mip));

        if (mip == 0) {
            RenderGraphBuilder pass = graph.addRasterPass("Depth Pyramid Copy");
            pass.setColorAttachment(depthPyramid, 0, 0);
            pass.readTexture(depth);
            pass.allowCulling(false);
            pass.setRenderFunc(
                [this, depth, renderWidth, renderHeight, targetWidth, targetHeight](RenderGraphContext& context) {
                    renderDepthPyramidMip(context.encoder(), context.viewId(), context.frameBuffer(),
                                          context.texture(depth), renderWidth, renderHeight, targetWidth, targetHeight,
                                          0, true);
                });
            continue;
        }

        const uint8_t sourceMip = static_cast<uint8_t>(mip - 1);
        const uint16_t sourceWidth = depthPyramidMipDimension(renderWidth, sourceMip);
        const uint16_t sourceHeight = depthPyramidMipDimension(renderHeight, sourceMip);
        gfx::TextureDesc scratchDesc;
        scratchDesc.width = sourceWidth;
        scratchDesc.height = sourceHeight;
        scratchDesc.format = selectDepthPyramidFormat();
        scratchDesc.renderTarget = true;
        scratchDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        scratchDesc.name = "prepass.depthPyramidScratch";
        const RenderGraphTextureHandle scratch =
            graph.createTexture(scratchDesc, "Depth Pyramid Scratch " + std::to_string(mip));

        RenderGraphBuilder copyPass = graph.addRasterPass("Depth Pyramid Read " + std::to_string(mip));
        copyPass.setColorAttachment(scratch);
        copyPass.readTexture(depthPyramid);
        copyPass.allowCulling(false);
        copyPass.setRenderFunc([this, depthPyramid, sourceWidth, sourceHeight, sourceMip](RenderGraphContext& context) {
            renderDepthPyramidMip(context.encoder(), context.viewId(), context.frameBuffer(),
                                  context.texture(depthPyramid), sourceWidth, sourceHeight, sourceWidth, sourceHeight,
                                  sourceMip, true);
        });

        RenderGraphBuilder downsamplePass = graph.addRasterPass("Depth Pyramid Mip " + std::to_string(mip));
        downsamplePass.setColorAttachment(depthPyramid, 0, mip);
        downsamplePass.readTexture(scratch);
        downsamplePass.allowCulling(false);
        downsamplePass.setRenderFunc([this, scratch, sourceWidth, sourceHeight, targetWidth,
                                      targetHeight](RenderGraphContext& context) {
            renderDepthPyramidMip(context.encoder(), context.viewId(), context.frameBuffer(), context.texture(scratch),
                                  sourceWidth, sourceHeight, targetWidth, targetHeight, 0, false);
        });
    }
}

void ForwardRenderer::addSkyPass(RenderGraph& graph, RenderGraphTextureHandle color,
                                 RenderGraphTextureHandle environment) {
    if (!m_skyManager.valid() || !color.valid() || !environment.valid()) {
        return;
    }

    RenderGraphBuilder pass = graph.addRasterPass("Sky");
    pass.setColorAttachment(color);
    pass.readTexture(environment);
    pass.setRenderFunc([this, environment](RenderGraphContext& context) {
        renderSkyPass(context.encoder(), context.viewId(), context.frameBuffer(), context.texture(environment));
    });
}

void ForwardRenderer::addForwardPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                                     RenderGraphTextureHandle lightData, RenderGraphTextureHandle lightList,
                                     RenderGraphTextureHandle directionalShadow,
                                     RenderGraphTextureHandle punctualShadow, RenderGraphTextureHandle shadowContext,
                                     const RenderScene& scene, bool transparent) {
    const FrameSettingsField visibility =
        transparent ? FrameSettingsField::TransparentObjects : FrameSettingsField::OpaqueObjects;
    if (!m_frameSettings.isEnabled(visibility) || forwardDrawList(transparent).empty()) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass(transparent ? "Forward Transparent" : "Forward Opaque");
    pass.setColorAttachment(color);
    pass.readTexture(depth);
    pass.setDepthAttachment(depth);
    pass.readTexture(lightData);
    if (lightList.valid()) {
        pass.readTexture(lightList);
    }
    if (directionalShadow.valid()) {
        pass.readTexture(directionalShadow);
    }
    if (punctualShadow.valid()) {
        pass.readTexture(punctualShadow);
    }
    if (shadowContext.valid()) {
        pass.readTexture(shadowContext);
    }
    pass.setRenderFunc([this, &scene, transparent](RenderGraphContext& context) {
        renderForwardPass(context.encoder(), context.viewId(), context.frameBuffer(), scene, transparent);
    });
}

void ForwardRenderer::addWireframePass(RenderGraph& graph, RenderGraphTextureHandle color,
                                       RenderGraphTextureHandle depth, const RenderScene& scene) {
    if (!m_wireframe || m_wireframeDrawList.empty()) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("Wireframe");
    pass.setColorAttachment(color);
    pass.readTexture(depth);
    pass.setDepthAttachment(depth);
    pass.setRenderFunc([this, &scene](RenderGraphContext& context) {
        renderWireframePass(context.encoder(), context.viewId(), context.frameBuffer(), scene);
    });
}

void ForwardRenderer::addDebugMaterialPass(RenderGraph& graph, RenderGraphTextureHandle color,
                                           RenderGraphTextureHandle depth, const RenderScene& scene,
                                           DebugDisplayMode debugMode) {
    if (!isMaterialDebug(debugMode) || !m_depthPrepassActive || m_opaqueDrawList.empty()) {
        return;
    }
    RenderGraphBuilder pass =
        graph.addRasterPass(debugMode == DebugDisplayMode::Albedo ? "Debug Albedo" : "Debug Roughness");
    pass.setColorAttachment(color);
    pass.readTexture(depth);
    pass.setDepthAttachment(depth);
    pass.allowCulling(false);
    pass.setRenderFunc([this, &scene, debugMode](RenderGraphContext& context) {
        renderDebugMaterialPass(context.encoder(), context.viewId(), context.frameBuffer(), scene, debugMode);
    });
}

void ForwardRenderer::addPresentationPass(RenderGraph& graph, RenderGraphTextureHandle color,
                                          RenderGraphTextureHandle depth, RenderGraphTextureHandle lightData,
                                          RenderGraphTextureHandle lightList, RenderGraphTextureHandle normal,
                                          RenderGraphTextureHandle motionVector, RenderGraphTextureHandle depthPyramid,
                                          RenderGraphTextureHandle directionalShadow,
                                          RenderGraphTextureHandle punctualShadow, RenderGraphBufferHandle fullscreen,
                                          DebugDisplayMode debugMode) {
    const bool lightDataDebug = debugMode == DebugDisplayMode::LightData;
    const bool normalDebug = debugMode == DebugDisplayMode::Normal;
    const bool motionDebug = debugMode == DebugDisplayMode::MotionVectors;
    const bool depthDebug = debugMode == DebugDisplayMode::Depth;
    const bool pyramidDebug = debugMode == DebugDisplayMode::DepthPyramid;
    const bool clusterDebug = debugMode == DebugDisplayMode::LightCluster;
    const bool directionalAtlasDebug = debugMode == DebugDisplayMode::ShadowAtlasDirectional;
    const bool punctualAtlasDebug = debugMode == DebugDisplayMode::ShadowAtlasPunctual;
    const bool materialDebug = isMaterialDebug(debugMode);
    const char* passName = lightDataDebug          ? "Debug Light Data"
                           : normalDebug           ? "Debug Normal Buffer"
                           : motionDebug           ? "Debug Motion Vectors"
                           : depthDebug            ? "Debug Depth"
                           : pyramidDebug          ? "Debug Depth Pyramid"
                           : clusterDebug          ? "Debug Light Cluster"
                           : directionalAtlasDebug ? "Debug Directional Shadow Atlas"
                           : punctualAtlasDebug    ? "Debug Punctual Shadow Atlas"
                           : materialDebug         ? "Debug Material"
                                                   : "Tonemap";
    RenderGraphBuilder pass = graph.addRasterPass(passName);
    if (lightDataDebug) {
        pass.readTexture(lightData);
    } else if (normalDebug) {
        pass.readTexture(normal);
    } else if (motionDebug) {
        pass.readTexture(motionVector);
    } else if (depthDebug) {
        pass.readTexture(depth);
    } else if (pyramidDebug) {
        pass.readTexture(depthPyramid);
    } else if (clusterDebug) {
        pass.readTexture(lightData);
        pass.readTexture(lightList);
    } else if (directionalAtlasDebug) {
        pass.readTexture(directionalShadow);
    } else if (punctualAtlasDebug) {
        pass.readTexture(punctualShadow);
    } else {
        pass.readTexture(color);
    }
    pass.readBuffer(fullscreen);
    pass.writeBackbuffer();
    pass.setRenderFunc([this, color, depth, lightData, lightList, normal, motionVector, depthPyramid, directionalShadow,
                        punctualShadow, fullscreen, debugMode](RenderGraphContext& context) {
        renderPresentation(context.encoder(), context.viewId(), context.texture(color), context.texture(depth),
                           context.texture(lightData), context.texture(lightList), context.texture(normal),
                           context.texture(motionVector), context.texture(depthPyramid),
                           context.texture(directionalShadow), context.texture(punctualShadow),
                           context.buffer(fullscreen), debugMode);
    });
}

bool ForwardRenderer::drawShadowBatch(gfx::Encoder& encoder, bgfx::ViewId view, const DrawList& list,
                                      const DrawBatch& batch, const RenderScene& scene) {
    if (batch.itemCount < 2 || batch.firstItem + batch.itemCount > list.items().size()) {
        return false;
    }
    const DrawItem& first = list.items()[batch.firstItem];
    if (first.meshIndex >= m_materialSystem.meshes().size() || first.materialIndex >= scene.materials.size() ||
        first.indexCount == 0) {
        return false;
    }
    const GpuMesh& mesh = m_materialSystem.meshes()[first.meshIndex];
    if (!mesh.vertexBuffer.valid() || !mesh.indexBuffer.valid()) {
        return false;
    }
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::ShadowInstanced, 0, 0});
    if (!program.valid() || gfx::InstanceData::available(batch.itemCount, kInstanceStride) < batch.itemCount) {
        return false;
    }

    gfx::InstanceData instances;
    if (!instances.allocate(batch.itemCount, kInstanceStride)) {
        return false;
    }
    float* data = instances.floats();
    for (uint32_t index = 0; index < batch.itemCount; ++index) {
        const DrawItem& item = list.items()[batch.firstItem + index];
        std::memcpy(data + static_cast<std::size_t>(index) * 16, item.world.data(), 16 * sizeof(float));
    }

    encoder.setState(shadowState(scene.materials[first.materialIndex]));
    m_passBinder.upload(m_passUniforms);
    encoder.setVertexBuffer(0, mesh.vertexBuffer);
    encoder.setIndexBuffer(mesh.indexBuffer, first.indexOffset, first.indexCount);
    encoder.setInstanceDataBuffer(instances);
    encoder.submit(view, program);
    return true;
}

bool ForwardRenderer::drawForwardBatch(gfx::Encoder& encoder, bgfx::ViewId view, const DrawList& list,
                                       const DrawBatch& batch, const RenderScene& scene) {
    if (batch.itemCount < 2 || batch.firstItem + batch.itemCount > list.items().size()) {
        return false;
    }
    const DrawItem& first = list.items()[batch.firstItem];
    if (first.transparent || first.meshIndex >= m_materialSystem.meshes().size() ||
        first.materialIndex >= m_materialSystem.materials().size() || first.materialIndex >= scene.materials.size() ||
        first.indexCount == 0) {
        return false;
    }
    const GpuMesh& mesh = m_materialSystem.meshes()[first.meshIndex];
    const GpuMaterial& material = m_materialSystem.materials()[first.materialIndex];
    const asset::Material& sourceMaterial = scene.materials[first.materialIndex];
    if (!mesh.vertexBuffer.valid() || !mesh.indexBuffer.valid()) {
        return false;
    }
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::ForwardInstanced, material.variantKey, 0});
    if (!program.valid() || gfx::InstanceData::available(batch.itemCount, kInstanceStride) < batch.itemCount) {
        return false;
    }

    gfx::InstanceData instances;
    if (!instances.allocate(batch.itemCount, kInstanceStride)) {
        return false;
    }
    float* data = instances.floats();
    for (uint32_t index = 0; index < batch.itemCount; ++index) {
        const DrawItem& item = list.items()[batch.firstItem + index];
        std::memcpy(data + static_cast<std::size_t>(index) * 16, item.world.data(), 16 * sizeof(float));
    }

    encoder.setState(forwardState(sourceMaterial, false, m_depthPrepassActive));
    m_globalBinder.upload(m_globalUniforms);
    m_passBinder.upload(m_passUniforms);
    m_materialBinder.upload(material.uniforms);
    encoder.setTexture(0, m_materialSamplers.baseColorMap, material.baseColorTexture);
    encoder.setTexture(1, m_materialSamplers.metallicRoughnessMap, material.metallicRoughnessTexture);
    encoder.setTexture(2, m_materialSamplers.normalMap, material.normalTexture);
    encoder.setTexture(3, m_materialSamplers.occlusionMap, material.occlusionTexture);
    encoder.setTexture(4, m_materialSamplers.emissiveMap, material.emissiveTexture);
    encoder.setTexture(5, m_lightDataSampler, m_lightDataTexture, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    if (m_shadowManager.directionalTexture().valid()) {
        encoder.setTexture(6, m_shadowSampler, m_shadowManager.directionalTexture(),
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT | BGFX_SAMPLER_COMPARE_LEQUAL);
    }
    if (m_shadowManager.contextTexture().valid()) {
        encoder.setTexture(8, m_shadowContextSampler, m_shadowManager.contextTexture(),
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    }
    if (m_shadowManager.punctualTexture().valid()) {
        encoder.setTexture(9, m_shadowPunctualSampler, m_shadowManager.punctualTexture(),
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT | BGFX_SAMPLER_COMPARE_LEQUAL);
    }
    if (m_lightListTexture.valid()) {
        encoder.setTexture(7, m_lightListSampler, m_lightListTexture, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    }
    encoder.setVertexBuffer(0, mesh.vertexBuffer);
    encoder.setIndexBuffer(mesh.indexBuffer, first.indexOffset, first.indexCount);
    encoder.setInstanceDataBuffer(instances);
    encoder.submit(view, program);
    return true;
}

void ForwardRenderer::renderShadowPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                       const RenderScene& scene, uint32_t requestIndex) {
    if (!shadowsActive() || !m_shadowManager.valid() || requestIndex >= m_shadowManager.requests().size()) {
        return;
    }
    const gfx::ProgramHandle shadowProgram = m_shaderLibrary.program(shader::ProgramKey{shader::PassId::Shadow, 0, 0});
    if (!shadowProgram.valid()) {
        return;
    }

    const ShadowRequest& request = m_shadowManager.requests()[requestIndex];
    m_passUniforms.shadowMatrix = request.worldToShadow;
    m_passUniforms.shadowViewProj = request.viewProj;
    m_shadowManager.beginPass(encoder, view, frameBuffer, request);
    const std::vector<GpuMesh>& meshes = m_materialSystem.meshes();
    for (const DrawBatch& batch : m_shadowDrawList.batches()) {
        if (drawShadowBatch(encoder, view, m_shadowDrawList, batch, scene)) {
            continue;
        }
        for (uint32_t index = 0; index < batch.itemCount; ++index) {
            const DrawItem& item = m_shadowDrawList.items()[batch.firstItem + index];
            if (item.meshIndex >= meshes.size() || item.materialIndex >= m_materialSystem.materials().size()) {
                continue;
            }
            const GpuMesh& mesh = meshes[item.meshIndex];
            const asset::Material& material = scene.materials[item.materialIndex];
            encoder.setState(shadowState(material));
            m_passBinder.upload(m_passUniforms);
            encoder.setTransform(item.world.data());
            encoder.setVertexBuffer(0, mesh.vertexBuffer);
            encoder.setIndexBuffer(mesh.indexBuffer, item.indexOffset, item.indexCount);
            encoder.submit(view, shadowProgram);
        }
    }
}

void ForwardRenderer::renderPrepass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                    const RenderScene& scene) {
    if (!m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects) || m_prepassDrawList.empty()) {
        return;
    }

    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewTransform(view, m_viewState.constants().view.data(), m_viewState.constants().proj.data());
    encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
    encoder.touch(view);

    m_globalBinder.upload(m_globalUniforms);
    const std::vector<GpuMesh>& meshes = m_materialSystem.meshes();
    for (const DrawItem& item : m_prepassDrawList.items()) {
        if (item.meshIndex >= meshes.size() || item.materialIndex >= m_materialSystem.materials().size() ||
            item.materialIndex >= scene.materials.size()) {
            continue;
        }
        const GpuMesh& mesh = meshes[item.meshIndex];
        const GpuMaterial& material = m_materialSystem.materials()[item.materialIndex];
        const asset::Material& sourceMaterial = scene.materials[item.materialIndex];
        if (!mesh.vertexBuffer.valid() || !mesh.indexBuffer.valid()) {
            continue;
        }

        const gfx::ProgramHandle program =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::Prepass, material.variantKey, 0});
        if (!program.valid()) {
            if (m_frameIndex == 0) {
                HBG_LOGW("prepass program invalid for material variant {}", material.variantKey);
            }
            continue;
        }

        encoder.setState(prepassState(sourceMaterial));
        encoder.setTransform(item.world.data());
        m_materialBinder.upload(material.uniforms);
        encoder.setTexture(0, m_materialSamplers.baseColorMap, material.baseColorTexture);
        encoder.setTexture(1, m_materialSamplers.metallicRoughnessMap, material.metallicRoughnessTexture);
        encoder.setTexture(2, m_materialSamplers.normalMap, material.normalTexture);
        encoder.setTexture(3, m_materialSamplers.occlusionMap, material.occlusionTexture);
        encoder.setTexture(4, m_materialSamplers.emissiveMap, material.emissiveTexture);
        encoder.setVertexBuffer(0, mesh.vertexBuffer);
        encoder.setIndexBuffer(mesh.indexBuffer, item.indexOffset, item.indexCount);
        encoder.submit(view, program);
    }
}

void ForwardRenderer::renderObjectsMotionVectors(gfx::Encoder& encoder, bgfx::ViewId view,
                                                 gfx::FrameBufferHandle frameBuffer, const RenderScene& scene) {
    if (!objectMotionVectorsActive() || m_motionVectorDrawList.empty()) {
        return;
    }

    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewTransform(view, m_viewState.constants().view.data(), m_viewState.constants().proj.data());
    encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
    encoder.touch(view);

    m_globalBinder.upload(m_globalUniforms);
    const std::vector<GpuMesh>& meshes = m_materialSystem.meshes();
    for (const DrawItem& item : m_motionVectorDrawList.items()) {
        if (item.meshIndex >= meshes.size() || item.materialIndex >= scene.materials.size()) {
            continue;
        }
        const GpuMesh& mesh = meshes[item.meshIndex];
        const asset::Material& material = scene.materials[item.materialIndex];
        if (!mesh.vertexBuffer.valid() || !mesh.indexBuffer.valid()) {
            continue;
        }

        const gfx::ProgramHandle program =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::MotionVectors, 0, 0});
        if (!program.valid()) {
            continue;
        }

        encoder.setState(motionVectorState(material));
        encoder.setTransform(item.world.data());
        m_objectUniforms.prevModel = item.prevWorld;
        m_objectBinder.upload(m_objectUniforms);
        encoder.setVertexBuffer(0, mesh.vertexBuffer);
        encoder.setIndexBuffer(mesh.indexBuffer, item.indexOffset, item.indexCount);
        encoder.submit(view, program);
    }
}

void ForwardRenderer::renderCameraMotionVectors(gfx::Encoder& encoder, bgfx::ViewId view,
                                                gfx::FrameBufferHandle frameBuffer, gfx::TextureHandle depth,
                                                bool cameraOnly) {
    if (!motionVectorsActive()) {
        return;
    }
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::CameraMotionVectors, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !depth.valid()) {
        return;
    }

    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    if (cameraOnly) {
        encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
        encoder.touch(view);
    }
    encoder.setState(cameraOnly ? gfx::state::kCameraMotionVectorsFull : gfx::state::kCameraMotionVectors);
    m_globalBinder.upload(m_globalUniforms);
    encoder.setTexture(0, m_depthSourceSampler, depth, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderDepthPyramidMip(gfx::Encoder& encoder, bgfx::ViewId view,
                                            gfx::FrameBufferHandle frameBuffer, gfx::TextureHandle source,
                                            uint16_t sourceWidth, uint16_t sourceHeight, uint16_t width,
                                            uint16_t height, uint8_t sourceMip, bool copyDepth) {
    const shader::PassId pass = copyDepth ? shader::PassId::DepthPyramidCopy : shader::PassId::DepthPyramidDownsample;
    const gfx::ProgramHandle program = m_shaderLibrary.program(shader::ProgramKey{pass, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !source.valid() || sourceWidth == 0 ||
        sourceHeight == 0) {
        return;
    }
    m_prepassUniforms = {};
    m_prepassUniforms.depthPyramidParams =
        Vec4(1.0f / static_cast<float>(sourceWidth), 1.0f / static_cast<float>(sourceHeight), 0.0f, 0.0f);
    encoder.setViewRect(view, 0, 0, width, height);
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setState(gfx::state::kNoDepth);
    m_prepassBinder.upload(m_prepassUniforms);
    if (sourceMip == 0) {
        encoder.setTexture(0, m_depthSourceSampler, source, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    } else {
        encoder.setTextureMip(0, m_depthSourceSampler, source, sourceMip, 1,
                              BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    }
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderSkyPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                    gfx::TextureHandle environment) {
    const gfx::ProgramHandle program = m_shaderLibrary.program(shader::ProgramKey{shader::PassId::Sky, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !environment.valid()) {
        return;
    }

    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setState(gfx::state::kNoDepth);
    m_globalBinder.upload(m_globalUniforms);
    m_environmentBinder.upload(m_environmentUniforms);
    encoder.setTexture(10, m_environmentSampler, environment, BGFX_SAMPLER_V_CLAMP);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderForwardPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                        const RenderScene& scene, bool transparent) {
    const FrameSettingsField visibility =
        transparent ? FrameSettingsField::TransparentObjects : FrameSettingsField::OpaqueObjects;
    if (!m_frameSettings.isEnabled(visibility)) {
        return;
    }
    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewTransform(view, m_viewState.constants().view.data(), m_viewState.constants().proj.data());

    const auto drawItem = [&](const DrawItem& item) {
        const GpuMesh& gpuMesh = m_materialSystem.meshes()[item.meshIndex];
        const GpuMaterial& gpuMaterial = m_materialSystem.materials()[item.materialIndex];
        const asset::Material& material = scene.materials[item.materialIndex];

        const shader::ProgramKey programKey{shader::PassId::Forward, gpuMaterial.variantKey, 0};
        const gfx::ProgramHandle program = m_shaderLibrary.program(programKey);
        if (!program.valid()) {
            return;
        }

        encoder.setState(forwardState(material, item.transparent, m_depthPrepassActive));
        encoder.setTransform(item.world.data());
        m_globalBinder.upload(m_globalUniforms);
        m_passBinder.upload(m_passUniforms);
        m_environmentBinder.upload(m_environmentUniforms);
        m_materialBinder.upload(gpuMaterial.uniforms);
        encoder.setTexture(0, m_materialSamplers.baseColorMap, gpuMaterial.baseColorTexture);
        encoder.setTexture(1, m_materialSamplers.metallicRoughnessMap, gpuMaterial.metallicRoughnessTexture);
        encoder.setTexture(2, m_materialSamplers.normalMap, gpuMaterial.normalTexture);
        encoder.setTexture(3, m_materialSamplers.occlusionMap, gpuMaterial.occlusionTexture);
        encoder.setTexture(4, m_materialSamplers.emissiveMap, gpuMaterial.emissiveTexture);
        const gfx::TextureHandle environment = m_skyManager.valid() ? m_skyManager.texture() : m_environmentFallback;
        encoder.setTexture(10, m_environmentSampler, environment,
                           m_skyManager.valid() ? BGFX_SAMPLER_V_CLAMP : BGFX_SAMPLER_UVW_CLAMP);
        encoder.setTexture(5, m_lightDataSampler, m_lightDataTexture, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        if (m_shadowManager.directionalTexture().valid()) {
            encoder.setTexture(6, m_shadowSampler, m_shadowManager.directionalTexture(),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT | BGFX_SAMPLER_COMPARE_LEQUAL);
        }
        if (m_shadowManager.contextTexture().valid()) {
            encoder.setTexture(8, m_shadowContextSampler, m_shadowManager.contextTexture(),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        }
        if (m_shadowManager.punctualTexture().valid()) {
            encoder.setTexture(9, m_shadowPunctualSampler, m_shadowManager.punctualTexture(),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT | BGFX_SAMPLER_COMPARE_LEQUAL);
        }
        if (m_lightListTexture.valid()) {
            encoder.setTexture(7, m_lightListSampler, m_lightListTexture, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        }

        encoder.setVertexBuffer(0, gpuMesh.vertexBuffer);
        encoder.setIndexBuffer(gpuMesh.indexBuffer, item.indexOffset, item.indexCount);
        const uint32_t depth =
            item.transparent ? static_cast<uint32_t>(transparentDepth(item.distance) & 0xffffffffu) : 0;
        encoder.submit(view, program, depth);
    };

    const DrawList& list = forwardDrawList(transparent);
    if (transparent) {
        for (const DrawItem& item : list.items()) {
            drawItem(item);
        }
        return;
    }

    for (const DrawBatch& batch : list.batches()) {
        if (drawForwardBatch(encoder, view, list, batch, scene)) {
            continue;
        }
        for (uint32_t index = 0; index < batch.itemCount; ++index) {
            drawItem(list.items()[batch.firstItem + index]);
        }
    }
}

void ForwardRenderer::renderWireframePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                          const RenderScene& scene) {
    if (!m_wireframe) {
        return;
    }
    if (m_resources == nullptr || !m_materialSystem.ensureWireframeBuffers(*m_resources, scene)) {
        return;
    }
    const gfx::ProgramHandle program = m_shaderLibrary.program(shader::ProgramKey{shader::PassId::Wireframe, 0, 0});
    if (!program.valid()) {
        return;
    }

    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewTransform(view, m_viewState.constants().view.data(), m_viewState.constants().proj.data());

    const std::vector<GpuMesh>& meshes = m_materialSystem.meshes();
    for (const DrawItem& item : m_wireframeDrawList.items()) {
        if (item.meshIndex >= meshes.size() || item.materialIndex >= scene.materials.size()) {
            continue;
        }
        const FrameSettingsField visibility =
            item.transparent ? FrameSettingsField::TransparentObjects : FrameSettingsField::OpaqueObjects;
        if (!m_frameSettings.isEnabled(visibility)) {
            continue;
        }
        const GpuMesh& mesh = meshes[item.meshIndex];
        if (!mesh.wireframeIndexBuffer.valid() || mesh.wireframeIndexCount == 0) {
            continue;
        }

        encoder.setState(gfx::state::kWireframe);
        encoder.setTransform(item.world.data());
        m_globalBinder.upload(m_globalUniforms);
        encoder.setVertexBuffer(0, mesh.vertexBuffer);
        encoder.setIndexBuffer(mesh.wireframeIndexBuffer, 0, mesh.wireframeIndexCount);
        encoder.submit(view, program);
    }
}

void ForwardRenderer::renderDebugMaterialPass(gfx::Encoder& encoder, bgfx::ViewId view,
                                              gfx::FrameBufferHandle frameBuffer, const RenderScene& scene,
                                              DebugDisplayMode debugMode) {
    if (!isMaterialDebug(debugMode) || !m_depthPrepassActive) {
        return;
    }
    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewTransform(view, m_viewState.constants().view.data(), m_viewState.constants().proj.data());

    const shader::PassId debugPass =
        debugMode == DebugDisplayMode::Albedo ? shader::PassId::DebugAlbedo : shader::PassId::DebugRoughness;
    const std::vector<GpuMesh>& meshes = m_materialSystem.meshes();
    const std::vector<GpuMaterial>& gpuMaterials = m_materialSystem.materials();
    for (const DrawItem& item : m_opaqueDrawList.items()) {
        if (item.meshIndex >= meshes.size() || item.materialIndex >= gpuMaterials.size() ||
            item.materialIndex >= scene.materials.size()) {
            continue;
        }
        const GpuMesh& mesh = meshes[item.meshIndex];
        const GpuMaterial& gpuMaterial = gpuMaterials[item.materialIndex];
        const asset::Material& material = scene.materials[item.materialIndex];
        if (!mesh.vertexBuffer.valid() || !mesh.indexBuffer.valid() || item.indexCount == 0) {
            continue;
        }

        const gfx::ProgramHandle program =
            m_shaderLibrary.program(shader::ProgramKey{debugPass, gpuMaterial.variantKey, 0});
        if (!program.valid()) {
            continue;
        }

        encoder.setState(debugMaterialState(material));
        encoder.setTransform(item.world.data());
        m_globalBinder.upload(m_globalUniforms);
        m_materialBinder.upload(gpuMaterial.uniforms);
        encoder.setTexture(0, m_materialSamplers.baseColorMap, gpuMaterial.baseColorTexture);
        encoder.setTexture(1, m_materialSamplers.metallicRoughnessMap, gpuMaterial.metallicRoughnessTexture);
        encoder.setTexture(2, m_materialSamplers.normalMap, gpuMaterial.normalTexture);
        encoder.setTexture(3, m_materialSamplers.occlusionMap, gpuMaterial.occlusionTexture);
        encoder.setTexture(4, m_materialSamplers.emissiveMap, gpuMaterial.emissiveTexture);
        encoder.setVertexBuffer(0, mesh.vertexBuffer);
        encoder.setIndexBuffer(mesh.indexBuffer, item.indexOffset, item.indexCount);
        encoder.submit(view, program);
    }
}

void ForwardRenderer::renderPresentation(gfx::Encoder& encoder, bgfx::ViewId view, gfx::TextureHandle color,
                                         gfx::TextureHandle depth, gfx::TextureHandle lightData,
                                         gfx::TextureHandle lightList, gfx::TextureHandle normal,
                                         gfx::TextureHandle motionVector, gfx::TextureHandle depthPyramid,
                                         gfx::TextureHandle directionalShadow, gfx::TextureHandle punctualShadow,
                                         gfx::BufferHandle fullscreen, DebugDisplayMode debugMode) {
    gfx::ProgramHandle presentation = m_shaderLibrary.program(shader::ProgramKey{shader::PassId::Tonemap, 0, 0});
    switch (debugMode) {
    case DebugDisplayMode::LightData: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugLightData, 0, 0});
        if (debugProgram.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::Normal: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugNormal, 0, 0});
        if (debugProgram.valid() && normal.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::MotionVectors: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugMotionVectors, 0, 0});
        if (debugProgram.valid() && motionVector.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::Depth: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugDepth, 0, 0});
        if (debugProgram.valid() && depth.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::DepthPyramid: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugDepthPyramid, 0, 0});
        if (debugProgram.valid() && depthPyramid.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::LightCluster: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugLightCluster, 0, 0});
        if (debugProgram.valid() && lightData.valid() && lightList.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::ShadowAtlasDirectional: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugShadowAtlas, 0, 0});
        if (debugProgram.valid() && directionalShadow.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::ShadowAtlasPunctual: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugShadowAtlas, 0, 0});
        if (debugProgram.valid() && punctualShadow.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::Albedo: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugColor, 0, 0});
        if (debugProgram.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::Roughness: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugColor, 0, 0});
        if (debugProgram.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::None:
        break;
    }
    if (!presentation.valid()) {
        return;
    }

    encoder.setViewRect(view, 0, 0, m_width, m_height);
    encoder.setViewFrameBuffer(view, {});
    encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
    encoder.touch(view);
    encoder.setState(gfx::state::kFullscreen);
    m_globalBinder.upload(m_globalUniforms);
    m_passBinder.upload(m_passUniforms);
    if (debugMode == DebugDisplayMode::None) {
        updatePostProcessUniforms();
        m_postProcessBinder.upload(m_postProcessUniforms);
    } else if (debugMode == DebugDisplayMode::Depth || debugMode == DebugDisplayMode::DepthPyramid) {
        m_debugBinder.upload(m_debugUniforms);
    }
    switch (debugMode) {
    case DebugDisplayMode::LightData:
        encoder.setTexture(5, m_lightDataSampler, lightData, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::Normal:
        encoder.setTexture(0, m_normalSampler, normal, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::MotionVectors:
        encoder.setTexture(0, m_motionVectorSampler, motionVector, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::Depth:
        encoder.setTexture(0, m_depthDebugSampler, depth, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::DepthPyramid:
        encoder.setTextureMip(0, m_depthPyramidSampler, depthPyramid, 0, 1,
                              BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::LightCluster:
        encoder.setTexture(5, m_lightDataSampler, lightData, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        encoder.setTexture(7, m_lightListSampler, lightList, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::ShadowAtlasDirectional:
        encoder.setTexture(1, m_shadowAtlasSampler, directionalShadow, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::ShadowAtlasPunctual:
        encoder.setTexture(1, m_shadowAtlasSampler, punctualShadow, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::Albedo:
    case DebugDisplayMode::Roughness:
        encoder.setTexture(0, m_hdrSampler, color, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::None:
        encoder.setTexture(0, m_hdrSampler, color);
        break;
    }
    encoder.setVertexBuffer(0, fullscreen);
    encoder.submit(view, presentation);
}

void ForwardRenderer::render(const RenderScene& scene, uint16_t width, uint16_t height, float time) {
    if (!m_valid || width == 0 || height == 0) {
        return;
    }
    if (width != m_width || height != m_height) {
        resize(width, height);
    }

    updateCamera(scene, width, height);
    ensureLightListTexture(m_viewState.renderWidth(), m_viewState.renderHeight());
    updateLights(scene);
    const float deltaTime = m_hasLastRenderTime ? std::max(time - m_lastRenderTime, 0.0f) : 1.0f / 60.0f;
    m_lastRenderTime = time;
    m_hasLastRenderTime = true;
    m_globalUniforms.time = Vec4(time, deltaTime, static_cast<float>(m_frameIndex), 0.0f);
    buildDrawLists(scene);
    m_depthPrepassActive =
        m_depthPrepassActive && m_shaderLibrary.program(shader::ProgramKey{shader::PassId::Prepass, 0, 0}).valid();
    if (m_frameIndex == 0) {
        HBG_LOGI("draw lists: opaque={} ({} batches, {} filtered, {} culled), transparent={} ({} batches, "
                 "{} filtered, {} culled), prepass={}, motion={}, shadow={}, wireframe={}",
                 m_opaqueDrawList.size(), m_opaqueDrawList.batches().size(), m_opaqueDrawList.filteredCount(),
                 m_opaqueDrawList.culledCount(), m_transparentDrawList.size(), m_transparentDrawList.batches().size(),
                 m_transparentDrawList.filteredCount(), m_transparentDrawList.culledCount(), m_prepassDrawList.size(),
                 m_motionVectorDrawList.size(), m_shadowDrawList.size(), m_wireframeDrawList.size());
    }

    const uint16_t renderWidth = m_viewState.renderWidth();
    const uint16_t renderHeight = m_viewState.renderHeight();
    const bool opaqueObjects = m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects);
    const bool depthPrepass = opaqueObjects && !m_prepassDrawList.empty();
    const bool motionVectors = motionVectorsActive();

    m_renderGraph.beginFrame(renderWidth, renderHeight, m_frameIndex);

    gfx::TextureDesc colorDesc;
    colorDesc.width = renderWidth;
    colorDesc.height = renderHeight;
    colorDesc.format = selectHdrFormat();
    colorDesc.renderTarget = true;
    colorDesc.flags = BGFX_SAMPLER_UVW_CLAMP;
    colorDesc.name = "forward.hdrColor";
    const RenderGraphTextureHandle color = m_renderGraph.createTexture(colorDesc, "Forward HDR Color");

    gfx::TextureDesc depthDesc;
    depthDesc.width = renderWidth;
    depthDesc.height = renderHeight;
    depthDesc.format = selectDepthFormat();
    depthDesc.depthStencil = true;
    depthDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    depthDesc.name = "forward.depth";
    const RenderGraphTextureHandle depth = m_renderGraph.createTexture(depthDesc, "Forward Depth");

    RenderGraphTextureHandle normal;
    if (depthPrepass) {
        gfx::TextureDesc normalDesc;
        normalDesc.width = renderWidth;
        normalDesc.height = renderHeight;
        normalDesc.format = selectNormalFormat();
        normalDesc.renderTarget = true;
        normalDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        normalDesc.name = "prepass.normal";
        normal = m_renderGraph.createTexture(normalDesc, "Normal Buffer");
    }

    RenderGraphTextureHandle motionVector;
    if (motionVectors) {
        gfx::TextureDesc motionDesc;
        motionDesc.width = renderWidth;
        motionDesc.height = renderHeight;
        motionDesc.format = selectMotionVectorFormat();
        motionDesc.renderTarget = true;
        motionDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        motionDesc.name = "prepass.motionVectors";
        motionVector = m_renderGraph.createTexture(motionDesc, "Motion Vectors");
    }

    RenderGraphTextureHandle depthPyramid;
    if (depthPrepass) {
        gfx::TextureDesc pyramidDesc;
        pyramidDesc.width = renderWidth;
        pyramidDesc.height = renderHeight;
        pyramidDesc.mipCount = depthPyramidMipCount(renderWidth, renderHeight);
        pyramidDesc.format = selectDepthPyramidFormat();
        pyramidDesc.renderTarget = true;
        pyramidDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        pyramidDesc.name = "prepass.depthPyramid";
        depthPyramid = m_renderGraph.createTexture(pyramidDesc, "Depth Pyramid");
    }

    const RenderGraphTextureHandle lightData = m_renderGraph.importTexture(m_lightDataTexture, "Light Data");
    RenderGraphTextureHandle lightList;
    if (m_lightListTexture.valid()) {
        lightList = m_renderGraph.importTexture(m_lightListTexture, "Light List");
    }
    const RenderGraphTextureHandle directionalShadow =
        m_renderGraph.importTexture(m_shadowManager.directionalTexture(), "Directional Shadow Atlas");
    const RenderGraphTextureHandle punctualShadow =
        m_renderGraph.importTexture(m_shadowManager.punctualTexture(), "Punctual Shadow Atlas");
    const RenderGraphTextureHandle shadowContext =
        m_renderGraph.importTexture(m_shadowManager.contextTexture(), "Shadow Context");
    RenderGraphTextureHandle environment;
    RenderGraphTextureHandle skyEnvironment;
    if (m_skyManager.valid()) {
        environment = m_renderGraph.importTexture(m_skyManager.texture(), "Environment IBL");
        skyEnvironment = m_renderGraph.importTexture(m_skyManager.skyTexture(), "Sky Environment");
    }
    const RenderGraphBufferHandle fullscreen =
        m_renderGraph.importBuffer(m_fullscreenVertexBuffer, "Fullscreen Triangle");

    DebugDisplayMode debugMode = m_debugDisplayMode;
    const auto hasProgram = [this](shader::PassId pass) {
        return m_shaderLibrary.program(shader::ProgramKey{pass, 0, 0}).valid();
    };
    switch (debugMode) {
    case DebugDisplayMode::LightData:
        if (!hasProgram(shader::PassId::DebugLightData)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::Normal:
        if (!normal.valid() || !hasProgram(shader::PassId::DebugNormal)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::MotionVectors:
        if (!motionVector.valid() || !hasProgram(shader::PassId::DebugMotionVectors)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::Depth:
        if (!depth.valid() || !hasProgram(shader::PassId::DebugDepth)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::DepthPyramid:
        if (!depthPyramid.valid() || !hasProgram(shader::PassId::DebugDepthPyramid)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::LightCluster:
        if (!lightList.valid() || !hasProgram(shader::PassId::DebugLightCluster)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::ShadowAtlasDirectional:
        if (!shadowsActive() || !directionalShadow.valid() || m_shadowManager.directionalRequestCount() == 0 ||
            !hasProgram(shader::PassId::DebugShadowAtlas)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::ShadowAtlasPunctual:
        if (!shadowsActive() || !punctualShadow.valid() || m_shadowManager.punctualRequestCount() == 0 ||
            !hasProgram(shader::PassId::DebugShadowAtlas)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::Albedo:
        if (!m_depthPrepassActive || m_opaqueDrawList.empty() || !hasProgram(shader::PassId::DebugAlbedo) ||
            !hasProgram(shader::PassId::DebugColor)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::Roughness:
        if (!m_depthPrepassActive || m_opaqueDrawList.empty() || !hasProgram(shader::PassId::DebugRoughness) ||
            !hasProgram(shader::PassId::DebugColor)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::None:
        break;
    }

    const bool materialDebug = isMaterialDebug(debugMode);
    if (debugMode == DebugDisplayMode::ShadowAtlasDirectional) {
        addShadowAtlasClearPass(m_renderGraph, directionalShadow, m_shadowManager.directionalAtlasSize());
    } else if (debugMode == DebugDisplayMode::ShadowAtlasPunctual) {
        addShadowAtlasClearPass(m_renderGraph, punctualShadow, m_shadowManager.punctualAtlasSize());
    }
    addShadowPass(m_renderGraph, directionalShadow, punctualShadow, scene);
    addClearPass(m_renderGraph, color, depth, materialDebug);
    addPrepassPass(m_renderGraph, normal, depth, scene);
    addDepthPyramidPasses(m_renderGraph, depth, depthPyramid);
    addObjectsMotionVectorsPass(m_renderGraph, motionVector, depth, scene);
    addCameraMotionVectorsPass(m_renderGraph, motionVector, depth, depthPyramid);
    if (materialDebug) {
        addDebugMaterialPass(m_renderGraph, color, depth, scene, debugMode);
    } else {
        addSkyPass(m_renderGraph, color, skyEnvironment);
        addForwardPass(m_renderGraph, color, depth, lightData, lightList, directionalShadow, punctualShadow,
                       shadowContext, scene, false);
        addForwardPass(m_renderGraph, color, depth, lightData, lightList, directionalShadow, punctualShadow,
                       shadowContext, scene, true);
        addWireframePass(m_renderGraph, color, depth, scene);
    }
    addPresentationPass(m_renderGraph, color, depth, lightData, lightList, normal, motionVector, depthPyramid,
                        directionalShadow, punctualShadow, fullscreen, debugMode);

    gfx::Encoder encoder(*m_resources);
    if (!m_renderGraph.execute(encoder, *m_resources)) {
        HBG_LOGE("forward renderer: render graph execution failed: {}", m_renderGraph.lastError());
    } else {
        if (m_frameIndex == 0) {
            const RenderGraphDiagnostics& diagnostics = m_renderGraph.diagnostics();
            HBG_LOGI("render graph: {} recorded, {} live, {} culled; transient textures={}, pooled={}, framebuffers={}",
                     diagnostics.recordedPasses, diagnostics.livePasses, diagnostics.culledPasses,
                     diagnostics.transientTextures, diagnostics.pooledTextures, diagnostics.framebuffers);
        }
    }

    ++m_frameIndex;
}

void ForwardRenderer::shutdown(gfx::ResourceManager& resources) {
    m_valid = false;
    m_renderGraph.shutdown(resources);
    m_shaderLibrary.shutdown(resources);
    m_materialSystem.shutdown(resources);
    m_shadowManager.shutdown(resources);
    m_skyManager.shutdown(resources);

    if (m_environmentFallback.valid()) {
        resources.destroyTexture(m_environmentFallback);
        m_environmentFallback = {};
    }
    if (m_environmentSampler.valid()) {
        resources.destroyUniform(m_environmentSampler);
        m_environmentSampler = {};
    }
    if (m_lightDataTexture.valid()) {
        resources.destroyTexture(m_lightDataTexture);
        m_lightDataTexture = {};
    }
    if (m_lightDataSampler.valid()) {
        resources.destroyUniform(m_lightDataSampler);
        m_lightDataSampler = {};
    }
    releaseLightListTexture();
    if (m_lightListSampler.valid()) {
        resources.destroyUniform(m_lightListSampler);
        m_lightListSampler = {};
    }
    if (m_shadowSampler.valid()) {
        resources.destroyUniform(m_shadowSampler);
        m_shadowSampler = {};
    }
    if (m_shadowPunctualSampler.valid()) {
        resources.destroyUniform(m_shadowPunctualSampler);
        m_shadowPunctualSampler = {};
    }
    if (m_shadowContextSampler.valid()) {
        resources.destroyUniform(m_shadowContextSampler);
        m_shadowContextSampler = {};
    }
    if (m_depthSourceSampler.valid()) {
        resources.destroyUniform(m_depthSourceSampler);
        m_depthSourceSampler = {};
    }
    if (m_normalSampler.valid()) {
        resources.destroyUniform(m_normalSampler);
        m_normalSampler = {};
    }
    if (m_motionVectorSampler.valid()) {
        resources.destroyUniform(m_motionVectorSampler);
        m_motionVectorSampler = {};
    }
    if (m_depthPyramidSampler.valid()) {
        resources.destroyUniform(m_depthPyramidSampler);
        m_depthPyramidSampler = {};
    }
    if (m_depthDebugSampler.valid()) {
        resources.destroyUniform(m_depthDebugSampler);
        m_depthDebugSampler = {};
    }
    if (m_shadowAtlasSampler.valid()) {
        resources.destroyUniform(m_shadowAtlasSampler);
        m_shadowAtlasSampler = {};
    }
    if (m_fullscreenVertexBuffer.valid()) {
        resources.destroyBuffer(m_fullscreenVertexBuffer);
        m_fullscreenVertexBuffer = {};
    }
    if (m_hdrSampler.valid()) {
        resources.destroyUniform(m_hdrSampler);
        m_hdrSampler = {};
    }

    m_globalBinder.shutdown(resources);
    m_passBinder.shutdown(resources);
    m_materialBinder.shutdown(resources);
    m_objectBinder.shutdown(resources);
    m_prepassBinder.shutdown(resources);
    m_environmentBinder.shutdown(resources);
    m_postProcessBinder.shutdown(resources);
    m_debugBinder.shutdown(resources);
    m_opaqueDrawList.clear();
    m_transparentDrawList.clear();
    m_shadowDrawList.clear();
    m_prepassDrawList.clear();
    m_motionVectorDrawList.clear();
    m_wireframeDrawList.clear();
    m_resources = nullptr;
}

} // namespace hbg::render
