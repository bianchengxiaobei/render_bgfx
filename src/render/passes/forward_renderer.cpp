#include "render/passes/forward_renderer.h"

#include "asset/texture_loader.h"
#include "core/log.h"
#include "gfx/encoder.h"
#include "gfx/state.h"
#include "render/lighting/ltc_lut.h"
#include "render/passes/prepass_utils.h"
#include "render/vfx/hbg_vfx_backend.h"

#include "vfx/asset.h"
#include "vfx/runtime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

constexpr uint32_t kForwardInstanceFloats = 20u;
constexpr uint16_t kForwardInstanceStride = static_cast<uint16_t>(sizeof(float) * kForwardInstanceFloats);
constexpr uint16_t kShadowInstanceStride = static_cast<uint16_t>(sizeof(float) * 16);
constexpr uint32_t kLightmapShChannelCount = 3u;
constexpr uint32_t kFallbackMaxTextureSize = 16384u;
constexpr uint32_t kMaxTextureUploadWidth = 65535u / sizeof(uint32_t);
constexpr bgfx::ViewId kClusterStatsBlitView = 4093;


bool equivalentPath(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code error;
    std::filesystem::path normalizedLeft = std::filesystem::weakly_canonical(left, error);
    if (error) {
        normalizedLeft = std::filesystem::absolute(left, error).lexically_normal();
    }
    error.clear();
    std::filesystem::path normalizedRight = std::filesystem::weakly_canonical(right, error);
    if (error) {
        normalizedRight = std::filesystem::absolute(right, error).lexically_normal();
    }
    return normalizedLeft == normalizedRight;
}

::vfx::Mat4 toVfxMatrix(const Mat4& source) {
    ::vfx::Mat4 result;
    for (std::size_t index = 0; index < 16; ++index) {
        result.m[index] = source[index];
    }
    return result;
}

::vfx::Vec3 toVfxVec3(const Vec3& source) {
    return ::vfx::Vec3{source.x, source.y, source.z};
}

::vfx::PropertyValue toVfxProperty(const VfxPropertyValue& source) {
    ::vfx::PropertyValue result;
    switch (source.kind) {
    case VfxPropertyKind::Float:
        result.type = ::vfx::PropertyType::Float;
        break;
    case VfxPropertyKind::Vector2:
        result.type = ::vfx::PropertyType::Vector2;
        break;
    case VfxPropertyKind::Vector3:
        result.type = ::vfx::PropertyType::Vector3;
        break;
    case VfxPropertyKind::Vector4:
        result.type = ::vfx::PropertyType::Vector4;
        break;
    case VfxPropertyKind::Color:
        result.type = ::vfx::PropertyType::Color;
        break;
    }
    result.f = source.values;
    return result;
}

ForwardRenderer::ClusterStats summarizeClusterOffsets(const std::vector<uint32_t>& offsets) {
    ForwardRenderer::ClusterStats stats;
    if (offsets.size() < kLightCategoryCount) {
        return stats;
    }
    stats.valid = true;
    stats.clusters = static_cast<uint32_t>(offsets.size() / kLightCategoryCount);
    for (uint32_t cluster = 0; cluster < stats.clusters; ++cluster) {
        const uint32_t count = unpackClusterCount(offsets[cluster]);
        stats.activeClusters += count > 0 ? 1u : 0u;
        stats.maxLights = std::max(stats.maxLights, count);
    }
    return stats;
}

uint32_t backendMaxTextureSize() {
    const bgfx::Caps* caps = bgfx::getCaps();
    return caps != nullptr && caps->limits.maxTextureSize != 0 ? caps->limits.maxTextureSize
                                                               : kFallbackMaxTextureSize;
}

Vec4 lightListCapacityParams(uint32_t capacity) {
    const float listCapacity = static_cast<float>(capacity);
    return Vec4(static_cast<float>(lightListStrideFor(capacity)), listCapacity, listCapacity, listCapacity);
}

struct ObjectLightmapUniforms {
    Vec4 st{1.0f, 1.0f, 0.0f, 0.0f};
    Vec4 params{0.0f, 0.0f, 0.0f, 0.0f};
};

ObjectLightmapUniforms objectLightmapUniforms(const RenderScene& scene, uint32_t objectIndex) {
    ObjectLightmapUniforms uniforms;
    if (objectIndex >= scene.objects.size()) {
        return uniforms;
    }
    const RenderObject& object = scene.objects[objectIndex];
    if (!object.hasLightmap() || object.lightmapAtlas >= scene.lightmapAtlases.size()) {
        return uniforms;
    }
    uniforms.st = object.lightmapST;
    uniforms.params = Vec4(1.0f, static_cast<float>(object.lightmapAtlas * kLightmapShChannelCount), 0.0f, 0.0f);
    return uniforms;
}

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

uint64_t bakedGIState(const asset::Material& material) {
    const uint64_t base =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL | BGFX_STATE_MSAA;
    return material.doubleSided ? base : (base | BGFX_STATE_CULL_CW);
}

uint64_t prepassState(const asset::Material& material) {
    return material.doubleSided ? gfx::state::kPrepassNoCull : gfx::state::kPrepass;
}

uint64_t prepassDepthState(const asset::Material& material) {
    return material.doubleSided ? (gfx::state::kDepthOnly & ~BGFX_STATE_CULL_CW) : gfx::state::kDepthOnly;
}

uint64_t gbufferState(const asset::Material& material) {
    return material.doubleSided ? gfx::state::kGBufferNoCull : gfx::state::kGBuffer;
}

uint32_t litVariantStencilWrite(uint32_t variant) {
    const uint32_t ref = litVariantStencilRef(variant);
    return BGFX_STENCIL_TEST_ALWAYS | BGFX_STENCIL_FUNC_REF(ref) |
           BGFX_STENCIL_FUNC_RMASK(kLitVariantStencilMask) | BGFX_STENCIL_OP_FAIL_S_KEEP |
           BGFX_STENCIL_OP_FAIL_Z_KEEP | BGFX_STENCIL_OP_PASS_Z_REPLACE;
}

uint32_t litVariantStencilTest(uint32_t variant) {
    const uint32_t ref = litVariantStencilRef(variant);
    return BGFX_STENCIL_TEST_EQUAL | BGFX_STENCIL_FUNC_REF(ref) |
           BGFX_STENCIL_FUNC_RMASK(kLitVariantStencilMask) | BGFX_STENCIL_OP_FAIL_S_KEEP |
           BGFX_STENCIL_OP_FAIL_Z_KEEP | BGFX_STENCIL_OP_PASS_Z_KEEP;
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

bool isGbufferDebug(DebugDisplayMode mode) {
    return mode == DebugDisplayMode::Albedo || mode == DebugDisplayMode::Roughness ||
           mode == DebugDisplayMode::Metallic || mode == DebugDisplayMode::AmbientOcclusion ||
           mode == DebugDisplayMode::Emissive;
}

float gbufferDebugMode(DebugDisplayMode mode) {
    switch (mode) {
    case DebugDisplayMode::Albedo:
        return 0.0f;
    case DebugDisplayMode::Roughness:
        return 1.0f;
    case DebugDisplayMode::Metallic:
        return 2.0f;
    case DebugDisplayMode::AmbientOcclusion:
        return 3.0f;
    case DebugDisplayMode::Emissive:
        return 4.0f;
    default:
        return 0.0f;
    }
}

bool framebufferFormatSupported(gfx::TextureFormat format) {
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr) {
        return false;
    }
    const bgfx::TextureFormat::Enum native = gfx::toBgfx(format);
    return native != bgfx::TextureFormat::Count && (caps->formats[native] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
}

gfx::TextureHandle createSolidTexture(gfx::ResourceManager& resources, uint8_t red, uint8_t green, uint8_t blue,
                                      uint8_t alpha, const char* name) {
    const std::array<uint8_t, 4> pixel = {red, green, blue, alpha};
    gfx::TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.format = gfx::TextureFormat::RGBA8;
    desc.data = pixel.data();
    desc.dataSize = static_cast<uint32_t>(pixel.size());
    desc.flags = BGFX_SAMPLER_UVW_CLAMP;
    desc.name = name;
    return resources.createTexture(desc);
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

bool ForwardRenderer::contactShadowsActive() const {
    return shadowsActive() && m_shadowSettings.contactShadows.enabled &&
           m_frameSettings.resolveMsaaSamples(m_pipelineSettings) == MSAASamples::None &&
           m_frameSettings.isEnabled(FrameSettingsField::ContactShadows) &&
           m_pipelineSettings.supportContactShadows;
}

bool ForwardRenderer::motionVectorsActive() const {
    return m_frameSettings.isEnabled(FrameSettingsField::MotionVectors);
}

bool ForwardRenderer::objectMotionVectorsActive() const {
    return motionVectorsActive() && m_frameSettings.isEnabled(FrameSettingsField::ObjectMotionVectors);
}

const char* ForwardRenderer::deferredUnavailableReason() const {
    if (m_frameSettings.litShaderMode() != LitShaderMode::Deferred) {
        return "lit shader mode is forward";
    }
    if (m_pipelineSettings.supportedLitShaderMode == SupportedLitShaderMode::ForwardOnly) {
        return "pipeline supports forward only";
    }
    if (m_frameSettings.resolveMsaaSamples(m_pipelineSettings) != MSAASamples::None) {
        return "MSAA is not supported in deferred v1";
    }

    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr) {
        return "bgfx capabilities are unavailable";
    }
    if (caps->limits.maxFBAttachments < 6) {
        return "backend supports fewer than six framebuffer attachments";
    }
    if (selectDepthFormat() != gfx::TextureFormat::D24S8) {
        return "D24S8 depth-stencil is unavailable";
    }

    constexpr std::array<gfx::TextureFormat, 5> kGBufferFormats = {
        gfx::TextureFormat::RGBA8,
        gfx::TextureFormat::RGBA8,
        gfx::TextureFormat::RGBA8,
        gfx::TextureFormat::RGBA16F,
        gfx::TextureFormat::RGBA32F,
    };
    for (gfx::TextureFormat format : kGBufferFormats) {
        if (!framebufferFormatSupported(format)) {
            return "one or more GBuffer formats are not framebuffer-capable";
        }
    }
    return nullptr;
}

bool ForwardRenderer::ensureGBufferTextures(uint16_t renderWidth, uint16_t renderHeight) {
    if (m_resources == nullptr || renderWidth == 0 || renderHeight == 0) {
        return false;
    }
    m_deferredLightLayersActive = m_pipelineSettings.supportLightLayers;
    if (m_gbufferWidth == renderWidth && m_gbufferHeight == renderHeight &&
        std::all_of(m_gbufferTextures.begin(), m_gbufferTextures.end(),
                    [](gfx::TextureHandle texture) { return texture.valid(); })) {
        return true;
    }

    releaseGBufferTextures();
    constexpr std::array<gfx::TextureFormat, 5> kFormats = {
        gfx::TextureFormat::RGBA8,
        gfx::TextureFormat::RGBA8,
        gfx::TextureFormat::RGBA8,
        gfx::TextureFormat::RGBA16F,
        gfx::TextureFormat::RGBA32F,
    };
    constexpr std::array<const char*, 5> kNames = {
        "gbuffer.baseColor",
        "gbuffer.material",
        "gbuffer.normal",
        "gbuffer.emissive",
        "gbuffer.advanced",
    };
    for (std::size_t index = 0; index < m_gbufferTextures.size(); ++index) {
        gfx::TextureDesc desc;
        desc.width = renderWidth;
        desc.height = renderHeight;
        desc.format = kFormats[index];
        desc.renderTarget = true;
        desc.computeWrite = index == 1;
        desc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        desc.name = kNames[index];
        m_gbufferTextures[index] = m_resources->createTexture(desc);
        if (!m_gbufferTextures[index].valid()) {
            HBG_LOGE("forward renderer: failed to create GBuffer target '{}'", kNames[index]);
            releaseGBufferTextures();
            return false;
        }
    }
    m_deferredLightLayersActive = m_pipelineSettings.supportLightLayers;
    m_gbufferWidth = renderWidth;
    m_gbufferHeight = renderHeight;
    return true;
}

void ForwardRenderer::releaseGBufferTextures() {
    if (m_resources != nullptr) {
        for (gfx::TextureHandle& texture : m_gbufferTextures) {
            if (texture.valid()) {
                m_resources->destroyTexture(texture);
            }
            texture = {};
        }
    } else {
        m_gbufferTextures.fill({});
    }
    m_deferredLightLayersActive = false;
    m_gbufferWidth = 0;
    m_gbufferHeight = 0;
}

bool ForwardRenderer::ensureDecalTextures(uint16_t renderWidth, uint16_t renderHeight) {
    if (m_resources == nullptr || renderWidth == 0 || renderHeight == 0) {
        return false;
    }
    if (m_decalWidth == renderWidth && m_decalHeight == renderHeight &&
        std::all_of(m_decalTextures.begin(), m_decalTextures.end(),
                    [](gfx::TextureHandle texture) { return texture.valid(); })) {
        return true;
    }

    releaseDecalTextures();
    constexpr std::array<const char*, 4> kNames = {
        "decal.dbuffer0",
        "decal.dbuffer1",
        "decal.dbuffer2",
        "decal.dbuffer3",
    };
    for (std::size_t index = 0; index < m_decalTextures.size(); ++index) {
        gfx::TextureDesc desc;
        desc.width = renderWidth;
        desc.height = renderHeight;
        desc.format = gfx::TextureFormat::RGBA8;
        desc.renderTarget = true;
        desc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        desc.name = kNames[index];
        m_decalTextures[index] = m_resources->createTexture(desc);
        if (!m_decalTextures[index].valid()) {
            HBG_LOGE("forward renderer: failed to create DBuffer target '{}'", kNames[index]);
            releaseDecalTextures();
            return false;
        }
    }
    m_decalWidth = renderWidth;
    m_decalHeight = renderHeight;
    return true;
}

void ForwardRenderer::releaseDecalTextures() {
    if (m_resources != nullptr) {
        for (gfx::TextureHandle& texture : m_decalTextures) {
            if (texture.valid()) {
                m_resources->destroyTexture(texture);
            }
            texture = {};
        }
    } else {
        m_decalTextures.fill({});
    }
    m_decalWidth = 0;
    m_decalHeight = 0;
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
    if (framebufferFormatSupported(gfx::TextureFormat::RGBA32F)) {
        return gfx::TextureFormat::RGBA32F;
    }
    HBG_LOGW("RG32F depth pyramid is not framebuffer-capable; using RGBA32F");
    return gfx::TextureFormat::RGBA32F;
}

gfx::TextureFormat ForwardRenderer::selectSsaoFormat() const {
    if (framebufferFormatSupported(gfx::TextureFormat::R8)) {
        return gfx::TextureFormat::R8;
    }
    HBG_LOGW("R8 SSAO target is not framebuffer-capable; using RGBA8");
    return gfx::TextureFormat::RGBA8;
}

void ForwardRenderer::syncTaaJitter() {
    const TaaSettings& taa = m_postProcessChain.settings().taa;
    m_viewState.setJitterScale(taa.jitterScale);
    m_jitterEnabled = taa.enabled;
}

void ForwardRenderer::setGpuLightListEnabled(bool enabled) {
    if (m_gpuLightListEnabled == enabled) {
        return;
    }
    m_gpuLightListEnabled = enabled;
    m_pipelineSettings.supportGpuLightList = enabled;
    m_gpuLightListWarningLogged = false;
}

void ForwardRenderer::setGpuLightListBufferEnabled(bool enabled) {
    if (m_gpuLightListBufferEnabled == enabled) {
        return;
    }
    m_gpuLightListBufferEnabled = enabled;
    m_pipelineSettings.supportGpuLightListBuffers = enabled;
    m_gpuLightListWarningLogged = false;
}

ForwardRenderer::ForwardRenderer() = default;

ForwardRenderer::~ForwardRenderer() { releaseVfx(); }

void ForwardRenderer::setSettings(const PipelineSettings& pipelineSettings, const FrameSettings& frameSettings) {
    m_pipelineSettings = pipelineSettings;
    if (!m_pipelineSettings.supportVFX && m_vfxInitialized) {
        releaseVfx();
    }
    m_pipelineSettings.lightListCapacity = sanitizeLightListCapacity(m_pipelineSettings.lightListCapacity);
    m_cookieAtlas.configure(m_pipelineSettings.cookieAtlasSize, m_pipelineSettings.supportCookieAtlas);
    m_lightListCapacity = m_pipelineSettings.lightListCapacity;
    m_lightListTextureCapacity = m_lightListCapacity;
    m_lightListEffectiveCapacity = m_lightListCapacity;
    m_lightListCapacityWarningLogged = false;
    m_objectUniforms.lightLayerMask = Vec4(static_cast<float>(kRenderingLayersMask), 0.0f, 0.0f, 0.0f);
    m_objectUniforms.lightmapST = Vec4(1.0f, 1.0f, 0.0f, 0.0f);
    m_objectUniforms.lightmapParams = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    m_frameSettings = frameSettings;
    m_frameSettings.sanitize(m_pipelineSettings);
    setGpuLightListEnabled(m_pipelineSettings.supportGpuLightList);
    setGpuLightListBufferEnabled(m_pipelineSettings.supportGpuLightListBuffers);
    m_litVariantsEnabled = m_pipelineSettings.supportLitVariants;
    m_litVariantTilesEnabled = m_pipelineSettings.supportLitVariants && m_pipelineSettings.supportLitVariantTiles;
    m_litVariantsActive = false;
    m_litVariantTileActive = false;
    m_litVariantsWarningLogged = false;
    m_litVariantImsWarningLogged = false;
    m_litVariantTileWarningLogged = false;
    m_postProcessChain.setSettings(m_postProcessSettings, m_pipelineSettings, m_frameSettings);
    syncTaaJitter();
    m_formatWarningLogged = false;
    m_deferredFallbackLogged = false;
    m_decalFallbackLogged = false;
    m_areaEvsmBlurWarningLogged = false;
    HBG_LOGI("frame settings: lit={} shadows={} opaque={} transparent={} postprocess={} motionVectors={} "
             "lightLayers={}",
             m_frameSettings.litShaderMode() == LitShaderMode::Deferred ? "deferred" : "forward",
             m_frameSettings.isEnabled(FrameSettingsField::ShadowMaps),
             m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects),
             m_frameSettings.isEnabled(FrameSettingsField::TransparentObjects),
             m_frameSettings.isEnabled(FrameSettingsField::Postprocess),
             m_frameSettings.isEnabled(FrameSettingsField::MotionVectors),
             m_pipelineSettings.supportLightLayers ? "true" : "false");
}

bool ForwardRenderer::setShadowSettings(const ShadowSettings& settings) {
    m_shadowSettings = settings;
    sanitizeShadowSettings(m_shadowSettings);
    updateContactShadowUniforms();
    if (!m_valid) {
        return true;
    }
    return m_shadowManager.setSettings(m_shadowSettings);
}

void ForwardRenderer::updateContactShadowUniforms() {
    const ContactShadowSettings& contact = m_shadowSettings.contactShadows;
    m_contactUniforms.params = Vec4(contact.length, contact.distanceScaleFactor, contact.minDistance,
                                    contact.maxDistance);
    m_contactUniforms.params2 =
        Vec4(contact.fadeDistance, contact.fadeInDistance, contact.rayBias, contact.thicknessScale);
    m_contactUniforms.params3 =
        Vec4(static_cast<float>(contact.sampleCount), contact.opacity, contact.directionalOnly ? 1.0f : 0.0f,
             contact.halfResolution ? 1.0f : 0.0f);
    m_contactUniforms.params4 = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
}

void ForwardRenderer::setPostProcessSettings(const PostProcessSettings& settings) {
    m_postProcessSettings = settings;
    sanitizePostProcessSettings(m_postProcessSettings);
    m_postProcessChain.setSettings(m_postProcessSettings, m_pipelineSettings, m_frameSettings);
    syncTaaJitter();
}

void ForwardRenderer::setPostProcessProfile(const PostProcessProfile& profile) {
    m_postProcessSettings = profile.settings;
    sanitizePostProcessSettings(m_postProcessSettings);
    m_postProcessChain.setProfile(profile, m_pipelineSettings, m_frameSettings);
    syncTaaJitter();
}

void ForwardRenderer::setSsaoSettings(const SsaoSettings& settings) {
    const bool temporalChanged = m_ssaoSettings.temporalAccumulation != settings.temporalAccumulation;
    m_ssaoSettings = settings;
    sanitizeSsaoSettings(m_ssaoSettings);
    if (temporalChanged || !m_ssaoSettings.temporalAccumulation) {
        releaseSsaoHistory();
        m_ssaoTemporalFallbackLogged = false;
    }
}

void ForwardRenderer::setSsrSettings(const SsrSettings& settings) {
    const bool temporalChanged = m_ssrSettings.temporalAccumulation != settings.temporalAccumulation;
    m_ssrSettings = settings;
    sanitizeSsrSettings(m_ssrSettings);
    if (temporalChanged || !m_ssrSettings.temporalAccumulation) {
        releaseSsrHistory();
    }
}

void ForwardRenderer::setSsgiSettings(const SsgiSettings& settings) {
    const bool temporalChanged = m_ssgiSettings.temporalAccumulation != settings.temporalAccumulation;
    m_ssgiSettings = settings;
    sanitizeSsgiSettings(m_ssgiSettings);
    if (temporalChanged || !m_ssgiSettings.temporalAccumulation) {
        releaseSsgiHistory();
    }
}

void ForwardRenderer::setVolumetricFogSettings(const VolumetricFogSettings& settings) {
    m_volumetricFogSettings = settings;
    sanitizeVolumetricFogSettings(m_volumetricFogSettings);
    m_volumetricFogLogged = false;
    releaseVolumetricFogHistory();
    m_volumetricFogActive = false;
    m_volumetricFogSteps = 0;
    m_volumetricFogWidth = 0;
    m_volumetricFogHeight = 0;
    m_volumetricFogTemporalActive = false;
    m_volumetricFogPunctualLights = 0;
    m_volumetricFogLocalVolumes = 0;
    m_volumetricFogLocalMasks = 0;
}

void ForwardRenderer::setRenderScale(float scale) {
    const float sanitized = std::clamp(scale, 0.1f, 1.0f);
    if (sanitized == m_renderScale) {
        return;
    }
    HBG_LOGI("forward renderer render scale: {:.3f} -> {:.3f}", m_renderScale, sanitized);
    m_renderScale = sanitized;
    m_viewState.resetHistory();
    m_postProcessChain.resetHistory();
    releaseSsaoHistory();
    releaseSsrHistory();
    releaseSsgiHistory();
    releaseVolumetricFogHistory();
}

bool ForwardRenderer::init(gfx::ResourceManager& resources, const RenderScene& scene, uint16_t width, uint16_t height) {
    m_resources = &resources;
    m_width = width;
    m_height = height;
    m_postProcessWallTotalMs = 0.0;
    m_postProcessWallFrames = 0;
    setSettings(PipelineSettings::defaults(), FrameSettings::defaults(FrameSettingsRenderType::Camera));

    if (!m_materialSystem.init(resources, scene)) {
        HBG_LOGE("forward renderer: material system initialization failed");
        return false;
    }
    m_materialSamplers = m_materialSystem.samplers();
    m_lightmapSampler = resources.createUniform({"s_lightmapSH", gfx::UniformType::Sampler, 1});

    std::string lightmapError;
    if (!m_lightmapSet.load(resources, scene, &lightmapError)) {
        HBG_LOGW("lightmap: SH array upload failed: {}", lightmapError);
    } else if (m_lightmapSet.valid()) {
        HBG_LOGI("lightmap: SH array ready atlases={} layers={} size={}", m_lightmapSet.atlasCount(),
                 m_lightmapSet.layerCount(), m_lightmapSet.atlases().front().size);
    }

    if (!ensureLocalFogMaskTextures(resources, scene)) {
        HBG_LOGW("local fog: mask texture upload failed; masked volumes will use constant density");
    }

    if (!m_shaderLibrary.init(resources, HBG_SHADER_ROOT)) {
        HBG_LOGE("forward renderer: shader library initialization failed (root='{}')", HBG_SHADER_ROOT);
        return false;
    }
    if (!m_globalBinder.init(resources) || !m_passBinder.init(resources) || !m_clusterBinder.init(resources) ||
        !m_ssaoBinder.init(resources) || !m_ssrBinder.init(resources) || !m_ssgiBinder.init(resources) ||
        !m_decalBinder.init(resources) || !m_materialBinder.init(resources) ||
        !m_objectBinder.init(resources) ||
        !m_prepassBinder.init(resources) || !m_environmentBinder.init(resources) ||
        !m_probeBinder.init(resources) || !m_debugBinder.init(resources) ||
        !m_fogBinder.init(resources) || !m_contactBinder.init(resources)) {
        HBG_LOGE("forward renderer: uniform block initialization failed");
        return false;
    }
    sanitizeShadowSettings(m_shadowSettings);
    if (!m_shadowManager.init(resources, m_shadowSettings)) {
        HBG_LOGE("forward renderer: shadow manager initialization failed");
        return false;
    }

    gfx::TextureDesc lightDataDesc;
    lightDataDesc.width = static_cast<uint16_t>(kLightDataTextureWidth);
    lightDataDesc.height = static_cast<uint16_t>(kLightDataTextureRows);
    lightDataDesc.format = gfx::TextureFormat::RGBA32F;
    lightDataDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    lightDataDesc.name = "lighting.lightData";
    m_lightDataTexture = resources.createTexture(lightDataDesc);

    gfx::TextureDesc lightScreenBoundsDesc;
    lightScreenBoundsDesc.width = static_cast<uint16_t>(kMaxGpuLights * 2u);
    lightScreenBoundsDesc.height = 1;
    lightScreenBoundsDesc.format = gfx::TextureFormat::RGBA32F;
    lightScreenBoundsDesc.computeWrite = true;
    lightScreenBoundsDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    lightScreenBoundsDesc.name = "lighting.lightScreenBounds";
    m_lightScreenBoundsTexture = resources.createTexture(lightScreenBoundsDesc);

    gfx::TextureDesc lightViewRowsDesc;
    lightViewRowsDesc.width = 6;
    lightViewRowsDesc.height = 1;
    lightViewRowsDesc.format = gfx::TextureFormat::RGBA32F;
    lightViewRowsDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    lightViewRowsDesc.name = "lighting.lightViewRows";
    m_lightViewRowsTexture = resources.createTexture(lightViewRowsDesc);

    m_lightDataSampler = resources.createUniform({"s_lightData", gfx::UniformType::Sampler, 1});
    m_lightListSampler = resources.createUniform({"s_lightList", gfx::UniformType::Sampler, 1});
    m_lightScreenBoundsSampler = resources.createUniform({"s_lightScreenBounds", gfx::UniformType::Sampler, 1});
    m_lightBigTileSampler = resources.createUniform({"s_lightBigTileList", gfx::UniformType::Sampler, 1});
    m_lightViewRowsSampler = resources.createUniform({"s_lightViewRows", gfx::UniformType::Sampler, 1});
    m_shadowSampler = resources.createUniform({"s_shadowMap", gfx::UniformType::Sampler, 1});
    m_shadowPunctualSampler = resources.createUniform({"s_shadowMapPunctual", gfx::UniformType::Sampler, 1});
    m_shadowPointSampler = resources.createUniform({"s_shadowMapPoint", gfx::UniformType::Sampler, 1});
    m_shadowPointPunctualSampler =
        resources.createUniform({"s_shadowMapPointPunctual", gfx::UniformType::Sampler, 1});
    m_shadowCopySampler = resources.createUniform({"s_shadowCachedAtlas", gfx::UniformType::Sampler, 1});
    m_shadowCopyParamsUniform = resources.createUniform({"u_shadowCopyParams", gfx::UniformType::Vec4, 1});
    m_shadowMomentSlotUniform = resources.createUniform({"u_shadowMomentSlot", gfx::UniformType::Vec4, 1});
    m_shadowMomentParamsUniform = resources.createUniform({"u_shadowMomentParams", gfx::UniformType::Vec4, 1});
    m_shadowMomentDepthSampler = resources.createUniform({"s_shadowDepth", gfx::UniformType::Sampler, 1});
    m_shadowMomentSampler = resources.createUniform({"s_shadowMoment", gfx::UniformType::Sampler, 1});
    gfx::TextureDesc shadowMomentSatFallback;
    shadowMomentSatFallback.format = gfx::TextureFormat::RGBA32U;
    shadowMomentSatFallback.computeWrite = true;
    shadowMomentSatFallback.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    shadowMomentSatFallback.name = "shadow.momentSatFallback";
    m_shadowMomentSatFallback = resources.createTexture(shadowMomentSatFallback);
    m_depthSourceSampler = resources.createUniform({"s_depthSource", gfx::UniformType::Sampler, 1});
    m_contactShadowSampler = resources.createUniform({"s_contactShadow", gfx::UniformType::Sampler, 1});
    m_normalSampler = resources.createUniform({"s_normalBuffer", gfx::UniformType::Sampler, 1});
    m_motionVectorSampler = resources.createUniform({"s_motionVectors", gfx::UniformType::Sampler, 1});
    m_depthPyramidSampler = resources.createUniform({"s_depthPyramid", gfx::UniformType::Sampler, 1});
    m_depthDebugSampler = resources.createUniform({"s_depthDebug", gfx::UniformType::Sampler, 1});
    m_shadowAtlasSampler = resources.createUniform({"s_shadowAtlas", gfx::UniformType::Sampler, 1});
    m_fogSourceSampler = resources.createUniform({"s_fogSource", gfx::UniformType::Sampler, 1});
    m_fogHistorySampler = resources.createUniform({"s_fogHistory", gfx::UniformType::Sampler, 1});
    m_fogLowResSampler = resources.createUniform({"s_fogLowRes", gfx::UniformType::Sampler, 1});
    constexpr std::array<const char*, kMaxLocalFogMasks> kFogMaskSamplerNames = {
        "s_fogMask0",
        "s_fogMask1",
        "s_fogMask2",
        "s_fogMask3",
    };
    for (std::size_t index = 0; index < m_fogMaskSamplers.size(); ++index) {
        m_fogMaskSamplers[index] =
            resources.createUniform({kFogMaskSamplerNames[index], gfx::UniformType::Sampler, 1});
    }
    m_environmentSampler = resources.createUniform({"s_environmentMap", gfx::UniformType::Sampler, 1});
    m_probeArraySampler = resources.createUniform({"s_probeMaps", gfx::UniformType::Sampler, 1});
    m_cookieAtlasSampler = resources.createUniform({"s_cookieAtlas", gfx::UniformType::Sampler, 1});
    m_ssaoSampler = resources.createUniform({"s_ssao", gfx::UniformType::Sampler, 1});
    m_bakedIndirectSampler = resources.createUniform({"s_bakedIndirect", gfx::UniformType::Sampler, 1});
    m_ssaoInputSampler = resources.createUniform({"s_ssaoInput", gfx::UniformType::Sampler, 1});
    m_ssaoHistorySampler = resources.createUniform({"s_ssaoHistory", gfx::UniformType::Sampler, 1});
    m_ssrSampler = resources.createUniform({"s_ssr", gfx::UniformType::Sampler, 1});
    m_ssrHistorySampler = resources.createUniform({"s_ssrHistory", gfx::UniformType::Sampler, 1});
    m_ssgiSampler = resources.createUniform({"s_ssgi", gfx::UniformType::Sampler, 1});
    m_ssgiRawSampler = resources.createUniform({"s_ssgiRaw", gfx::UniformType::Sampler, 1});
    m_ssgiHistorySampler = resources.createUniform({"s_ssgiHistory", gfx::UniformType::Sampler, 1});
    m_ssgiHistoryDepthSampler =
        resources.createUniform({"s_ssgiHistoryDepth", gfx::UniformType::Sampler, 1});
    m_ssgiHistoryNormalSampler =
        resources.createUniform({"s_ssgiHistoryNormal", gfx::UniformType::Sampler, 1});
    m_decalAlbedoSampler = resources.createUniform({"s_decalAlbedo", gfx::UniformType::Sampler, 1});
    m_decalNormalSampler = resources.createUniform({"s_decalNormal", gfx::UniformType::Sampler, 1});
    m_decalMaskSampler = resources.createUniform({"s_decalMask", gfx::UniformType::Sampler, 1});
    constexpr std::array<const char*, 4> kDecalSamplerNames = {
        "s_decal0",
        "s_decal1",
        "s_decal2",
        "s_decal3",
    };
    for (std::size_t index = 0; index < m_decalSamplers.size(); ++index) {
        m_decalSamplers[index] = resources.createUniform({kDecalSamplerNames[index], gfx::UniformType::Sampler, 1});
    }
    constexpr std::array<const char*, 5> kGBufferSamplerNames = {
        "s_gbuffer0",
        "s_gbuffer1",
        "s_gbuffer2",
        "s_gbuffer3",
        "s_gbuffer4",
    };
    for (std::size_t index = 0; index < m_gbufferSamplers.size(); ++index) {
        m_gbufferSamplers[index] = resources.createUniform({kGBufferSamplerNames[index], gfx::UniformType::Sampler, 1});
    }
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

    constexpr std::array<float, 4> kWhiteFloatPixel = {1.0f, 1.0f, 1.0f, 1.0f};
    gfx::TextureDesc probeArrayFallbackDesc;
    probeArrayFallbackDesc.width = 1;
    probeArrayFallbackDesc.height = 1;
    probeArrayFallbackDesc.layers = 1;
    probeArrayFallbackDesc.format = gfx::TextureFormat::RGBA32F;
    probeArrayFallbackDesc.flags = BGFX_SAMPLER_UVW_CLAMP;
    probeArrayFallbackDesc.data = kWhiteFloatPixel.data();
    probeArrayFallbackDesc.dataSize = static_cast<uint32_t>(sizeof(kWhiteFloatPixel));
    probeArrayFallbackDesc.name = "probe.fallbackArray";
    m_probeArrayFallback = resources.createTexture(probeArrayFallbackDesc);
    m_cookieAtlasFallback = createSolidTexture(resources, 255, 255, 255, 255, "cookieAtlas.fallback");

    std::string probeError;
    if (!m_probeManager.load(resources, scene.probes, scene.images, &probeError)) {
        HBG_LOGW("probes: static reflection probe upload failed: {}", probeError);
    }

    constexpr std::array<uint8_t, 4> kWhitePixel = {255, 255, 255, 255};
    gfx::TextureDesc ssaoFallbackDesc;
    ssaoFallbackDesc.width = 1;
    ssaoFallbackDesc.height = 1;
    ssaoFallbackDesc.format = gfx::TextureFormat::RGBA8;
    ssaoFallbackDesc.flags = BGFX_SAMPLER_UVW_CLAMP;
    ssaoFallbackDesc.data = kWhitePixel.data();
    ssaoFallbackDesc.dataSize = static_cast<uint32_t>(sizeof(kWhitePixel));
    ssaoFallbackDesc.name = "ssao.fallback";
    m_ssaoFallback = resources.createTexture(ssaoFallbackDesc);
    m_bakedIndirectFallback = createSolidTexture(resources, 0, 0, 0, 0, "bakedIndirect.fallback");
    m_contactShadowFallback = createSolidTexture(resources, 0, 0, 0, 0, "contactShadow.fallback");

    m_decalFallbackTextures[0] = createSolidTexture(resources, 255, 255, 255, 255, "decal.fallback.albedo");
    m_decalFallbackTextures[1] = createSolidTexture(resources, 128, 128, 255, 255, "decal.fallback.normal");
    m_decalFallbackTextures[2] = createSolidTexture(resources, 255, 255, 255, 255, "decal.fallback.mask");

    if (!m_lightDataTexture.valid() || !m_lightDataSampler.valid() || !m_lightmapSampler.valid() ||
        !m_lightListSampler.valid() ||
        !m_lightScreenBoundsSampler.valid() || !m_lightBigTileSampler.valid() ||
        !m_lightViewRowsSampler.valid() || !m_lightViewRowsTexture.valid() ||
        !m_shadowSampler.valid() || !m_shadowPunctualSampler.valid() || !m_shadowPointSampler.valid() ||
        !m_shadowPointPunctualSampler.valid() || !m_shadowCopySampler.valid() ||
        !m_shadowCopyParamsUniform.valid() || !m_shadowMomentSlotUniform.valid() ||
        !m_shadowMomentParamsUniform.valid() || !m_shadowMomentDepthSampler.valid() ||
        !m_shadowMomentSampler.valid() ||
        !m_shadowMomentSatFallback.valid() ||
        !m_depthSourceSampler.valid() || !m_normalSampler.valid() || !m_motionVectorSampler.valid() ||
        !m_depthPyramidSampler.valid() || !m_depthDebugSampler.valid() || !m_shadowAtlasSampler.valid() ||
        !m_fogSourceSampler.valid() || !m_fogHistorySampler.valid() || !m_fogLowResSampler.valid() ||
        !std::all_of(m_fogMaskSamplers.begin(), m_fogMaskSamplers.end(),
                     [](gfx::UniformHandle uniform) { return uniform.valid(); }) ||
        !m_environmentSampler.valid() || !m_environmentFallback.valid() ||
        !m_probeArraySampler.valid() || !m_probeArrayFallback.valid() ||
        !m_cookieAtlasSampler.valid() || !m_cookieAtlasFallback.valid() || !m_ssaoSampler.valid() ||
        !m_ssaoInputSampler.valid() || !m_ssaoHistorySampler.valid() || !m_ssaoFallback.valid() ||
        !m_bakedIndirectSampler.valid() || !m_bakedIndirectFallback.valid() ||
        !m_contactShadowFallback.valid() ||
        !m_contactShadowSampler.valid() || !m_ssrSampler.valid() || !m_ssrHistorySampler.valid() ||
        !m_ssgiSampler.valid() || !m_ssgiRawSampler.valid() || !m_ssgiHistorySampler.valid() ||
        !m_ssgiHistoryDepthSampler.valid() || !m_ssgiHistoryNormalSampler.valid() ||
        !m_decalAlbedoSampler.valid() ||
        !m_decalNormalSampler.valid() || !m_decalMaskSampler.valid() ||
        !std::all_of(m_decalSamplers.begin(), m_decalSamplers.end(),
                     [](gfx::UniformHandle uniform) { return uniform.valid(); }) ||
        !std::all_of(m_decalFallbackTextures.begin(), m_decalFallbackTextures.end(),
                     [](gfx::TextureHandle texture) { return texture.valid(); }) ||
        !std::all_of(m_gbufferSamplers.begin(), m_gbufferSamplers.end(),
                     [](gfx::UniformHandle uniform) { return uniform.valid(); })) {
        HBG_LOGE("forward renderer: failed to create light/shadow/prepass/environment/postprocess bindings");
        return false;
    }
    if (!m_lightScreenBoundsTexture.valid()) {
        HBG_LOGW("gpu light list: failed to create screen-bounds texture; the CPU list remains active");
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
    if (!m_postProcessChain.init(resources, m_shaderLibrary, width, height)) {
        HBG_LOGE("forward renderer: post-process chain initialization failed");
        return false;
    }
    m_postProcessChain.setSettings(m_postProcessSettings, m_pipelineSettings, m_frameSettings);

    sanitizeSsaoSettings(m_ssaoSettings);
    sanitizeSsrSettings(m_ssrSettings);
    sanitizeSsgiSettings(m_ssgiSettings);
    updateCamera(scene, width, height);
    ensureLightListTexture(m_viewState.renderWidth(), m_viewState.renderHeight());
    if (m_gpuLightListEnabled) {
        ensureLightBigTileTexture(m_viewState.renderWidth(), m_viewState.renderHeight());
    }
    ensureClusterLightListTextures(m_viewState.renderWidth(), m_viewState.renderHeight());
    if (m_gpuLightListEnabled && m_gpuLightListBufferEnabled) {
        ensureLightListBuffers(m_viewState.renderWidth(), m_viewState.renderHeight());
    }
    ensureLitVariantTileResources(m_viewState.renderWidth(), m_viewState.renderHeight());
    updateLights(scene);
    buildDrawLists(scene);
    m_valid = true;
    HBG_LOGI("forward renderer ready: {}x{}, opaque={} forwardOnly={} bakedGI={} transparent={} shadow={} prepass={} "
             "gbuffer={} motion={} wireframe={} draw items, render graph",
             width, height, m_opaqueDrawList.size(), m_forwardOpaqueDrawList.size(), m_bakedGIDrawList.size(),
             m_transparentDrawList.size(), m_shadowDrawList.size(), m_prepassDrawList.size(),
             m_gbufferDrawList.size(), m_motionVectorDrawList.size(), m_wireframeDrawList.size());
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
    if (width != m_width || height != m_height) {
        HBG_LOGI("forward renderer resize: {}x{} -> {}x{}", m_width, m_height, width, height);
        if (m_resources != nullptr) {
            m_renderGraph.shutdown(*m_resources);
        }
        releaseGBufferTextures();
        releaseDecalTextures();
        m_viewState.resetHistory();
        releaseSsaoHistory();
        releaseSsrHistory();
        releaseSsgiHistory();
        releaseVolumetricFogHistory();
    }
    m_width = width;
    m_height = height;
    m_postProcessChain.resize(width, height);
}

void ForwardRenderer::updateCamera(const RenderScene& scene, uint16_t width, uint16_t height) {
    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.7777778f;
    m_camera = scene.cameras.empty() ? fallbackCamera(scene, aspect) : scene.cameras.front();
    m_debugUniforms.depthParams = Vec4(m_camera.nearClip, m_camera.farClip, 0.0f, 0.0f);
    m_debugUniforms.clusterParams = Vec4(static_cast<float>(m_debugLightClusterSlice), 0.0f, 0.0f, 0.0f);
    m_debugUniforms.lightmapParams = Vec4(1.0f, 0.0f, 0.0f, 0.0f);

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
    m_lightBuilder.build(scene, m_cameraPosition, m_cameraForward, m_camera.farClip, contactShadowsActive(),
                         m_shadowSettings.contactShadows.directionalOnly, &m_shadowSettings, m_lightListCapacity,
                         m_pipelineSettings.supportLightLayers);

    uint64_t cookieSignature = 1469598103934665603ull;
    const auto& selectedLights = m_lightBuilder.selectedLights();
    std::vector<uint32_t> cookieOrder;
    cookieOrder.reserve(selectedLights.size());
    for (uint32_t index = 0; index < selectedLights.size(); ++index) {
        const Light* light = selectedLights[index];
        if (light == nullptr || light->cookieMode == LightCookieMode::None) {
            continue;
        }
        cookieOrder.push_back(index);
        const auto mix = [&cookieSignature](uint64_t value) {
            cookieSignature ^= value;
            cookieSignature *= 1099511628211ull;
        };
        mix(reinterpret_cast<uintptr_t>(light));
        mix(reinterpret_cast<uintptr_t>(light->cookieNative.data()));
        mix(light->cookieNativeWidth);
        mix(light->cookieNativeHeight);
        mix(light->cookieNative.size());
        mix(static_cast<uint64_t>(light->cookieMode));
    }
    std::stable_sort(cookieOrder.begin(), cookieOrder.end(), [&](uint32_t first, uint32_t second) {
        const uint32_t firstSize = m_cookieAtlas.requiredTileSize(*selectedLights[first]);
        const uint32_t secondSize = m_cookieAtlas.requiredTileSize(*selectedLights[second]);
        if (firstSize != secondSize) {
            return firstSize > secondSize;
        }
        return first < second;
    });
    m_cookieAtlas.beginFrame(cookieSignature);
    for (uint32_t index : cookieOrder) {
        uint32_t tileIndex = 0;
        Vec4 atlasScaleOffset;
        if (m_cookieAtlas.allocate(*selectedLights[index], &tileIndex, &atlasScaleOffset)) {
            m_lightBuilder.setCookieAtlas(index, tileIndex, atlasScaleOffset);
        }
    }

    const GpuLightData& lightData = m_lightBuilder.data();
    if (m_frameIndex == 0) {
        HBG_LOGI("cookie atlas: tiles={}/{} native={} size={} mips={} lights={}", m_cookieAtlas.tileCount(),
                 kCookieAtlasTileCapacity, m_cookieAtlas.nativeTileCount(), m_cookieAtlas.size(),
                 m_cookieAtlas.mipCount(), selectedLights.size());
    }

    m_passUniforms = {};
    m_passUniforms.lightmapParams2 = Vec4(1.0f, static_cast<float>(kLightmapShChannelCount), 0.0f, 0.0f);
    m_passUniforms.cookieAtlasParams =
        Vec4(static_cast<float>(m_cookieAtlas.size()),
             static_cast<float>(m_cookieAtlas.padding()) / static_cast<float>(m_cookieAtlas.size()),
             static_cast<float>(m_cookieAtlas.mipCount() - 1u), 0.0f);
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
            Vec4(1.0f, 0.0f, m_shadowManager.directionalRequestCount() > 0 ? 1.0f : 0.0f,
                 static_cast<float>(m_shadowManager.directionalRequestCount()));
    } else {
        m_passUniforms.shadowMatrix = Mat4::identity();
        m_passUniforms.shadowViewProj = Mat4::identity();
        m_passUniforms.shadowCascadeSplits = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        m_passUniforms.shadowParams = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    m_passUniforms.shadowParams.y = m_shadowSettings.microShadows ? m_shadowSettings.microShadowOpacity : 0.0f;
    m_passUniforms.lightCounts.w = shadowValid ? static_cast<float>(m_shadowManager.shadowedLightCount()) : 0.0f;
    if (m_frameIndex == 0) {
        HBG_LOGI("shadows: requests={} directional={} punctual={} area={} splits=({}, {}, {}, {})",
                 m_shadowManager.requests().size(), m_shadowManager.directionalRequestCount(),
                 m_shadowManager.punctualRequestCount(), m_shadowManager.areaRequestCount(),
                 m_passUniforms.shadowCascadeSplits.x,
                 m_passUniforms.shadowCascadeSplits.y, m_passUniforms.shadowCascadeSplits.z,
                 m_passUniforms.shadowCascadeSplits.w);
        HBG_LOGI("shadow cache: directional_active={} directional_cached={} directional_dirty={} "
                 "directional_mixed={} directional_partial={} "
                 "punctual_active={} punctual_cached={} area_active={} area_cached={} "
                 "area_dynamic_atlas={} area_mixed={} area_dirty={}",
                 m_shadowManager.directionalCacheActive() ? "true" : "false",
                 m_shadowManager.cachedDirectionalRequestCount(),
                 m_shadowManager.directionalCacheDirty() ? "true" : "false",
                 m_shadowManager.directionalMixedCached() ? "true" : "false",
                 m_shadowManager.directionalCacheMixed() ? "true" : "false",
                 m_shadowManager.cacheActive() ? "true" : "false", m_shadowManager.cachedRequestCount(),
                 m_shadowManager.areaCacheActive() ? "true" : "false",
                 m_shadowManager.cachedAreaRequestCount(),
                 m_shadowManager.areaCacheUsesDynamicAtlas() ? "true" : "false",
                 m_shadowManager.areaCacheMixed() ? "true" : "false",
                 m_shadowManager.areaCacheDirty() ? "true" : "false");
    }

    uploadLightData();
    uploadCookieAtlas();
    updateLightList();
    m_passUniforms.lightListParams2.w = m_shadowManager.areaRequestCount() > 0 ? 1.0f : 0.0f;
    updateEnvironmentUniforms();
}

void ForwardRenderer::updateEnvironmentUniforms() {
    const bool skyReflection = m_frameSettings.isEnabled(FrameSettingsField::SkyReflection);
    const bool probesEnabled = m_frameSettings.isEnabled(FrameSettingsField::ReflectionProbe);
    m_probeManager.select(m_cameraPosition, probesEnabled, skyReflection);
    m_probeUniforms = m_probeManager.uniforms();

    m_environmentUniforms = {};
    if (!m_skyManager.valid()) {
        m_environmentUniforms.skyParams = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    m_environmentUniforms = m_skyManager.uniforms();
    m_environmentUniforms.skyParams.z = 1.0f;
    m_environmentUniforms.skyParams.w = skyReflection ? 1.0f : 0.0f;
}

void ForwardRenderer::bindProbeTextures(gfx::Encoder& encoder, bool bindCookies) const {
    const gfx::TextureHandle probeArray = m_probeManager.textureArray();
    encoder.setTexture(14, m_probeArraySampler, probeArray.valid() ? probeArray : m_probeArrayFallback,
                       BGFX_SAMPLER_V_CLAMP);
    if (bindCookies) {
        bindCookieAtlas(encoder);
    }
}

void ForwardRenderer::bindCookieAtlas(gfx::Encoder& encoder, uint8_t stage) const {
    const gfx::TextureHandle texture = m_cookieAtlasTexture.valid() ? m_cookieAtlasTexture : m_cookieAtlasFallback;
    const uint32_t flags = BGFX_SAMPLER_UVW_CLAMP | (m_cookieAtlas.native() ? 0u : BGFX_SAMPLER_POINT);
    encoder.setTexture(stage, m_cookieAtlasSampler, texture, flags);
}

gfx::ProgramHandle ForwardRenderer::lightListProgram(shader::PassId pass, uint64_t materialKey) {
    const uint64_t pipelineKey = m_gpuLightListBufferActive ? shader::kLightListBufferProgramKey : 0;
    return m_shaderLibrary.program(shader::ProgramKey{pass, materialKey, pipelineKey});
}

void ForwardRenderer::bindLightListTexture(gfx::Encoder& encoder, bool cluster) const {
    const gfx::TextureHandle texture =
        cluster && m_clusterTexture.valid() ? m_clusterTexture : m_lightListTexture;
    if (texture.valid()) {
        encoder.setTexture(7, m_lightListSampler, texture, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    }
}

void ForwardRenderer::bindLightList(gfx::Encoder& encoder, bool cluster) const {
    if (m_gpuLightListBufferActive) {
        const gfx::BufferHandle buffer =
            cluster && m_lightClusterBuffer.valid() ? m_lightClusterBuffer : m_activeLightListBuffer;
        if (buffer.valid()) {
            encoder.setStorageBuffer(7, buffer, bgfx::Access::Read);
        }
        return;
    }
    bindLightListTexture(encoder, cluster);
}

ForwardLightingContext ForwardRenderer::makeForwardLightingContext(
    const gfx::generated::ObjectUniforms& objectUniforms, gfx::TextureHandle contactShadow, bool cluster,
    bool contactShadows, bool useGpuLightList) const {
    ForwardLightingContext context;
    context.globalBinder = &m_globalBinder;
    context.passBinder = &m_passBinder;
    context.clusterBinder = &m_clusterBinder;
    context.environmentBinder = &m_environmentBinder;
    context.probeBinder = &m_probeBinder;
    context.objectBinder = &m_objectBinder;
    context.globalUniforms = m_globalUniforms;
    context.passUniforms = m_passUniforms;
    context.clusterUniforms = m_clusterUniforms;
    context.environmentUniforms = m_environmentUniforms;
    context.probeUniforms = m_probeUniforms;
    context.objectUniforms = objectUniforms;

    context.lightDataSampler = m_lightDataSampler;
    context.directionalShadowSampler = m_shadowSampler;
    context.lightListSampler = m_lightListSampler;
    context.punctualShadowSampler = m_shadowPunctualSampler;
    context.environmentSampler = m_environmentSampler;
    context.contactShadowSampler = m_contactShadowSampler;
    context.probeArraySampler = m_probeArraySampler;
    context.cookieAtlasSampler = m_cookieAtlasSampler;

    context.lightData = m_lightDataTexture;
    context.directionalShadow = directionalShadowSamplingTexture(m_shadowManager.activeDirectionalTexture());
    context.lightListBuffer =
        cluster && m_lightClusterBuffer.valid() ? m_lightClusterBuffer : m_activeLightListBuffer;
    context.lightListTexture = cluster && m_clusterTexture.valid() ? m_clusterTexture : m_lightListTexture;
    context.gpuLightList = useGpuLightList && m_gpuLightListBufferActive;
    context.shadowContext = m_shadowManager.contextBuffer();
    context.punctualShadow = m_shadowManager.activePunctualTexture();
    context.environment = m_skyManager.valid() ? m_skyManager.texture() : m_environmentFallback;
    context.environmentFallback = m_environmentFallback;
    context.environmentFlags = m_skyManager.valid() ? BGFX_SAMPLER_V_CLAMP : BGFX_SAMPLER_UVW_CLAMP;
    context.contactShadow = contactShadow;
    context.contactShadowFallback = m_contactShadowFallback;
    context.bindContactShadow = contactShadows;
    context.probeArray = m_probeManager.textureArray();
    context.probeFallback = m_probeArrayFallback;
    context.cookieAtlas = m_cookieAtlasTexture.valid() ? m_cookieAtlasTexture : m_cookieAtlasFallback;
    context.cookieFallback = m_cookieAtlasFallback;
    context.cookieFlags =
        BGFX_SAMPLER_UVW_CLAMP | (m_cookieAtlas.native() ? 0u : BGFX_SAMPLER_POINT);
    context.directionalShadowFlags = directionalShadowSamplingFlags();
    return context;
}

void ForwardRenderer::updatePostProcessUniforms() {
    const bool postProcessEnabled = m_frameSettings.isEnabled(FrameSettingsField::Postprocess);
    const bool exposureEnabled = postProcessEnabled && m_frameSettings.isEnabled(FrameSettingsField::ExposureControl);
    const bool tonemappingEnabled = postProcessEnabled && m_frameSettings.isEnabled(FrameSettingsField::Tonemapping);
    const PostProcessSettings& settings = m_postProcessSettings;

    float exposureMode = 2.0f;
    float fixedExposure = 1.0f;
    if (exposureEnabled) {
        if (m_postProcessChain.exposureApplied()) {
            exposureMode = 2.0f;
        } else if (settings.exposureMode != ExposureMode::Fixed) {
            exposureMode = 1.0f;
        } else {
            exposureMode = 0.0f;
            fixedExposure = fixedExposureMultiplier(settings);
        }
    }

    const TonemappingMode tonemapping = tonemappingEnabled ? settings.tonemapping : TonemappingMode::None;
    m_postProcessUniforms.exposureParams = Vec4(
        exposureMode, fixedExposure, settings.compensation + settings.debugExposure, lensImperfectionExposureScale());
    m_postProcessUniforms.autoExposureParams = Vec4(settings.limitMin, settings.limitMax, 0.0f, 0.0f);
    m_postProcessUniforms.tonemapParams = Vec4(static_cast<float>(tonemapping), 0.0f, 0.0f, 0.0f);
    m_postProcessUniforms.frameParams =
        Vec4(postProcessEnabled ? 1.0f : 0.0f, settings.hasAdvancedEffects() ? 1.0f : 0.0f,
             m_postProcessChain.exposureApplied() ? 1.0f : 0.0f,
             m_postProcessChain.exposureHistoryValid() ? 1.0f : 0.0f);
    m_postProcessUniforms.screenParams =
        Vec4(static_cast<float>(m_viewState.renderWidth()), static_cast<float>(m_viewState.renderHeight()),
             static_cast<float>(m_width), static_cast<float>(m_height));
}

void ForwardRenderer::updateSsaoUniforms(bool enabled) {
    m_ssaoUniforms = {};
    m_ssaoUniforms.params = Vec4(m_ssaoSettings.radius, m_ssaoSettings.intensity, m_ssaoSettings.bias,
                                 m_ssaoSettings.thickness);
    m_ssaoUniforms.params2 =
        Vec4(m_ssaoSettings.falloff, static_cast<float>(m_ssaoSettings.stepCount),
             m_ssaoSettings.halfResolution ? 1.0f : 0.0f, enabled ? 1.0f : 0.0f);
}

void ForwardRenderer::updateSsrUniforms(bool enabled) {
    m_ssrUniforms = {};
    m_ssrUniforms.params = Vec4(m_ssrSettings.maxDistance, static_cast<float>(m_ssrSettings.maxSteps),
                                m_ssrSettings.thickness, m_ssrSettings.roughnessFade);
    m_ssrUniforms.params2 =
        Vec4(m_ssrSettings.edgeFade, m_ssrSettings.intensity, enabled ? 1.0f : 0.0f, 0.0f);
    m_ssrUniforms.params3 = Vec4(0.0f, m_ssrSettings.temporalWeight, 0.0f, 0.0f);
}

void ForwardRenderer::updateSsgiUniforms(bool enabled) {
    m_ssgiUniforms = {};
    m_ssgiUniforms.params = Vec4(m_ssgiSettings.intensity, m_ssgiSettings.maxDistance,
                                 static_cast<float>(m_ssgiSettings.maxSteps),
                                 static_cast<float>(m_ssgiSettings.rayCount));
    m_ssgiUniforms.params2 = Vec4(m_ssgiSettings.falloff, m_ssgiSettings.thickness,
                                  m_ssgiSettings.halfResolution ? 1.0f : 0.0f, enabled ? 1.0f : 0.0f);
    m_ssgiUniforms.params3 = Vec4(m_ssgiSettings.ambientBias, 0.0f, 0.0f, 0.0f);
    m_ssgiUniforms.params4 = Vec4(0.0f, 0.02f, 8.0f, 0.0f);
}

void ForwardRenderer::updateVolumetricFogUniforms(bool enabled, bool historyValid, bool temporalActive,
                                                  bool punctualLights, uint16_t fogWidth, uint16_t fogHeight) {
    m_fogUniforms = {};
    const float scaleHeight = std::max(m_volumetricFogSettings.scaleHeight(), 0.01f);
    m_fogUniforms.params =
        Vec4(m_volumetricFogSettings.baseHeight, 1.0f / scaleHeight, m_volumetricFogSettings.extinction(),
             m_volumetricFogSettings.anisotropy);
    m_fogUniforms.params2 =
        Vec4(m_volumetricFogSettings.depthExtent, static_cast<float>(m_volumetricFogSettings.stepCount),
             enabled ? 1.0f : 0.0f, m_volumetricFogSettings.globalLightProbeDimmer);
    m_fogUniforms.albedo =
        Vec4(m_volumetricFogSettings.albedo.x, m_volumetricFogSettings.albedo.y, m_volumetricFogSettings.albedo.z,
             1.0f);
    m_fogUniforms.params3 =
        Vec4(m_volumetricFogSettings.directionalShadows ? 1.0f : 0.0f, punctualLights ? 1.0f : 0.0f,
             temporalActive ? 1.0f : 0.0f, m_volumetricFogSettings.punctualShadows ? 1.0f : 0.0f);
    m_fogUniforms.params4 = Vec4(static_cast<float>(fogWidth), static_cast<float>(fogHeight),
                                 m_volumetricFogSettings.temporalHistoryWeight, historyValid ? 1.0f : 0.0f);
    m_fogUniforms.params5 =
        Vec4(static_cast<float>(m_volumetricFogSettings.maxPunctualLights),
             m_volumetricFogSettings.temporalJitter ? 1.0f : 0.0f, 0.0f, 0.0f);
}

void ForwardRenderer::updateLocalFogUniforms(const RenderScene& scene, uint32_t& localVolumeCount) {
    localVolumeCount = 0;
    if (!m_volumetricFogSettings.localVolumes) {
        m_fogUniforms.params6 = Vec4(0.0f, static_cast<float>(m_localFogMaskCount), 0.0f,
                                     static_cast<float>(kMaxLocalFogVolumes));
        return;
    }

    for (std::size_t index = 0; index < scene.localFogVolumes.size(); ++index) {
        if (localVolumeCount >= kMaxLocalFogVolumes) {
            if (!m_localFogVolumeWarningLogged) {
                HBG_LOGW("local fog: only the first {} enabled volumes are uploaded", kMaxLocalFogVolumes);
                m_localFogVolumeWarningLogged = true;
            }
            break;
        }

        LocalFogVolume volume = scene.localFogVolumes[index];
        if (!volume.enabled) {
            continue;
        }
        sanitizeLocalFogVolume(volume);

        const Mat4 rotation = volume.rotationMatrix();
        const Vec3 right(rotation[0], rotation[1], rotation[2]);
        const Vec3 up(rotation[4], rotation[5], rotation[6]);
        const Vec3 halfSize(volume.size.x * 0.5f, volume.size.y * 0.5f, volume.size.z * 0.5f);
        const int maskSlot =
            index < m_localFogMaskSlots.size() ? m_localFogMaskSlots[index] : -1;
        const float maskIntensity = maskSlot >= 0 ? volume.maskIntensity : 0.0f;
        const float distanceFadeLength = volume.distanceFadeLength();

        m_fogUniforms.localObbRight[localVolumeCount] =
            Vec4(right.x, right.y, right.z, volume.shape == LocalFogShape::Sphere ? 1.0f : 0.0f);
        m_fogUniforms.localObbUp[localVolumeCount] =
            Vec4(up.x, up.y, up.z, static_cast<float>(maskSlot));
        m_fogUniforms.localObbExtents[localVolumeCount] = Vec4(halfSize.x, halfSize.y, halfSize.z, 0.0f);
        m_fogUniforms.localObbCenter[localVolumeCount] =
            Vec4(volume.center.x, volume.center.y, volume.center.z, 0.0f);
        const Vec3 scattering = volume.scattering();
        m_fogUniforms.localScattering[localVolumeCount] =
            Vec4(scattering.x, scattering.y, scattering.z, volume.extinction());
        m_fogUniforms.localFaceFadePos[localVolumeCount] =
            Vec4(1.0f / std::max(volume.positiveFade.x, 1.0e-4f),
                 1.0f / std::max(volume.positiveFade.y, 1.0e-4f),
                 1.0f / std::max(volume.positiveFade.z, 1.0e-4f), volume.invertFade ? 1.0f : 0.0f);
        m_fogUniforms.localFaceFadeNeg[localVolumeCount] =
            Vec4(1.0f / std::max(volume.negativeFade.x, 1.0e-4f),
                 1.0f / std::max(volume.negativeFade.y, 1.0e-4f),
                 1.0f / std::max(volume.negativeFade.z, 1.0e-4f),
                 volume.falloff == LocalFogFalloff::Exponential ? 1.0f : 0.0f);
        m_fogUniforms.localDistanceFade[localVolumeCount] =
            Vec4(1.0f / distanceFadeLength, volume.endTimesRcpDistanceFadeLength(), 0.0f, 0.0f);
        m_fogUniforms.localMaskTiling[localVolumeCount] =
            Vec4(volume.maskTiling.x, volume.maskTiling.y, maskIntensity,
                 static_cast<float>(static_cast<uint32_t>(volume.maskPlane)));
        m_fogUniforms.localMaskScroll[localVolumeCount] =
            Vec4(volume.maskScroll.x, volume.maskScroll.y, volume.maskInvert ? 1.0f : 0.0f, 0.0f);
        ++localVolumeCount;
    }

    m_fogUniforms.params6 =
        Vec4(static_cast<float>(localVolumeCount), static_cast<float>(m_localFogMaskCount), 1.0f,
             static_cast<float>(kMaxLocalFogVolumes));
}

bool ForwardRenderer::ensureLocalFogMaskTextures(gfx::ResourceManager& resources, const RenderScene& scene) {
    releaseLocalFogMaskTextures(resources);
    m_localFogMaskSlots.assign(scene.localFogVolumes.size(), -1);
    std::array<int, kMaxLocalFogMasks> slotImages{};
    slotImages.fill(-1);

    uint32_t maskCount = 0;
    for (std::size_t index = 0; index < scene.localFogVolumes.size(); ++index) {
        if (!scene.localFogVolumes[index].enabled) {
            continue;
        }
        const int imageIndex = scene.localFogVolumes[index].maskImage;
        if (imageIndex < 0) {
            continue;
        }
        if (static_cast<std::size_t>(imageIndex) >= scene.images.size()) {
            HBG_LOGW("local fog '{}': mask image index {} is invalid", scene.localFogVolumes[index].name,
                     imageIndex);
            continue;
        }

        int slot = -1;
        for (uint32_t existing = 0; existing < maskCount; ++existing) {
            if (slotImages[existing] == imageIndex) {
                slot = static_cast<int>(existing);
                break;
            }
        }
        if (slot < 0) {
            if (maskCount >= kMaxLocalFogMasks) {
                if (!m_localFogVolumeWarningLogged) {
                    HBG_LOGW("local fog: only the first {} unique density masks are supported",
                             kMaxLocalFogMasks);
                    m_localFogVolumeWarningLogged = true;
                }
                continue;
            }
            const asset::ImageAsset& image = scene.images[static_cast<std::size_t>(imageIndex)];
            gfx::TextureHandle texture = asset::TextureLoader::createTexture(
                resources, image, false, "localFog.mask." + std::to_string(maskCount));
            if (!texture.valid()) {
                HBG_LOGW("local fog '{}': failed to upload mask '{}'", scene.localFogVolumes[index].name,
                         image.name);
                continue;
            }
            slot = static_cast<int>(maskCount);
            slotImages[maskCount] = imageIndex;
            m_fogMaskTextures[maskCount] = texture;
            ++maskCount;
        }
        m_localFogMaskSlots[index] = slot;
    }

    m_localFogMaskCount = maskCount;
    m_fogMaskFallback = createSolidTexture(resources, 255, 255, 255, 255, "localFog.mask.fallback");
    if (!m_fogMaskFallback.valid()) {
        m_localFogMaskSlots.assign(scene.localFogVolumes.size(), -1);
        m_localFogMaskCount = 0;
        return false;
    }
    return true;
}

void ForwardRenderer::releaseLocalFogMaskTextures(gfx::ResourceManager& resources) {
    for (gfx::TextureHandle& texture : m_fogMaskTextures) {
        if (texture.valid()) {
            resources.destroyTexture(texture);
        }
        texture = {};
    }
    if (m_fogMaskFallback.valid()) {
        resources.destroyTexture(m_fogMaskFallback);
        m_fogMaskFallback = {};
    }
    m_localFogMaskSlots.clear();
    m_localFogMaskCount = 0;
}

bool ForwardRenderer::ensureVolumetricFogHistory(uint16_t fogWidth, uint16_t fogHeight) {
    if (m_resources == nullptr || fogWidth == 0 || fogHeight == 0) {
        return false;
    }
    if (m_fogHistoryTextures[0].valid() && m_fogHistoryTextures[1].valid() &&
        m_fogHistoryWidth == fogWidth && m_fogHistoryHeight == fogHeight) {
        return true;
    }

    releaseVolumetricFogHistory();
    gfx::TextureDesc desc;
    desc.width = fogWidth;
    desc.height = fogHeight;
    desc.format = gfx::TextureFormat::RGBA16F;
    desc.renderTarget = true;
    desc.flags = BGFX_SAMPLER_UVW_CLAMP;
    for (uint32_t index = 0; index < m_fogHistoryTextures.size(); ++index) {
        desc.name = "atmosphere.volumetricFog.history." + std::to_string(index);
        m_fogHistoryTextures[index] = m_resources->createTexture(desc);
        if (!m_fogHistoryTextures[index].valid()) {
            HBG_LOGE("volumetric fog: failed to create history texture ({}x{})", fogWidth, fogHeight);
            releaseVolumetricFogHistory();
            return false;
        }
    }
    m_fogHistoryWidth = fogWidth;
    m_fogHistoryHeight = fogHeight;
    m_fogHistoryIndex = 0;
    m_fogHistoryFrame = 0;
    m_fogHistoryValid = false;
    return true;
}

void ForwardRenderer::releaseVolumetricFogHistory() {
    if (m_resources != nullptr) {
        for (gfx::TextureHandle& texture : m_fogHistoryTextures) {
            if (texture.valid()) {
                m_resources->destroyTexture(texture);
            }
            texture = {};
        }
    } else {
        for (gfx::TextureHandle& texture : m_fogHistoryTextures) {
            texture = {};
        }
    }
    m_fogHistoryWidth = 0;
    m_fogHistoryHeight = 0;
    m_fogHistoryIndex = 0;
    m_fogHistoryFrame = 0;
    m_fogHistoryValid = false;
}

bool ForwardRenderer::ensureSsaoHistory(uint16_t width, uint16_t height) {
    if (m_resources == nullptr || width == 0 || height == 0) {
        return false;
    }
    if (m_ssaoHistoryTextures[0].valid() && m_ssaoHistoryTextures[1].valid() &&
        m_ssaoHistoryWidth == width && m_ssaoHistoryHeight == height) {
        return true;
    }

    releaseSsaoHistory();
    gfx::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = gfx::TextureFormat::RGBA8;
    desc.renderTarget = true;
    desc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    for (uint32_t index = 0; index < m_ssaoHistoryTextures.size(); ++index) {
        desc.name = "ssao.history." + std::to_string(index);
        m_ssaoHistoryTextures[index] = m_resources->createTexture(desc);
        if (!m_ssaoHistoryTextures[index].valid()) {
            HBG_LOGE("ssao: failed to create history texture ({}x{})", width, height);
            releaseSsaoHistory();
            return false;
        }
    }
    m_ssaoHistoryWidth = width;
    m_ssaoHistoryHeight = height;
    m_ssaoHistoryIndex = 0;
    m_ssaoHistoryFrame = 0;
    m_ssaoHistoryValid = false;
    HBG_LOGI("ssao: history created {}x{} RGBA8", width, height);
    return true;
}

void ForwardRenderer::releaseSsaoHistory() {
    if (m_resources != nullptr) {
        for (gfx::TextureHandle& texture : m_ssaoHistoryTextures) {
            if (texture.valid()) {
                m_resources->destroyTexture(texture);
            }
            texture = {};
        }
    } else {
        for (gfx::TextureHandle& texture : m_ssaoHistoryTextures) {
            texture = {};
        }
    }
    m_ssaoHistoryWidth = 0;
    m_ssaoHistoryHeight = 0;
    m_ssaoHistoryIndex = 0;
    m_ssaoHistoryFrame = 0;
    m_ssaoHistoryValid = false;
}

bool ForwardRenderer::ensureSsrHistory(uint16_t width, uint16_t height) {
    if (m_resources == nullptr || width == 0 || height == 0) {
        return false;
    }
    if (m_ssrHistoryTextures[0].valid() && m_ssrHistoryTextures[1].valid() &&
        m_ssrHistoryWidth == width && m_ssrHistoryHeight == height) {
        return true;
    }

    releaseSsrHistory();
    gfx::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = gfx::TextureFormat::RGBA16F;
    desc.renderTarget = true;
    desc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    for (uint32_t index = 0; index < m_ssrHistoryTextures.size(); ++index) {
        desc.name = "ssr.history." + std::to_string(index);
        m_ssrHistoryTextures[index] = m_resources->createTexture(desc);
        if (!m_ssrHistoryTextures[index].valid()) {
            HBG_LOGE("ssr: failed to create history texture ({}x{})", width, height);
            releaseSsrHistory();
            return false;
        }
    }
    m_ssrHistoryWidth = width;
    m_ssrHistoryHeight = height;
    m_ssrHistoryIndex = 0;
    m_ssrHistoryFrame = 0;
    m_ssrHistoryValid = false;
    HBG_LOGI("ssr: history created {}x{} {}", width, height, gfx::textureFormatName(desc.format));
    return true;
}

void ForwardRenderer::releaseSsrHistory() {
    if (m_resources != nullptr) {
        for (gfx::TextureHandle& texture : m_ssrHistoryTextures) {
            if (texture.valid()) {
                m_resources->destroyTexture(texture);
            }
            texture = {};
        }
    } else {
        for (gfx::TextureHandle& texture : m_ssrHistoryTextures) {
            texture = {};
        }
    }
    m_ssrHistoryWidth = 0;
    m_ssrHistoryHeight = 0;
    m_ssrHistoryIndex = 0;
    m_ssrHistoryFrame = 0;
    m_ssrHistoryValid = false;
}

bool ForwardRenderer::ensureSsgiHistory(uint16_t width, uint16_t height) {
    if (m_resources == nullptr || width == 0 || height == 0) {
        return false;
    }
    if (m_ssgiHistoryTextures[0].valid() && m_ssgiHistoryTextures[1].valid() &&
        m_ssgiHistoryDepthTextures[0].valid() && m_ssgiHistoryDepthTextures[1].valid() &&
        m_ssgiHistoryNormalTextures[0].valid() && m_ssgiHistoryNormalTextures[1].valid() &&
        m_ssgiHistoryWidth == width && m_ssgiHistoryHeight == height) {
        return true;
    }

    releaseSsgiHistory();
    gfx::TextureDesc colorDesc;
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.format = gfx::TextureFormat::RGBA16F;
    colorDesc.renderTarget = true;
    colorDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;

    gfx::TextureDesc depthDesc = colorDesc;
    depthDesc.format = gfx::TextureFormat::R32F;

    gfx::TextureDesc normalDesc = colorDesc;
    normalDesc.format = gfx::TextureFormat::RGBA8;

    for (uint32_t index = 0; index < m_ssgiHistoryTextures.size(); ++index) {
        colorDesc.name = "ssgi.history." + std::to_string(index);
        m_ssgiHistoryTextures[index] = m_resources->createTexture(colorDesc);
        depthDesc.name = "ssgi.historyDepth." + std::to_string(index);
        m_ssgiHistoryDepthTextures[index] = m_resources->createTexture(depthDesc);
        normalDesc.name = "ssgi.historyNormal." + std::to_string(index);
        m_ssgiHistoryNormalTextures[index] = m_resources->createTexture(normalDesc);
        if (!m_ssgiHistoryTextures[index].valid() || !m_ssgiHistoryDepthTextures[index].valid() ||
            !m_ssgiHistoryNormalTextures[index].valid()) {
            HBG_LOGE("ssgi: failed to create history textures ({}x{})", width, height);
            releaseSsgiHistory();
            return false;
        }
    }
    m_ssgiHistoryWidth = width;
    m_ssgiHistoryHeight = height;
    m_ssgiHistoryIndex = 0;
    m_ssgiHistoryFrame = 0;
    m_ssgiHistoryValid = false;
    HBG_LOGI("ssgi: history created {}x{} {} + R32F + RGBA8", width, height,
             gfx::textureFormatName(colorDesc.format));
    return true;
}

void ForwardRenderer::releaseSsgiHistory() {
    if (m_resources != nullptr) {
        for (gfx::TextureHandle& texture : m_ssgiHistoryTextures) {
            if (texture.valid()) {
                m_resources->destroyTexture(texture);
            }
            texture = {};
        }
        for (gfx::TextureHandle& texture : m_ssgiHistoryDepthTextures) {
            if (texture.valid()) {
                m_resources->destroyTexture(texture);
            }
            texture = {};
        }
        for (gfx::TextureHandle& texture : m_ssgiHistoryNormalTextures) {
            if (texture.valid()) {
                m_resources->destroyTexture(texture);
            }
            texture = {};
        }
    } else {
        for (gfx::TextureHandle& texture : m_ssgiHistoryTextures) {
            texture = {};
        }
        for (gfx::TextureHandle& texture : m_ssgiHistoryDepthTextures) {
            texture = {};
        }
        for (gfx::TextureHandle& texture : m_ssgiHistoryNormalTextures) {
            texture = {};
        }
    }
    m_ssgiHistoryWidth = 0;
    m_ssgiHistoryHeight = 0;
    m_ssgiHistoryIndex = 0;
    m_ssgiHistoryFrame = 0;
    m_ssgiHistoryValid = false;
}

bool ForwardRenderer::ensureLightDataTexture() {
    if (m_resources == nullptr) {
        return false;
    }
    const gfx::Texture* texture = m_resources->texture(m_lightDataTexture);
    if (texture != nullptr && texture->desc().width == kLightDataTextureWidth &&
        texture->desc().height == kLightDataTextureRows) {
        return true;
    }
    if (m_lightDataTexture.valid()) {
        m_resources->destroyTexture(m_lightDataTexture);
        m_lightDataTexture = {};
    }
    gfx::TextureDesc desc;
    desc.width = static_cast<uint16_t>(kLightDataTextureWidth);
    desc.height = static_cast<uint16_t>(kLightDataTextureRows);
    desc.format = gfx::TextureFormat::RGBA32F;
    desc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    desc.name = "lighting.lightData";
    m_lightDataTexture = m_resources->createTexture(desc);
    if (!m_lightDataTexture.valid()) {
        HBG_LOGE("forward renderer: failed to create light-data texture ({}x{})", kLightDataTextureWidth,
                 kLightDataTextureRows);
        return false;
    }
    return true;
}

bool ForwardRenderer::ensureCookieAtlasTexture() {
    if (m_resources == nullptr) {
        return false;
    }
    const gfx::Texture* current = m_resources->texture(m_cookieAtlasTexture);
    if (current != nullptr && current->desc().width == m_cookieAtlas.size() &&
        current->desc().height == m_cookieAtlas.size() &&
        current->desc().mipCount == m_cookieAtlas.mipCount()) {
        return true;
    }
    if (m_cookieAtlasTexture.valid()) {
        m_resources->destroyTexture(m_cookieAtlasTexture);
        m_cookieAtlasTexture = {};
    }
    m_cookieAtlasUploaded = false;

    gfx::TextureDesc desc;
    desc.width = static_cast<uint16_t>(m_cookieAtlas.size());
    desc.height = static_cast<uint16_t>(m_cookieAtlas.size());
    desc.mipCount = static_cast<uint8_t>(m_cookieAtlas.mipCount());
    desc.format = gfx::TextureFormat::RGBA8;
    desc.flags = BGFX_SAMPLER_UVW_CLAMP;
    desc.name = "lighting.cookieAtlas";
    m_cookieAtlasTexture = m_resources->createTexture(desc);
    if (!m_cookieAtlasTexture.valid()) {
        HBG_LOGE("forward renderer: failed to create cookie atlas texture ({}x{}, mips={})", desc.width, desc.height,
                 desc.mipCount);
        return false;
    }
    return true;
}

void ForwardRenderer::uploadCookieAtlas() {
    if (!ensureCookieAtlasTexture()) {
        return;
    }
    if (m_cookieAtlasUploaded && m_cookieAtlasUploadHash == m_cookieAtlas.contentHash()) {
        m_cookieAtlas.clearDirty();
        return;
    }
    for (uint32_t mip = 0; mip < m_cookieAtlas.mipCount(); ++mip) {
        const uint32_t mipSize = std::max(1u, m_cookieAtlas.size() >> mip);
        const std::vector<uint8_t>& data = m_cookieAtlas.mipData(mip);
        if (data.size() != static_cast<std::size_t>(mipSize) * mipSize * 4u) {
            HBG_LOGE("forward renderer: cookie atlas mip {} has an invalid size", mip);
            return;
        }
        if (!m_resources->updateTexture(m_cookieAtlasTexture, data.data(), static_cast<uint32_t>(data.size()),
                                        static_cast<uint16_t>(mipSize), static_cast<uint16_t>(mipSize),
                                        static_cast<uint16_t>(mipSize * 4u), 0, static_cast<uint8_t>(mip))) {
            HBG_LOGE("forward renderer: failed to upload cookie atlas mip {}", mip);
            return;
        }
    }
    m_cookieAtlasUploadHash = m_cookieAtlas.contentHash();
    m_cookieAtlasUploaded = true;
    m_cookieAtlas.clearDirty();
}

void ForwardRenderer::uploadLightData() {
    if (!ensureLightDataTexture()) {
        return;
    }
    const GpuLightData& lightData = m_lightBuilder.data();
    const std::size_t textureTexels = static_cast<std::size_t>(kLightDataTextureRows) * kLightDataTextureWidth;
    if (m_lightDataUpload.size() != textureTexels) {
        m_lightDataUpload.assign(textureTexels, Vec4());
    }
    for (uint32_t row = 0; row < kMaxGpuLights; ++row) {
        const auto source = lightData.texels.begin() + static_cast<std::size_t>(row) * kGpuLightTexels;
        auto target = m_lightDataUpload.begin() + static_cast<std::size_t>(row) * kLightDataTextureWidth;
        std::copy_n(source, kGpuLightTexels, target);
    }
    const auto& ltc = ltcGgxTable();
    for (uint32_t index = 0; index < kLtcLutTexelCount; ++index) {
        const uint32_t row = index / kLtcLutSize;
        const uint32_t column = index - row * kLtcLutSize;
        m_lightDataUpload[static_cast<std::size_t>(kMaxGpuLights + row) * kLightDataTextureWidth + column] =
            ltc[index];
    }
    m_resources->updateTexture(m_lightDataTexture, m_lightDataUpload.data(),
                               static_cast<uint32_t>(m_lightDataUpload.size() * sizeof(Vec4)),
                               static_cast<uint16_t>(kLightDataTextureWidth),
                               static_cast<uint16_t>(kLightDataTextureRows),
                               static_cast<uint16_t>(kLightDataTextureWidth * sizeof(Vec4)));
}

bool ForwardRenderer::ensureLightListTexture(uint16_t renderWidth, uint16_t renderHeight) {
    if (m_resources == nullptr) {
        return false;
    }

    const uint32_t maxTextureSize = backendMaxTextureSize();
    const uint32_t capacity =
        LightListBuilder::textureCapacityFor(renderWidth, renderHeight, m_lightListCapacity, maxTextureSize);
    if (capacity != 0 && m_lightListTextureCapacity != 0 && capacity != m_lightListTextureCapacity) {
        HBG_LOGI("light list texture capacity: {} -> {} at {}x{} (maxTextureSize={})", m_lightListTextureCapacity,
                 capacity, renderWidth, renderHeight, maxTextureSize);
    }
    m_lightListTextureCapacity = capacity;
    if (capacity == 0) {
        releaseLightListTexture();
        return false;
    }

    const uint16_t width = LightListBuilder::textureWidthFor(renderWidth, capacity);
    const uint16_t height = LightListBuilder::textureHeightFor(renderHeight);
    if (width == 0 || height == 0 || width > maxTextureSize || width > kMaxTextureUploadWidth ||
        height > maxTextureSize) {
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
    desc.computeWrite = true;
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

bool ForwardRenderer::ensureLightBigTileTexture(uint16_t renderWidth, uint16_t renderHeight) {
    if (m_resources == nullptr) {
        return false;
    }

    const uint32_t maxTextureSize = backendMaxTextureSize();
    const uint16_t width = LightListBuilder::bigTileTextureWidthFor(renderWidth, m_lightListTextureCapacity);
    const uint16_t height = LightListBuilder::bigTileTextureHeightFor(renderHeight);
    if (m_lightListTextureCapacity == 0 || width == 0 || height == 0 || width > maxTextureSize ||
        height > maxTextureSize) {
        releaseLightBigTileTexture();
        return false;
    }
    const gfx::Texture* currentTexture = m_resources->texture(m_lightBigTileTexture);
    if (currentTexture != nullptr && currentTexture->desc().width == width && currentTexture->desc().height == height) {
        return true;
    }

    releaseLightBigTileTexture();
    gfx::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = gfx::TextureFormat::R32U;
    desc.computeWrite = true;
    desc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    desc.name = "lighting.lightBigTileList";
    m_lightBigTileTexture = m_resources->createTexture(desc);
    if (!m_lightBigTileTexture.valid()) {
        HBG_LOGE("forward renderer: failed to create big-tile light-list texture ({}x{})", width, height);
        return false;
    }
    return true;
}

bool ForwardRenderer::ensureClusterLightListTextures(uint16_t renderWidth, uint16_t renderHeight) {
    if (m_resources == nullptr) {
        return false;
    }

    const uint32_t maxTextureSize = backendMaxTextureSize();
    const uint32_t capacity =
        LightListBuilder::clusterCapacityFor(renderWidth, renderHeight, m_lightListTextureCapacity);
    const uint32_t indexCapacity =
        LightListBuilder::clusterIndexCountFor(renderWidth, renderHeight, m_lightListTextureCapacity);
    const uint32_t clusterTileCount = static_cast<uint32_t>(LightListBuilder::tileCount(renderWidth, kClusterTileSize)) *
                                      LightListBuilder::tileCount(renderHeight, kClusterTileSize);
    const uint32_t offsetCount = clusterTileCount * kClusterSliceCount * kLightCategoryCount;
    const uint32_t combinedCount = offsetCount + indexCapacity + clusterTileCount;
    const uint16_t width = LightListBuilder::clusterIndexTextureWidthFor(combinedCount);
    const uint16_t height = LightListBuilder::clusterIndexTextureHeightFor(combinedCount);
    if (capacity == 0 || width == 0 || height == 0 || width > maxTextureSize ||
        width > kMaxTextureUploadWidth || height > maxTextureSize) {
        m_clusterCapacity = 0;
        m_clusterIndexCapacity = 0;
        m_clusterTextureWidth = 0;
        releaseClusterLightListTextures();
        return false;
    }
    m_clusterCapacity = capacity;
    m_clusterIndexCapacity = indexCapacity;
    m_clusterTextureWidth = width;

    const gfx::Texture* current = m_resources->texture(m_clusterTexture);
    if (current != nullptr && current->desc().width == width && current->desc().height == height) {
        return true;
    }

    releaseClusterLightListTextures();
    gfx::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = gfx::TextureFormat::R32U;
    desc.computeWrite = true;
    desc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    desc.name = "lighting.clusterLightList";
    m_clusterTexture = m_resources->createTexture(desc);

    if (!m_clusterTexture.valid()) {
        HBG_LOGE("forward renderer: failed to create cluster light-list texture ({}x{})", width, height);
        releaseClusterLightListTextures();
        m_lightListBuilder.clear();
        return false;
    }
    return true;
}

bool ForwardRenderer::ensureLightListBuffers(uint16_t renderWidth, uint16_t renderHeight) {
    if (m_resources == nullptr) {
        return false;
    }
    const uint16_t tileCountX = LightListBuilder::tileCount(renderWidth, kLightTileSize);
    const uint16_t tileCountY = LightListBuilder::tileCount(renderHeight, kLightTileSize);
    const uint16_t bigTileCountX = LightListBuilder::tileCount(renderWidth, kLightBigTileSize);
    const uint16_t bigTileCountY = LightListBuilder::tileCount(renderHeight, kLightBigTileSize);
    const uint16_t clusterTileCountX = LightListBuilder::tileCount(renderWidth, kClusterTileSize);
    const uint16_t clusterTileCountY = LightListBuilder::tileCount(renderHeight, kClusterTileSize);
    if (tileCountX == 0 || tileCountY == 0 || bigTileCountX == 0 || bigTileCountY == 0 ||
        clusterTileCountX == 0 || clusterTileCountY == 0) {
        releaseLightListBuffers();
        return false;
    }

    const uint32_t screenBoundsCount = kMaxGpuLights * 2u;
    const uint32_t listStride = lightListStrideFor(m_lightListCapacity);
    const uint32_t bigTileCount = static_cast<uint32_t>(bigTileCountX) * bigTileCountY * listStride * kLightCategoryCount;
    const uint32_t fptlCount = static_cast<uint32_t>(tileCountX) * tileCountY * listStride * kLightCategoryCount;
    m_litTileListBase = fptlCount;
    m_litTileListCount = m_litVariantTilesEnabled
                             ? static_cast<uint32_t>(tileCountX) * tileCountY * kLitVariantCount
                             : 0u;
    const uint32_t fptlStorageCount = fptlCount + m_litTileListCount;
    const uint32_t clusterCount =
        static_cast<uint32_t>(clusterTileCountX) * clusterTileCountY * kClusterSliceCount;
    const uint32_t clusterCapacity =
        LightListBuilder::clusterCapacityFor(renderWidth, renderHeight, m_lightListCapacity);
    const uint32_t clusterIndexCapacity =
        LightListBuilder::clusterIndexCountFor(renderWidth, renderHeight, m_lightListCapacity);
    const uint32_t clusterOffsetCount = clusterCount * kLightCategoryCount;
    const uint32_t clusterFlagCount = static_cast<uint32_t>(clusterTileCountX) * clusterTileCountY;
    const uint32_t clusterCombinedCount = clusterOffsetCount + clusterIndexCapacity + clusterFlagCount;
    if (clusterCapacity == 0 || clusterIndexCapacity == 0) {
        releaseLightListBuffers();
        return false;
    }

    const auto needsRecreate = [this](gfx::BufferHandle handle, uint32_t count) {
        const gfx::Buffer* buffer = m_resources->buffer(handle);
        return buffer == nullptr || !buffer->valid() || buffer->count() != count;
    };
    const bool indirectReady =
        !m_pipelineSettings.supportGpuLightListIndirect || m_lightIndirectArgsBuffer.valid();
    const bool litIndirectReady = !m_litVariantTilesEnabled || m_litIndirectArgsBuffer.valid();
    if (!needsRecreate(m_lightScreenBoundsBuffer, screenBoundsCount) &&
        !needsRecreate(m_lightBigTileBuffer, bigTileCount) && !needsRecreate(m_lightFptlBuffer, fptlStorageCount) &&
        !needsRecreate(m_lightClusterBuffer, clusterCombinedCount) &&
        indirectReady && litIndirectReady) {
        m_clusterCapacity = clusterCapacity;
        m_clusterIndexCapacity = clusterIndexCapacity;
        return true;
    }

    releaseLightListBuffers();

    gfx::VertexLayout boundsLayout;
    boundsLayout.add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();

    gfx::BufferDesc boundsDesc;
    boundsDesc.type = gfx::BufferType::StorageVertex;
    boundsDesc.count = screenBoundsCount;
    boundsDesc.layout = &boundsLayout;
    boundsDesc.flags = BGFX_BUFFER_COMPUTE_READ_WRITE;
    boundsDesc.name = "lighting.lightScreenBoundsBuffer";
    m_lightScreenBoundsBuffer = m_resources->createBuffer(boundsDesc);

    gfx::BufferDesc bigDesc;
    bigDesc.type = gfx::BufferType::Storage;
    bigDesc.count = bigTileCount;
    bigDesc.flags = BGFX_BUFFER_COMPUTE_READ_WRITE;
    bigDesc.name = "lighting.lightBigTileBuffer";
    m_lightBigTileBuffer = m_resources->createBuffer(bigDesc);

    gfx::BufferDesc fptlDesc;
    fptlDesc.type = gfx::BufferType::Storage;
    fptlDesc.count = fptlStorageCount;
    fptlDesc.flags = BGFX_BUFFER_COMPUTE_READ_WRITE;
    fptlDesc.name = "lighting.lightFptlBuffer";
    m_lightFptlBuffer = m_resources->createBuffer(fptlDesc);

    gfx::BufferDesc clusterOffsetDesc;
    clusterOffsetDesc.type = gfx::BufferType::Storage;
    clusterOffsetDesc.count = clusterCombinedCount;
    clusterOffsetDesc.flags = BGFX_BUFFER_COMPUTE_READ_WRITE;
    clusterOffsetDesc.name = "lighting.lightClusterBuffer";
    m_lightClusterBuffer = m_resources->createBuffer(clusterOffsetDesc);



    if (m_pipelineSettings.supportGpuLightListIndirect) {
        gfx::BufferDesc indirectDesc;
        indirectDesc.type = gfx::BufferType::Indirect;
        indirectDesc.count = 1;
        indirectDesc.name = "lighting.lightIndirectArgs";
        m_lightIndirectArgsBuffer = m_resources->createBuffer(indirectDesc);
    }
    if (m_litVariantTilesEnabled) {
        gfx::BufferDesc litIndirectDesc;
        litIndirectDesc.type = gfx::BufferType::Indirect;
        litIndirectDesc.count = kLitVariantCount;
        litIndirectDesc.name = "lighting.litVariantIndirectArgs";
        m_litIndirectArgsBuffer = m_resources->createBuffer(litIndirectDesc);
    }

    if (!m_lightScreenBoundsBuffer.valid() || !m_lightBigTileBuffer.valid() || !m_lightFptlBuffer.valid() ||
        !m_lightClusterBuffer.valid() || (m_litVariantTilesEnabled && !m_litIndirectArgsBuffer.valid())) {
        HBG_LOGE("gpu light list: failed to create storage buffers");
        releaseLightListBuffers();
        return false;
    }
    m_clusterCapacity = clusterCapacity;
    m_clusterIndexCapacity = clusterIndexCapacity;
    const uint64_t boundsBytes = static_cast<uint64_t>(screenBoundsCount) * sizeof(Vec4);
    const uint64_t bigBytes = static_cast<uint64_t>(bigTileCount) * sizeof(uint32_t);
    const uint64_t fptlBytes = static_cast<uint64_t>(fptlStorageCount) * sizeof(uint32_t);
    const uint64_t clusterOffsetBytes = static_cast<uint64_t>(clusterOffsetCount) * sizeof(uint32_t);
    const uint64_t clusterIndexBytes = static_cast<uint64_t>(clusterIndexCapacity) * sizeof(uint32_t);
    HBG_LOGI("light list buffers: capacity={} cluster_capacity={} stride={} bounds={} vec4 ({} B), "
             "big={} uints ({} B), fptl={} uints ({} B), cluster_offsets={} uints ({} B), "
             "cluster_indices={} uints ({} B), total={} B",
             m_lightListCapacity, clusterCapacity, listStride, screenBoundsCount, boundsBytes, bigTileCount,
             bigBytes, fptlCount, fptlBytes, clusterOffsetCount, clusterOffsetBytes, clusterIndexCapacity,
             clusterIndexBytes,
             boundsBytes + bigBytes + fptlBytes + clusterOffsetBytes + clusterIndexBytes);
    return true;
}

void ForwardRenderer::releaseLightListBuffers() {
    if (m_resources != nullptr) {
        if (m_lightScreenBoundsBuffer.valid()) {
            m_resources->destroyBuffer(m_lightScreenBoundsBuffer);
        }
        if (m_lightBigTileBuffer.valid()) {
            m_resources->destroyBuffer(m_lightBigTileBuffer);
        }
        if (m_lightFptlBuffer.valid()) {
            m_resources->destroyBuffer(m_lightFptlBuffer);
        }
        if (m_lightClusterBuffer.valid()) {
            m_resources->destroyBuffer(m_lightClusterBuffer);
        }
        if (m_lightIndirectArgsBuffer.valid()) {
            m_resources->destroyBuffer(m_lightIndirectArgsBuffer);
        }
        if (m_litIndirectArgsBuffer.valid()) {
            m_resources->destroyBuffer(m_litIndirectArgsBuffer);
        }
    }
    m_lightScreenBoundsBuffer = {};
    m_lightBigTileBuffer = {};
    m_lightFptlBuffer = {};
    m_lightClusterBuffer = {};
    m_lightIndirectArgsBuffer = {};
    m_litIndirectArgsBuffer = {};
    m_activeLightListBuffer = {};
}

bool ForwardRenderer::ensureLitVariantTileResources(uint16_t renderWidth, uint16_t renderHeight) {
    if (!m_litVariantTilesEnabled || m_resources == nullptr) {
        releaseLitVariantTileResources();
        return false;
    }

    const uint16_t width = LightListBuilder::tileCount(renderWidth, kLightTileSize);
    const uint16_t height = LightListBuilder::tileCount(renderHeight, kLightTileSize);
    if (width == 0 || height == 0) {
        releaseLitVariantTileResources();
        return false;
    }
    const gfx::Texture* current = m_resources->texture(m_litTileFeatureFlagsTexture);
    if (current != nullptr && current->valid() && current->desc().width == width &&
        current->desc().height == height) {
        m_litTileWidth = width;
        m_litTileHeight = height;
        return true;
    }

    releaseLitVariantTileResources();
    gfx::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = gfx::TextureFormat::R32U;
    desc.computeWrite = true;
    desc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    desc.name = "lighting.litTileFeatureFlags";
    m_litTileFeatureFlagsTexture = m_resources->createTexture(desc);
    if (!m_litTileFeatureFlagsTexture.valid()) {
        HBG_LOGE("lit variants: failed to create tile feature flags texture ({}x{})", width, height);
        releaseLitVariantTileResources();
        return false;
    }
    m_litTileWidth = width;
    m_litTileHeight = height;
    return true;
}

void ForwardRenderer::releaseLitVariantTileResources() {
    if (m_resources != nullptr && m_litTileFeatureFlagsTexture.valid()) {
        m_resources->destroyTexture(m_litTileFeatureFlagsTexture);
    }
    m_litTileFeatureFlagsTexture = {};
    m_litTileWidth = 0;
    m_litTileHeight = 0;
}

bool ForwardRenderer::ensureShadowGpuBuffers(uint32_t requestCount, uint32_t casterCount, uint32_t batchCount) {
    if (m_resources == nullptr || requestCount == 0 || casterCount == 0 || batchCount == 0) {
        releaseShadowGpuBuffers();
        return false;
    }

    const uint32_t requestElements = requestCount * 6u;
    const uint32_t casterElements = casterCount * 7u;
    const uint32_t batchElements = 4u + batchCount * 4u;
    const uint32_t recordCount = requestCount * batchCount;
    const uint32_t instanceCount = requestCount * casterCount;
    const uint32_t statsElements = recordCount * 2u;

    const auto matches = [this](gfx::BufferHandle handle, uint32_t count) {
        const gfx::Buffer* buffer = m_resources->buffer(handle);
        return buffer != nullptr && buffer->valid() && buffer->count() == count;
    };
    if (matches(m_shadowRequestBuffer, requestElements) && matches(m_shadowCasterBuffer, casterElements) &&
        matches(m_shadowBatchBuffer, batchElements) && matches(m_shadowInstanceBuffer, instanceCount) &&
        matches(m_shadowIndirectBuffer, recordCount) && matches(m_shadowStatsBuffer, statsElements)) {
        return true;
    }

    releaseShadowGpuBuffers();

    gfx::VertexLayout dataLayout;
    dataLayout.add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();

    gfx::BufferDesc requestDesc;
    requestDesc.type = gfx::BufferType::StorageVertex;
    requestDesc.count = requestElements;
    requestDesc.layout = &dataLayout;
    requestDesc.flags = BGFX_BUFFER_COMPUTE_READ;
    requestDesc.name = "shadow.requests";
    m_shadowRequestBuffer = m_resources->createBuffer(requestDesc);

    gfx::BufferDesc casterDesc;
    casterDesc.type = gfx::BufferType::StorageVertex;
    casterDesc.count = casterElements;
    casterDesc.layout = &dataLayout;
    casterDesc.flags = BGFX_BUFFER_COMPUTE_READ;
    casterDesc.name = "shadow.casters";
    m_shadowCasterBuffer = m_resources->createBuffer(casterDesc);

    gfx::BufferDesc batchDesc;
    batchDesc.type = gfx::BufferType::Storage;
    batchDesc.count = batchElements;
    batchDesc.flags = BGFX_BUFFER_COMPUTE_READ;
    batchDesc.name = "shadow.batches";
    m_shadowBatchBuffer = m_resources->createBuffer(batchDesc);

    gfx::VertexLayout instanceLayout;
    instanceLayout.add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord3, 4, bgfx::AttribType::Float)
        .end();
    gfx::BufferDesc instanceDesc;
    instanceDesc.type = gfx::BufferType::StorageVertex;
    instanceDesc.count = instanceCount;
    instanceDesc.layout = &instanceLayout;
    instanceDesc.flags = BGFX_BUFFER_COMPUTE_READ_WRITE;
    instanceDesc.name = "shadow.instances";
    m_shadowInstanceBuffer = m_resources->createBuffer(instanceDesc);

    gfx::BufferDesc indirectDesc;
    indirectDesc.type = gfx::BufferType::Indirect;
    indirectDesc.count = recordCount;
    indirectDesc.name = "shadow.indirectArgs";
    m_shadowIndirectBuffer = m_resources->createBuffer(indirectDesc);

    gfx::BufferDesc statsDesc;
    statsDesc.type = gfx::BufferType::Storage;
    statsDesc.count = statsElements;
    statsDesc.flags = BGFX_BUFFER_COMPUTE_READ_WRITE;
    statsDesc.name = "shadow.cullStats";
    m_shadowStatsBuffer = m_resources->createBuffer(statsDesc);

    if (!m_shadowRequestBuffer.valid() || !m_shadowCasterBuffer.valid() || !m_shadowBatchBuffer.valid() ||
        !m_shadowInstanceBuffer.valid() || !m_shadowIndirectBuffer.valid() || !m_shadowStatsBuffer.valid()) {
        HBG_LOGE("gpu shadow: failed to create GPU culling buffers");
        releaseShadowGpuBuffers();
        return false;
    }
    return true;
}

void ForwardRenderer::releaseShadowGpuBuffers() {
    if (m_resources != nullptr) {
        for (gfx::BufferHandle* handle : {&m_shadowRequestBuffer, &m_shadowCasterBuffer, &m_shadowBatchBuffer,
                                          &m_shadowInstanceBuffer, &m_shadowIndirectBuffer, &m_shadowStatsBuffer}) {
            if (handle->valid()) {
                m_resources->destroyBuffer(*handle);
            }
            *handle = {};
        }
    } else {
        m_shadowRequestBuffer = {};
        m_shadowCasterBuffer = {};
        m_shadowBatchBuffer = {};
        m_shadowInstanceBuffer = {};
        m_shadowIndirectBuffer = {};
        m_shadowStatsBuffer = {};
    }
    m_shadowGpuRequestCount = 0;
    m_shadowGpuCasterCount = 0;
    m_shadowGpuBatchCount = 0;
    m_shadowGpuCullingActive = false;
    m_shadowGpuIndirectActive = false;
}

bool ForwardRenderer::updateShadowGpuData(const RenderScene& scene) {
    (void)scene;
    m_shadowGpuCullingActive = false;
    m_shadowGpuIndirectActive = false;
    m_shadowGpuCullDispatches = 0;
    m_shadowIndirectDraws = 0;

    if (!m_pipelineSettings.supportGpuShadowCulling || !m_pipelineSettings.supportGpuShadowIndirect ||
        !m_shadowsEnabled || !shadowsActive() || !m_shadowManager.valid() || m_shadowDrawList.empty() ||
        m_shadowManager.directionalMixedCached()) {
        return false;
    }
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr || (caps->supported & BGFX_CAPS_COMPUTE) == 0 ||
        (caps->supported & BGFX_CAPS_DRAW_INDIRECT) == 0) {
        return false;
    }
    const gfx::ProgramHandle cullProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::ShadowCull, 0, 0});
    if (!cullProgram.valid()) {
        if (!m_shadowGpuWarningLogged) {
            HBG_LOGW("gpu shadow: culling program is unavailable; using CPU shadow culling");
            m_shadowGpuWarningLogged = true;
        }
        return false;
    }

    const std::vector<ShadowRequest>& requests = m_shadowManager.requests();
    const std::vector<DrawBatch>& batches = m_shadowDrawList.batches();
    const uint32_t requestCount = static_cast<uint32_t>(requests.size());
    const uint32_t casterCount = static_cast<uint32_t>(m_shadowDrawList.size());
    const uint32_t batchCount = static_cast<uint32_t>(batches.size());
    if (!ensureShadowGpuBuffers(requestCount, casterCount, batchCount)) {
        return false;
    }

    std::vector<Vec4> requestData(static_cast<std::size_t>(requestCount) * 6u);
    for (uint32_t requestIndex = 0; requestIndex < requestCount; ++requestIndex) {
        for (uint32_t planeIndex = 0; planeIndex < 6u; ++planeIndex) {
            requestData[static_cast<std::size_t>(requestIndex) * 6u + planeIndex] =
                requests[requestIndex].frustumPlanes[planeIndex];
        }
    }

    std::vector<Vec4> casterData(static_cast<std::size_t>(casterCount) * 7u);
    const std::vector<DrawItem>& items = m_shadowDrawList.items();
    for (uint32_t casterIndex = 0; casterIndex < casterCount; ++casterIndex) {
        const DrawItem& item = items[casterIndex];
        const std::size_t base = static_cast<std::size_t>(casterIndex) * 7u;
        casterData[base + 0u] = Vec4(item.world[0], item.world[1], item.world[2], item.world[3]);
        casterData[base + 1u] = Vec4(item.world[4], item.world[5], item.world[6], item.world[7]);
        casterData[base + 2u] = Vec4(item.world[8], item.world[9], item.world[10], item.world[11]);
        casterData[base + 3u] = Vec4(item.world[12], item.world[13], item.world[14], item.world[15]);
        const bool valid = item.worldBounds.valid();
        const Vec3 center = valid ? item.worldBounds.center()
                                  : Vec3(item.world[12], item.world[13], item.world[14]);
        const Vec3 extents = valid ? item.worldBounds.extents() : Vec3(0.0f, 0.0f, 0.0f);
        casterData[base + 4u] = Vec4(center.x, center.y, center.z, 0.0f);
        casterData[base + 5u] = Vec4(extents.x, extents.y, extents.z, 0.0f);
        casterData[base + 6u] = Vec4(valid ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    }

    std::vector<uint32_t> batchData(static_cast<std::size_t>(4u + batchCount * 4u), 0u);
    batchData[0] = requestCount;
    batchData[1] = batchCount;
    batchData[2] = casterCount;
    for (uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
        const DrawBatch& batch = batches[batchIndex];
        const std::size_t base = 4u + static_cast<std::size_t>(batchIndex) * 4u;
        const DrawItem& first = items[batch.firstItem];
        batchData[base + 0u] = batch.firstItem;
        batchData[base + 1u] = batch.itemCount;
        batchData[base + 2u] = first.indexOffset;
        batchData[base + 3u] = first.indexCount;
    }

    const bool updated =
        m_resources->updateBuffer(m_shadowRequestBuffer, requestData.data(),
                                  static_cast<uint32_t>(requestData.size() * sizeof(Vec4))) &&
        m_resources->updateBuffer(m_shadowCasterBuffer, casterData.data(),
                                  static_cast<uint32_t>(casterData.size() * sizeof(Vec4))) &&
        m_resources->updateBuffer(m_shadowBatchBuffer, batchData.data(),
                                  static_cast<uint32_t>(batchData.size() * sizeof(uint32_t)));
    if (!updated) {
        if (!m_shadowGpuWarningLogged) {
            HBG_LOGW("gpu shadow: failed to upload culling data; using CPU shadow culling");
            m_shadowGpuWarningLogged = true;
        }
        return false;
    }

    m_shadowGpuRequestCount = requestCount;
    m_shadowGpuCasterCount = casterCount;
    m_shadowGpuBatchCount = batchCount;
    m_shadowGpuCullDispatches = requestCount * batchCount;
    m_shadowGpuCullingActive = true;
    m_shadowGpuIndirectActive = true;
    return true;
}

void ForwardRenderer::addGpuShadowCullPass(RenderGraph& graph, const FramePassResources& resources) {
    if (!m_shadowGpuCullingActive) {
        return;
    }
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::ShadowCull, 0, 0});
    if (!program.valid() || !resources.shadowRequestBuffer.valid() || !resources.shadowCasterBuffer.valid() ||
        !resources.shadowBatchBuffer.valid() || !resources.shadowInstanceBuffer.valid() ||
        !resources.shadowIndirectBuffer.valid() || !resources.shadowStatsBuffer.valid()) {
        m_shadowGpuCullingActive = false;
        m_shadowGpuIndirectActive = false;
        return;
    }

    graph.addComputePass("GPU Shadow Caster Culling")
        .readBuffer(resources.shadowRequestBuffer)
        .readBuffer(resources.shadowCasterBuffer)
        .readBuffer(resources.shadowBatchBuffer)
        .writeBuffer(resources.shadowInstanceBuffer)
        .writeBuffer(resources.shadowIndirectBuffer)
        .writeBuffer(resources.shadowStatsBuffer)
        .allowCulling(false)
        .setRenderFunc([this, resources, program](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setViewRect(context.viewId(), 0, 0, context.width(), context.height());
            encoder.setViewFrameBuffer(context.viewId(), {});
            encoder.setStorageBuffer(0, context.buffer(resources.shadowRequestBuffer), bgfx::Access::Read);
            encoder.setStorageBuffer(1, context.buffer(resources.shadowCasterBuffer), bgfx::Access::Read);
            encoder.setStorageBuffer(2, context.buffer(resources.shadowInstanceBuffer), bgfx::Access::Write);
            encoder.setStorageBuffer(3, context.buffer(resources.shadowBatchBuffer), bgfx::Access::Read);
            encoder.setStorageBuffer(4, context.buffer(resources.shadowIndirectBuffer), bgfx::Access::Write);
            encoder.setStorageBuffer(5, context.buffer(resources.shadowStatsBuffer), bgfx::Access::Write);
            encoder.dispatch(context.viewId(), program, m_shadowGpuBatchCount, m_shadowGpuRequestCount, 1);
        });
}

bool ForwardRenderer::gpuLightListPathAvailable() {
    if (!m_gpuLightListEnabled || m_resources == nullptr) {
        return false;
    }
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr || (caps->supported & BGFX_CAPS_COMPUTE) == 0 ||
        (caps->supported & BGFX_CAPS_IMAGE_RW) == 0) {
        return false;
    }
    if (!m_lightDataTexture.valid() || !m_lightScreenBoundsTexture.valid() || !m_lightListTexture.valid() ||
        !m_lightBigTileTexture.valid() || !m_lightViewRowsTexture.valid() || !m_lightDataSampler.valid() ||
        !m_lightListSampler.valid() || !m_lightScreenBoundsSampler.valid() || !m_lightBigTileSampler.valid() ||
        !m_lightViewRowsSampler.valid()) {
        return false;
    }
    const bool clusterProgramValid =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightCluster, 0, 0}).valid();
    if (m_lightClusterEnabled && (!m_clusterTexture.valid() || !clusterProgramValid)) {
        return false;
    }
    return m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightScreenAABB, 0, 0}).valid() &&
           m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightBigTile, 0, 0}).valid() &&
           m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightFptl, 0, 0}).valid() &&
           (!m_lightClusterEnabled || clusterProgramValid);
}

void ForwardRenderer::updateLightList() {
    m_lightListCpuMs = 0.0;
    m_gpuLightListActive = false;
    m_gpuLightListBufferActive = false;
    m_gpuLightListIndirectActive = false;
    if (m_pipelineSettings.supportAsyncGpuLightList && !m_asyncGpuLightListWarningLogged) {
        HBG_LOGW("gpu light list: async compute is unavailable in bgfx; keeping the light list synchronous");
        m_asyncGpuLightListWarningLogged = true;
    }
    m_activeLightListBuffer = {};

    const ViewConstants& constants = m_viewState.constants();
    if (m_resources != nullptr && m_lightViewRowsTexture.valid()) {
        const Mat4& viewProj = constants.viewProj;
        const Mat4& view = constants.view;
        const std::array<Vec4, 6> rows = {
            Vec4(viewProj[0], viewProj[4], viewProj[8], viewProj[12]),
            Vec4(viewProj[1], viewProj[5], viewProj[9], viewProj[13]),
            Vec4(viewProj[2], viewProj[6], viewProj[10], viewProj[14]),
            Vec4(viewProj[3], viewProj[7], viewProj[11], viewProj[15]),
            Vec4(view[2], view[6], view[10], view[14]),
            Vec4(0.0f, 0.0f, 0.0f, 0.0f),
        };
        m_resources->updateTexture(m_lightViewRowsTexture, rows.data(), static_cast<uint32_t>(sizeof(rows)), 6, 1,
                                   static_cast<uint16_t>(6 * sizeof(Vec4)));
    }

    const uint16_t renderWidth = m_viewState.renderWidth();
    const uint16_t renderHeight = m_viewState.renderHeight();
    const uint16_t tileCountX = LightListBuilder::tileCount(renderWidth, kLightTileSize);
    const uint16_t tileCountY = LightListBuilder::tileCount(renderHeight, kLightTileSize);
    const uint16_t clusterTileCountX = LightListBuilder::tileCount(renderWidth, kClusterTileSize);
    const uint16_t clusterTileCountY = LightListBuilder::tileCount(renderHeight, kClusterTileSize);
    const bool clusterRequested = m_lightClusterEnabled;
    const bgfx::Caps* caps = bgfx::getCaps();
    const bool computeSupported = caps != nullptr && (caps->supported & BGFX_CAPS_COMPUTE) != 0;
    const auto bufferCount = [this](gfx::BufferHandle handle) {
        const gfx::Buffer* buffer = m_resources != nullptr ? m_resources->buffer(handle) : nullptr;
        return buffer != nullptr && buffer->valid() ? buffer->count() : 0u;
    };

    const gfx::ProgramHandle aabbBufferProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightScreenAABBBuffer, 0, 0});
    const gfx::ProgramHandle bigTileBufferProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightBigTileBuffer, 0, 0});
    const gfx::ProgramHandle fptlBufferProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightFptlBuffer, 0, 0});
    const gfx::ProgramHandle clusterBufferProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightClusterBuffer, 0, 0});
    const gfx::ProgramHandle indirectArgsProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightIndirectArgs, 0, 0});
    const bool indirectRequested = m_gpuLightListBufferEnabled && m_pipelineSettings.supportGpuLightListIndirect;
    const bool indirectReady =
        !indirectRequested || (m_lightIndirectArgsBuffer.valid() && indirectArgsProgram.valid());
    const uint32_t listStride = lightListStrideFor(m_lightListCapacity);
    const uint32_t bufferClusterCapacity =
        LightListBuilder::clusterCapacityFor(renderWidth, renderHeight, m_lightListCapacity);
    const uint32_t bufferClusterIndexCapacity =
        LightListBuilder::clusterIndexCountFor(renderWidth, renderHeight, m_lightListCapacity);
    const uint32_t bufferClusterOffsetCount =
        static_cast<uint32_t>(clusterTileCountX) * clusterTileCountY * kClusterSliceCount * kLightCategoryCount;
    const uint32_t bufferClusterFlagCount =
        static_cast<uint32_t>(clusterTileCountX) * clusterTileCountY;
    const bool bufferAvailable =
        m_gpuLightListEnabled && m_gpuLightListBufferEnabled && computeSupported && m_resources != nullptr &&
        m_lightDataTexture.valid() && m_lightViewRowsTexture.valid() && m_lightScreenBoundsBuffer.valid() &&
        m_lightBigTileBuffer.valid() && m_lightFptlBuffer.valid() &&
        (bufferCount(m_lightScreenBoundsBuffer) == kMaxGpuLights * 2u) &&
        (bufferCount(m_lightBigTileBuffer) ==
         static_cast<uint32_t>(LightListBuilder::tileCount(renderWidth, kLightBigTileSize)) *
             LightListBuilder::tileCount(renderHeight, kLightBigTileSize) * listStride * kLightCategoryCount) &&
        (bufferCount(m_lightFptlBuffer) == static_cast<uint32_t>(tileCountX) * tileCountY * listStride * kLightCategoryCount + m_litTileListCount) &&
        (!clusterRequested ||
         (m_lightClusterBuffer.valid() && bufferClusterCapacity != 0 &&
          bufferCount(m_lightClusterBuffer) ==
              bufferClusterOffsetCount + bufferClusterIndexCapacity + bufferClusterFlagCount)) &&
        aabbBufferProgram.valid() && bigTileBufferProgram.valid() && fptlBufferProgram.valid() &&
        (!clusterRequested || clusterBufferProgram.valid()) && indirectReady && tileCountX > 0 && tileCountY > 0 &&
        clusterTileCountX > 0 && clusterTileCountY > 0;

    const uint32_t textureCapacity = m_lightListTextureCapacity;
    const uint32_t textureClusterCapacity =
        LightListBuilder::clusterCapacityFor(renderWidth, renderHeight, textureCapacity);
    const uint32_t textureClusterIndexCapacity =
        LightListBuilder::clusterIndexCountFor(renderWidth, renderHeight, textureCapacity);
    const uint32_t textureClusterOffsetCount =
        static_cast<uint32_t>(clusterTileCountX) * clusterTileCountY * kClusterSliceCount * kLightCategoryCount;
    const uint32_t textureClusterFlagCount =
        static_cast<uint32_t>(clusterTileCountX) * clusterTileCountY;
    const gfx::Texture* lightListTexture = m_resources != nullptr ? m_resources->texture(m_lightListTexture) : nullptr;
    const gfx::Texture* bigTileTexture =
        m_resources != nullptr ? m_resources->texture(m_lightBigTileTexture) : nullptr;
    const gfx::Texture* clusterLightList =
        m_resources != nullptr ? m_resources->texture(m_clusterTexture) : nullptr;
    const bool textureAvailable =
        gpuLightListPathAvailable() && textureCapacity > 0 && lightListTexture != nullptr &&
        bigTileTexture != nullptr && tileCountX > 0 && tileCountY > 0 && clusterTileCountX > 0 &&
        clusterTileCountY > 0 &&
        lightListTexture->desc().width == LightListBuilder::textureWidthFor(renderWidth, textureCapacity) &&
        lightListTexture->desc().height == LightListBuilder::textureHeightFor(renderHeight) &&
        bigTileTexture->desc().width == LightListBuilder::bigTileTextureWidthFor(renderWidth, textureCapacity) &&
        bigTileTexture->desc().height == LightListBuilder::bigTileTextureHeightFor(renderHeight) &&
        (!clusterRequested ||
         (clusterLightList != nullptr && textureClusterCapacity != 0 &&
          clusterLightList->desc().width == LightListBuilder::clusterIndexTextureWidthFor(
              textureClusterOffsetCount + textureClusterIndexCapacity + textureClusterFlagCount) &&
          clusterLightList->desc().height == LightListBuilder::clusterIndexTextureHeightFor(
              textureClusterOffsetCount + textureClusterIndexCapacity + textureClusterFlagCount)));

    const bool gpuAvailable = bufferAvailable || textureAvailable;
    const uint32_t effectiveCapacity = bufferAvailable ? m_lightListCapacity : textureCapacity;
    if (!bufferAvailable) {
        if (textureCapacity == 0 && !m_lightListCapacityWarningLogged) {
            HBG_LOGW("light list: no texture tier fits {}x{} with maxTextureSize={} and uploadWidthLimit={}; "
                     "texture/CPU path unavailable",
                     renderWidth, renderHeight, backendMaxTextureSize(), kMaxTextureUploadWidth);
            m_lightListCapacityWarningLogged = true;
        } else if (textureCapacity != m_lightListCapacity && !m_lightListCapacityWarningLogged) {
            HBG_LOGW("light list: texture capacity clamped {} -> {} at {}x{} (maxTextureSize={}, "
                     "uploadWidthLimit={})",
                     m_lightListCapacity, textureCapacity, renderWidth, renderHeight, backendMaxTextureSize(),
                     kMaxTextureUploadWidth);
            m_lightListCapacityWarningLogged = true;
        } else if (textureCapacity == m_lightListCapacity) {
            m_lightListCapacityWarningLogged = false;
        }
    } else {
        m_lightListCapacityWarningLogged = false;
    }
    m_lightListEffectiveCapacity = effectiveCapacity;
    m_gpuLightListActive = gpuAvailable;
    m_gpuLightListBufferActive = bufferAvailable;
    m_gpuLightListIndirectActive = bufferAvailable && indirectRequested;

    if (gpuAvailable) {
        m_lightListBuilder.clear();
        m_passUniforms.lightListParams =
            Vec4(static_cast<float>(kLightTileSize), static_cast<float>(tileCountX),
                 static_cast<float>(tileCountY), 1.0f);
        m_passUniforms.lightListParams2 = lightListCapacityParams(effectiveCapacity);
        const float width = static_cast<float>(renderWidth);
        const float height = static_cast<float>(renderHeight);
        m_passUniforms.lightScreenSize = Vec4(width, height, 1.0f / width, 1.0f / height);
        m_clusterUniforms = {};
        m_clusterUniforms.params = Vec4(static_cast<float>(kClusterTileSize),
                                        static_cast<float>(clusterTileCountX),
                                        static_cast<float>(clusterTileCountY),
                                        static_cast<float>(kClusterSliceCount));
        m_clusterUniforms.depthParams =
            Vec4(m_camera.nearClip, m_camera.farClip, LightListBuilder::clusterLogBase(),
                 clusterRequested ? 1.0f : 0.0f);
        m_clusterUniforms.lightCounts = m_passUniforms.lightCounts;
        m_clusterUniforms.screenSize = m_passUniforms.lightScreenSize;
        const uint32_t effectiveClusterCapacity =
            bufferAvailable ? bufferClusterCapacity : textureClusterCapacity;
        const uint32_t effectiveClusterIndexCapacity =
            bufferAvailable ? bufferClusterIndexCapacity : textureClusterIndexCapacity;
        const uint32_t effectiveClusterOffsetCount =
            bufferAvailable ? bufferClusterOffsetCount : textureClusterOffsetCount;
        const uint16_t effectiveClusterWidth = LightListBuilder::clusterIndexTextureWidthFor(
            effectiveClusterOffsetCount + effectiveClusterIndexCapacity + bufferClusterFlagCount);
        m_clusterCapacity = effectiveClusterCapacity;
        m_clusterIndexCapacity = effectiveClusterIndexCapacity;
        m_clusterOffsetCount = effectiveClusterOffsetCount;
        m_clusterTextureWidth = effectiveClusterWidth;
        m_clusterUniforms.lightListParams2 =
            Vec4(static_cast<float>(lightListStrideFor(effectiveCapacity)),
                 static_cast<float>(effectiveClusterCapacity), static_cast<float>(effectiveCapacity),
                 static_cast<float>(effectiveCapacity));
        m_clusterUniforms.params2 =
            Vec4(static_cast<float>(effectiveClusterWidth),
                 static_cast<float>(effectiveClusterOffsetCount),
                 static_cast<float>(effectiveClusterIndexCapacity),
                 static_cast<float>(kLightCategoryCount));
        if (bufferAvailable) {
            m_activeLightListBuffer = m_lightFptlBuffer;
        }
        if (m_frameIndex == 0) {
            HBG_LOGI("light list: {}x{} tiles, {}x{} big tiles, source={}, path={}, capacity={}/{}, "
                     "cluster_capacity={}, cluster_indices={}, gpu_cluster={}",
                     tileCountX, tileCountY, LightListBuilder::tileCount(renderWidth, kLightBigTileSize),
                     LightListBuilder::tileCount(renderHeight, kLightBigTileSize),
                     bufferAvailable ? "gpu" : "gpu", bufferAvailable ? "buffer" : "texture", effectiveCapacity,
                     m_lightListCapacity, effectiveClusterCapacity, effectiveClusterIndexCapacity,
                     clusterRequested ? "true" : "false");
        }
    } else {
        updateCpuLightList();
    }

    if (m_gpuLightListEnabled && !bufferAvailable && m_gpuLightListBufferEnabled && textureAvailable &&
        !m_gpuLightListWarningLogged) {
        HBG_LOGW("gpu light list: buffer path unavailable; using the texture path");
        m_gpuLightListWarningLogged = true;
    }
    if (m_gpuLightListEnabled && !gpuAvailable && !m_gpuLightListWarningLogged) {
        HBG_LOGW("gpu light list: requested but compute/program/texture unavailable; using CPU fallback");
        m_gpuLightListWarningLogged = true;
    }
    updateLightListFeatureFlags();
}

void ForwardRenderer::updateLightListFeatureFlags() {
    const bool available = m_pipelineSettings.supportLightListFeatureFlags &&
                           m_passUniforms.lightListParams.w > 0.5f &&
                           (m_gpuLightListActive || m_lightListBuilder.data().enabled);
    m_passUniforms.lightListParams2.z = available ? 1.0f : 0.0f;
}

void ForwardRenderer::updateCpuLightList() {
    if (m_lightListTextureCapacity == 0) {
        m_lightListBuilder.clear();
        m_lightListEffectiveCapacity = 0;
        m_passUniforms.lightListParams = Vec4(static_cast<float>(kLightTileSize), 0.0f, 0.0f, 0.0f);
        m_passUniforms.lightListParams2 = lightListCapacityParams(0);
        updateLightListFeatureFlags();
        return;
    }
    m_lightListEffectiveCapacity = m_lightListTextureCapacity;
    const float width = static_cast<float>(m_viewState.renderWidth());
    const float height = static_cast<float>(m_viewState.renderHeight());
    m_passUniforms.lightScreenSize =
        Vec4(width, height, width > 0.0f ? 1.0f / width : 0.0f, height > 0.0f ? 1.0f / height : 0.0f);
    const ViewConstants& constants = m_viewState.constants();
    const auto buildStart = std::chrono::steady_clock::now();
    m_lightListBuilder.build(m_lightBuilder.data(), constants.viewProj, constants.view, m_camera.nearClip,
                             m_camera.farClip, m_viewState.renderWidth(), m_viewState.renderHeight(),
                             m_lightListTextureCapacity);
    const auto buildEnd = std::chrono::steady_clock::now();
    m_lightListCpuMs += std::chrono::duration<double, std::milli>(buildEnd - buildStart).count();
    uploadCpuLightList();
    uploadClusterLightList();
    updateClusterStatsFromCpu();
    updateLightListFeatureFlags();
    const GpuLightListData& list = m_lightListBuilder.data();
    if (m_frameIndex == 0 && m_passUniforms.lightListParams.w > 0.5f && list.enabled) {
        HBG_LOGI("light list: {}x{} tiles, {}x{} big tiles, max lights tile={}, big tile={}, source={}",
                 list.tileCountX, list.tileCountY, list.bigTileCountX, list.bigTileCountY, list.maxLightsPerTile,
                 list.maxLightsPerBigTile, m_gpuLightListEnabled ? "cpu_fallback" : "cpu");
    }
}

bool ForwardRenderer::uploadCpuLightList() {
    const GpuLightListData& list = m_lightListBuilder.data();
    const gfx::Texture* lightListTexture = m_resources != nullptr ? m_resources->texture(m_lightListTexture) : nullptr;
    const uint32_t capacity = list.capacity != 0 ? list.capacity : m_lightListTextureCapacity;
    m_passUniforms.lightListParams2 = lightListCapacityParams(capacity);
    if (!list.enabled || lightListTexture == nullptr || list.width != lightListTexture->desc().width ||
        list.height != lightListTexture->desc().height) {
        m_passUniforms.lightListParams = Vec4(static_cast<float>(kLightTileSize), 0.0f, 0.0f, 0.0f);
        return false;
    }

    const uint16_t pitch = static_cast<uint16_t>(list.width * sizeof(uint32_t));
    if (!m_resources->updateTexture(m_lightListTexture, list.texels.data(),
                                    static_cast<uint32_t>(list.texels.size() * sizeof(uint32_t)), list.width,
                                    list.height, pitch)) {
        m_passUniforms.lightListParams = Vec4(static_cast<float>(kLightTileSize), 0.0f, 0.0f, 0.0f);
        return false;
    }

    m_passUniforms.lightListParams =
        Vec4(static_cast<float>(kLightTileSize), static_cast<float>(list.tileCountX),
             static_cast<float>(list.tileCountY), 1.0f);
    return true;
}

void ForwardRenderer::uploadClusterLightList() {
    const GpuLightClusterData& cluster = m_lightListBuilder.clusterData();
    const gfx::Texture* clusterTexture =
        m_resources != nullptr ? m_resources->texture(m_clusterTexture) : nullptr;
    const uint32_t capacity = m_lightListTextureCapacity != 0 ? m_lightListTextureCapacity
                                                              : m_lightListBuilder.data().capacity;
    const auto disableCluster = [this, capacity]() {
        m_clusterUniforms = {};
        m_clusterUniforms.params = Vec4(static_cast<float>(kClusterTileSize), 0.0f, 0.0f,
                                        static_cast<float>(kClusterSliceCount));
        m_clusterUniforms.depthParams = Vec4(m_camera.nearClip, m_camera.farClip, LightListBuilder::clusterLogBase(),
                                             0.0f);
        m_clusterUniforms.lightListParams2 = lightListCapacityParams(capacity);
    };
    if (!cluster.enabled || clusterTexture == nullptr || cluster.offsets.empty()) {
        disableCluster();
        return;
    }

    const uint16_t width = clusterTexture->desc().width;
    const uint32_t offsetCount = static_cast<uint32_t>(cluster.offsets.size());
    const uint32_t flagCount = static_cast<uint32_t>(cluster.tileCountX) * cluster.tileCountY;
    const uint32_t combinedCount = offsetCount + cluster.indexCapacity + flagCount;
    const uint16_t height = static_cast<uint16_t>((combinedCount + width - 1u) / width);
    std::vector<uint32_t> words(static_cast<std::size_t>(width) * height, 0u);
    std::copy(cluster.offsets.begin(), cluster.offsets.end(), words.begin());
    std::copy(cluster.indices.begin(), cluster.indices.end(), words.begin() + offsetCount);
    if (cluster.tileFeatureFlags.size() == flagCount) {
        std::copy(cluster.tileFeatureFlags.begin(), cluster.tileFeatureFlags.end(),
                  words.begin() + offsetCount + cluster.indexCapacity);
    }
    if (!m_resources->updateTexture(m_clusterTexture, words.data(),
                                    static_cast<uint32_t>(words.size() * sizeof(uint32_t)), width, height,
                                    static_cast<uint16_t>(width * sizeof(uint32_t)))) {
        disableCluster();
        return;
    }

    m_clusterCapacity = cluster.capacity;
    m_clusterIndexCapacity = cluster.indexCapacity;
    m_clusterOffsetCount = offsetCount;
    m_clusterTextureWidth = width;
    m_clusterUniforms.params = Vec4(static_cast<float>(kClusterTileSize), static_cast<float>(cluster.tileCountX),
                                    static_cast<float>(cluster.tileCountY), static_cast<float>(kClusterSliceCount));
    m_clusterUniforms.depthParams =
        Vec4(m_camera.nearClip, m_camera.farClip, LightListBuilder::clusterLogBase(), 1.0f);
    m_clusterUniforms.lightCounts = m_passUniforms.lightCounts;
    m_clusterUniforms.screenSize = m_passUniforms.lightScreenSize;
    m_clusterUniforms.lightListParams2 =
        Vec4(static_cast<float>(lightListStrideFor(capacity)), static_cast<float>(cluster.capacity),
             static_cast<float>(capacity), static_cast<float>(capacity));
    m_clusterUniforms.params2 =
        Vec4(static_cast<float>(width), static_cast<float>(offsetCount),
             static_cast<float>(cluster.indexCapacity), static_cast<float>(kLightCategoryCount));
    if (m_frameIndex == 0) {
        HBG_LOGI("light cluster: {}x{} tiles x {} slices, texture={}x{}, used={}/{}, max lights cluster={}, "
                 "truncated clusters={}",
                 cluster.tileCountX, cluster.tileCountY, kClusterSliceCount, width, height,
                 cluster.usedIndexCount, cluster.indexCapacity, cluster.maxLightsPerCluster,
                 cluster.truncatedClusters);
    }
}

void ForwardRenderer::requestClusterStatsReadback() {
    if (m_clusterStatsReadbackPending) {
        return;
    }
    m_clusterStatsReadbackRequested = true;
    m_clusterStatsReferenceValid = false;
}

void ForwardRenderer::updateClusterStatsFromCpu() {
    const GpuLightClusterData& cluster = m_lightListBuilder.clusterData();
    if (!cluster.enabled) {
        m_clusterStats = {};
        return;
    }
    m_clusterStats = summarizeClusterOffsets(cluster.offsets);
    m_clusterStats.gpu = false;
}

void ForwardRenderer::pollClusterStatsReadback() {
    if (!m_clusterStatsReadbackPending || m_frameIndex < m_clusterStatsReadbackFrame) {
        return;
    }
    if (m_clusterStatsReadbackData.size() > m_clusterStatsReadbackTotalCount) {
        m_clusterStatsReadbackData.resize(m_clusterStatsReadbackTotalCount);
    }
    const std::size_t offsetCount =
        std::min<std::size_t>(m_clusterStatsReadbackOffsetCount, m_clusterStatsReadbackData.size());
    const std::vector<uint32_t> offsets(m_clusterStatsReadbackData.begin(),
                                        m_clusterStatsReadbackData.begin() + offsetCount);
    m_clusterStats = summarizeClusterOffsets(offsets);
    m_clusterStats.gpu = true;
    m_clusterStatsReadbackPending = false;
    m_clusterStatsReadbackRequested = false;
    m_clusterStatsReadbackFrame = 0;
    m_clusterStatsReferenceValid = false;
    if (m_clusterStats.valid) {
        uint32_t offsetMismatch = 0;
        for (uint32_t cluster = 0; cluster < m_clusterStats.clusters; ++cluster) {
            const uint32_t packed = m_clusterStatsReadbackData[cluster];
            const uint32_t count = unpackClusterCount(packed);
            const uint32_t offset = unpackClusterOffset(packed);
            if (count > 0 && offset != cluster * m_clusterCapacity) {
                ++offsetMismatch;
            }
        }
        const GpuLightClusterData& reference = m_clusterStatsReferenceBuilder.clusterData();
        uint32_t flagMismatch = 0;
        uint32_t flagsChecked = 0;
        if (m_clusterStatsReadbackFlagCount > 0 &&
            m_clusterStatsReadbackFlagOffset + m_clusterStatsReadbackFlagCount <=
                m_clusterStatsReadbackData.size() &&
            reference.tileFeatureFlags.size() >= m_clusterStatsReadbackFlagCount) {
            flagsChecked = m_clusterStatsReadbackFlagCount;
            for (uint32_t index = 0; index < flagsChecked; ++index) {
                if (m_clusterStatsReadbackData[m_clusterStatsReadbackFlagOffset + index] !=
                    reference.tileFeatureFlags[index]) {
                    ++flagMismatch;
                }
            }
        }
        m_clusterStats.featureFlagsChecked = flagsChecked;
        m_clusterStats.featureFlagMismatch = flagMismatch;
        HBG_LOGI("cluster stats readback: path={} clusters={} active={} max_lights={} offset_mismatch={} "
                 "feature_flags_checked={} feature_flag_mismatch={}",
                 m_clusterStatsReadbackTexturePath ? "gpu_texture" : "gpu_buffer", m_clusterStats.clusters,
                 m_clusterStats.activeClusters, m_clusterStats.maxLights, offsetMismatch, flagsChecked,
                 flagMismatch);
    }
}

bool ForwardRenderer::ensureClusterStatsReadbackTexture(uint16_t width, uint16_t height) {
    if (m_resources == nullptr || width == 0 || height == 0) {
        return false;
    }
    const gfx::Texture* current = m_resources->texture(m_clusterStatsReadbackTexture);
    if (current != nullptr && current->desc().width == width && current->desc().height == height) {
        return true;
    }
    releaseClusterStatsReadbackTexture();
    gfx::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = gfx::TextureFormat::R32U;
    desc.flags = BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    desc.name = "lighting.clusterStatsReadback";
    m_clusterStatsReadbackTexture = m_resources->createTexture(desc);
    if (!m_clusterStatsReadbackTexture.valid()) {
        HBG_LOGE("light list: failed to create cluster stats readback texture ({}x{})", width, height);
        return false;
    }
    return true;
}

void ForwardRenderer::releaseClusterStatsReadbackTexture() {
    if (m_resources != nullptr && m_clusterStatsReadbackTexture.valid()) {
        m_resources->destroyTexture(m_clusterStatsReadbackTexture);
    }
    m_clusterStatsReadbackTexture = {};
}

void ForwardRenderer::submitClusterStatsReadback() {
    if (!m_clusterStatsReadbackRequested || m_clusterStatsReadbackPending || !m_gpuLightListActive ||
        !m_lightClusterEnabled || m_resources == nullptr) {
        return;
    }

    if (!m_clusterStatsReferenceValid) {
        m_clusterStatsReferenceBuilder.build(
            m_lightBuilder.data(), m_viewState.constants().viewProj, m_viewState.constants().view,
            m_camera.nearClip, m_camera.farClip, m_viewState.renderWidth(), m_viewState.renderHeight(),
            m_lightListEffectiveCapacity);
        m_clusterStatsReferenceValid = true;
    }
    const GpuLightClusterData& reference = m_clusterStatsReferenceBuilder.clusterData();
    m_clusterStatsReadbackOffsetCount = m_clusterOffsetCount;
    m_clusterStatsReadbackFlagCount =
        reference.enabled ? static_cast<uint32_t>(reference.tileFeatureFlags.size()) : 0u;
    m_clusterStatsReadbackFlagOffset = m_clusterOffsetCount + m_clusterIndexCapacity;
    m_clusterStatsReadbackTotalCount = m_clusterStatsReadbackFlagOffset + m_clusterStatsReadbackFlagCount;

    if (m_gpuLightListBufferActive) {
        const gfx::Buffer* buffer = m_resources->buffer(m_lightClusterBuffer);
        if (buffer == nullptr || !buffer->valid() ||
            buffer->size() < m_clusterStatsReadbackTotalCount * sizeof(uint32_t)) {
            return;
        }
        m_clusterStatsReadbackData.resize(m_clusterStatsReadbackTotalCount);
        m_clusterStatsReadbackTexturePath = false;
        m_clusterStatsReadbackFrame =
            bgfx::read({.handle = m_resources->native(m_lightClusterBuffer), .offset = 0,
                        .size = m_clusterStatsReadbackTotalCount * sizeof(uint32_t)},
                       m_clusterStatsReadbackData.data());
        m_clusterStatsReadbackPending = m_clusterStatsReadbackFrame != 0;
        return;
    }

    const gfx::Texture* clusterTexture = m_resources->texture(m_clusterTexture);
    if (clusterTexture == nullptr) {
        return;
    }
    const uint16_t width = clusterTexture->desc().width;
    const uint16_t height = clusterTexture->desc().height;
    if (!ensureClusterStatsReadbackTexture(width, height)) {
        return;
    }
    m_clusterStatsReadbackData.resize(static_cast<std::size_t>(width) * height);
    m_clusterStatsReadbackTexturePath = true;
    const bgfx::TextureHandle destination = m_resources->native(m_clusterStatsReadbackTexture);
    const bgfx::TextureHandle source = clusterTexture->native();
    bgfx::blit(kClusterStatsBlitView, {.handle = destination, .width = width, .height = height},
               {.handle = source, .width = width, .height = height});
    bgfx::touch(kClusterStatsBlitView);
    m_clusterStatsReadbackFrame =
        bgfx::read({.handle = destination, .width = width, .height = height}, m_clusterStatsReadbackData.data());
    m_clusterStatsReadbackPending = m_clusterStatsReadbackFrame != 0;
}

void ForwardRenderer::releaseLightListTexture() {
    if (m_resources != nullptr && m_lightListTexture.valid()) {
        m_resources->destroyTexture(m_lightListTexture);
    }
    m_lightListTexture = {};
}

void ForwardRenderer::releaseLightBigTileTexture() {
    if (m_resources != nullptr && m_lightBigTileTexture.valid()) {
        m_resources->destroyTexture(m_lightBigTileTexture);
    }
    m_lightBigTileTexture = {};
}

void ForwardRenderer::releaseClusterLightListTextures() {
    if (m_resources != nullptr) {
        if (m_clusterTexture.valid()) {
            m_resources->destroyTexture(m_clusterTexture);
        }
    }
    m_clusterTexture = {};
}

const DrawList& ForwardRenderer::forwardDrawList(bool transparent) const {
    return transparent ? m_transparentDrawList : m_opaqueDrawList;
}

ForwardRenderer::FrameDiagnostics ForwardRenderer::frameDiagnostics() const {
    FrameDiagnostics diagnostics;
    diagnostics.passCount = m_renderGraph.diagnostics().livePasses;
    diagnostics.gbufferTargets =
        m_deferredActive ? static_cast<uint32_t>(m_gbufferTextures.size())
                          : 0u;
    diagnostics.deferred = m_deferredActive;
    diagnostics.deferredAdvanced = m_deferredActive && m_advancedLightingActive;
    diagnostics.bakedGIActive = m_bakedGIActive;
    diagnostics.bakedGIObjects = static_cast<uint32_t>(m_bakedGIDrawList.size());
    diagnostics.litVariantsRequested = m_litVariantsEnabled;
    diagnostics.litVariantsActive = m_litVariantsActive;
    diagnostics.litVariantTilesRequested = m_litVariantTilesEnabled;
    diagnostics.litVariantTilesActive = m_litVariantTileActive;
    diagnostics.litVariantLightFlags = m_litLightFlags;
    diagnostics.stencil = m_deferredActive && selectDepthFormat() == gfx::TextureFormat::D24S8;
    diagnostics.postProcessPasses = m_postProcessChain.postProcessPassCount();
    diagnostics.postProcessActive =
        m_postProcessChain.effectsActive() || m_postProcessChain.exposureActive();
    diagnostics.postProcessComputeSupported = m_postProcessChain.computeSupported();
    diagnostics.postProcessHistoryValid =
        m_postProcessChain.historyValid() || m_postProcessChain.exposureHistoryValid() ||
        m_postProcessChain.taaHistoryValid();
    diagnostics.postProcessTaaActive = m_postProcessChain.taaActive();
    diagnostics.postProcessTaaHistoryValid = m_postProcessChain.taaHistoryValid();
    diagnostics.postProcessTaaHistoryFrames = m_postProcessChain.taaHistoryFrameCount();
    diagnostics.postProcessWallMs =
        m_postProcessWallFrames > 0
            ? static_cast<float>(m_postProcessWallTotalMs / static_cast<double>(m_postProcessWallFrames))
            : 0.0f;
    diagnostics.postProcessExposureActive = m_postProcessChain.exposureActive();
    diagnostics.postProcessUberActive = m_postProcessChain.uberActive();
    diagnostics.postProcessBloomActive = m_postProcessChain.bloomActive();
    diagnostics.postProcessDofActive = m_postProcessChain.dofActive();
    diagnostics.postProcessMotionBlurActive = m_postProcessChain.motionBlurActive();
    diagnostics.postProcessFxaaActive = m_postProcessChain.fxaaActive();
    diagnostics.volumetricFogActive = m_volumetricFogActive;
    diagnostics.volumetricFogSteps = m_volumetricFogSteps;
    diagnostics.volumetricFogWidth = m_volumetricFogWidth;
    diagnostics.volumetricFogHeight = m_volumetricFogHeight;
    diagnostics.volumetricFogScale = m_volumetricFogSettings.resolutionScale;
    diagnostics.volumetricFogTemporal = m_volumetricFogTemporalActive;
    diagnostics.volumetricFogHistoryValid = m_fogHistoryValid;
    diagnostics.ssaoTemporal = m_ssaoTemporalActive;
    diagnostics.ssaoHistoryValid = m_ssaoHistoryValid;
    diagnostics.ssaoHistoryFrames = m_ssaoHistoryFrame;
    diagnostics.ssrTemporal = m_ssrTemporalActive;
    diagnostics.ssrHistoryValid = m_ssrHistoryValid;
    diagnostics.ssrHistoryFrames = m_ssrHistoryFrame;
    diagnostics.ssgiTemporal = m_ssgiTemporalActive;
    diagnostics.ssgiHistoryValid = m_ssgiHistoryValid;
    diagnostics.ssgiHistoryFrames = m_ssgiHistoryFrame;
    diagnostics.volumetricFogPunctualLights = m_volumetricFogPunctualLights;
    diagnostics.volumetricFogLocalVolumes = m_volumetricFogLocalVolumes;
    diagnostics.volumetricFogLocalMasks = m_volumetricFogLocalMasks;
    diagnostics.shadowDrawnItems = m_shadowDrawnItems;
    diagnostics.shadowCulledItems = m_shadowCulledItems;
    diagnostics.shadowDirectionalCacheActive = m_shadowManager.directionalCacheActive();
    diagnostics.shadowDirectionalMixedCached = m_shadowManager.directionalMixedCached();
    diagnostics.shadowCachedDirectionalRequests = m_shadowManager.cachedDirectionalRequestCount();
    diagnostics.shadowCacheActive = m_shadowManager.cacheActive();
    diagnostics.shadowCachedRequests = m_shadowManager.cachedRequestCount();
    diagnostics.shadowAreaCacheActive = m_shadowManager.areaCacheActive();
    diagnostics.shadowCachedAreaRequests = m_shadowManager.cachedAreaRequestCount();
    diagnostics.gpuShadowCulling = m_shadowGpuCullingActive;
    diagnostics.gpuShadowIndirect = m_shadowGpuIndirectActive;
    diagnostics.gpuShadowCullDispatches = m_shadowGpuCullDispatches;
    diagnostics.gpuShadowIndirectDraws = m_shadowIndirectDraws;
    diagnostics.contactShadowsActive = contactShadowsActive();
    diagnostics.contactShadowWidth = m_contactShadowWidth;
    diagnostics.contactShadowHeight = m_contactShadowHeight;
    diagnostics.gpuLightListRequested = m_gpuLightListEnabled;
    diagnostics.gpuLightListActive = m_gpuLightListActive;
    diagnostics.gpuLightListFallback = m_gpuLightListEnabled && !m_gpuLightListActive;
    diagnostics.gpuLightListBufferPath = m_gpuLightListBufferActive;
    diagnostics.gpuLightListIndirect = m_gpuLightListIndirectActive;
    diagnostics.gpuLightListPasses = m_gpuLightListPasses;
    diagnostics.lightListCpuMs = static_cast<float>(m_lightListCpuMs);
    diagnostics.lightListCapacityRequested = m_pipelineSettings.lightListCapacity;
    diagnostics.lightListCapacityEffective = m_lightListEffectiveCapacity;
    diagnostics.lightListStride =
        m_lightListEffectiveCapacity != 0 ? lightListStrideFor(m_lightListEffectiveCapacity) : 0;
    diagnostics.lightListTruncated = m_lightBuilder.data().truncatedLightCount;
    diagnostics.lightListCapacityClamped =
        m_lightListEffectiveCapacity != 0 && m_lightListEffectiveCapacity < m_pipelineSettings.lightListCapacity;
    diagnostics.lightListFeatureFlagsActive = m_passUniforms.lightListParams2.z > 0.5f;
    diagnostics.asyncGpuLightListRequested = m_pipelineSettings.supportAsyncGpuLightList;
    diagnostics.asyncGpuLightListActive = false;
    diagnostics.clusterStatsValid = m_clusterStats.valid;
    diagnostics.clusterStatsGpu = m_clusterStats.gpu;
    diagnostics.clusterStatsReadbackPending = m_clusterStatsReadbackPending;
    diagnostics.clusterStatsClusters = m_clusterStats.clusters;
    diagnostics.clusterStatsActiveClusters = m_clusterStats.activeClusters;
    diagnostics.clusterStatsOverflowClusters = m_clusterStats.overflowClusters;
    diagnostics.clusterStatsOverflowLights = m_clusterStats.overflowLights;
    diagnostics.clusterStatsMaxOverflow = m_clusterStats.maxOverflow;
    diagnostics.clusterStatsMaxLights = m_clusterStats.maxLights;
    diagnostics.clusterStatsFeatureFlagsChecked = m_clusterStats.featureFlagsChecked;
    diagnostics.clusterStatsFeatureFlagMismatch = m_clusterStats.featureFlagMismatch;
    diagnostics.vfxActive = m_vfxActive;
    if (m_vfxRuntime != nullptr) {
        const ::vfx::Diagnostics vfxDiagnostics = m_vfxRuntime->diagnostics();
        diagnostics.vfxEffects = vfxDiagnostics.effects;
        diagnostics.vfxSystems = vfxDiagnostics.systems;
        diagnostics.vfxParticles = vfxDiagnostics.particles;
        diagnostics.vfxDraws = vfxDiagnostics.draws;
        diagnostics.vfxDispatches = vfxDiagnostics.dispatches;
        diagnostics.vfxSortPasses = vfxDiagnostics.sortPasses;
        diagnostics.vfxEvents = vfxDiagnostics.eventCount;
        diagnostics.vfxUpdateMs = vfxDiagnostics.updateMs;
        diagnostics.vfxSimulationMs = vfxDiagnostics.simulationMs;
        diagnostics.vfxOutputMs = vfxDiagnostics.outputMs;
        diagnostics.vfxMs = vfxDiagnostics.totalMs;
    }
    return diagnostics;
}

void ForwardRenderer::buildDrawLists(const RenderScene& scene) {
    DrawListDesc desc;
    desc.passTags = kForwardPassMask;

    if (m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects)) {
        desc.queueRange = kRenderQueueRangeAllOpaque;
        desc.sortingCriteria = kOpaqueSortingCriteria;
        desc.frustumCull = true;
        m_opaqueDrawList.build(scene, m_viewState, desc);

        desc.passTags = kForwardOnlyOpaquePassMask;
        m_forwardOpaqueDrawList.build(scene, m_viewState, desc);

        desc.passTags = kForwardPassMask;
        desc.requireLightmap = true;
        m_bakedGIDrawList.build(scene, m_viewState, desc);
        desc.requireLightmap = false;
    } else {
        m_opaqueDrawList.clear();
        m_forwardOpaqueDrawList.clear();
        m_bakedGIDrawList.clear();
    }

    if (m_frameSettings.isEnabled(FrameSettingsField::TransparentObjects)) {
        desc.passTags = kForwardPassMask;
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

        desc.passTags = shaderPassMask(ShaderPass::GBuffer);
        m_gbufferDrawList.build(scene, m_viewState, desc);
    } else {
        m_prepassDrawList.clear();
        m_gbufferDrawList.clear();
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
        desc.shadowCasterFilter = ShadowCasterFilter::All;
        m_shadowDrawList.build(scene, m_viewState, desc);
        if (m_shadowManager.directionalMixedCached()) {
            desc.shadowCasterFilter = ShadowCasterFilter::StaticOnly;
            m_shadowStaticDrawList.build(scene, m_viewState, desc);
            desc.shadowCasterFilter = ShadowCasterFilter::DynamicOnly;
            m_shadowDynamicDrawList.build(scene, m_viewState, desc);
        } else {
            m_shadowStaticDrawList.clear();
            m_shadowDynamicDrawList.clear();
        }
    } else {
        m_shadowDrawList.clear();
        m_shadowStaticDrawList.clear();
        m_shadowDynamicDrawList.clear();
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

    m_advancedMaterialActive = false;
    const uint32_t advancedMask = asset::MaterialFeatureClearCoat | asset::MaterialFeatureTransmission |
                                  asset::MaterialFeatureIridescence | asset::MaterialFeatureAnisotropy |
                                  asset::MaterialFeatureSubsurface;
    const std::vector<GpuMaterial>& materials = m_materialSystem.materials();
    for (const DrawItem& item : m_opaqueDrawList.items()) {
        if (item.materialIndex < materials.size() &&
            (materials[item.materialIndex].features & advancedMask) != 0) {
            m_advancedMaterialActive = true;
            break;
        }
    }
}

void ForwardRenderer::addShadowPass(RenderGraph& graph, RenderGraphTextureHandle directionalShadow,
                                    RenderGraphTextureHandle cachedDirectionalShadow,
                                    RenderGraphTextureHandle punctualShadow, RenderGraphTextureHandle areaShadow,
                                    const RenderScene& scene) {
    if (!shadowsActive() || !m_shadowManager.valid()) {
        return;
    }
    const std::vector<ShadowRequest>& requests = m_shadowManager.requests();
    const bool mixedDirectional = m_shadowManager.directionalMixedCached();

    if (mixedDirectional) {
        for (uint32_t index = 0; index < requests.size(); ++index) {
            const ShadowRequest& request = requests[index];
            if (request.atlas != ShadowAtlasType::Directional || !cachedDirectionalShadow.valid()) {
                continue;
            }
            if (request.isCached) {
                RenderGraphBuilder pass = graph.addRasterPass("Shadow Cached " + std::to_string(index));
                pass.setDepthAttachment(cachedDirectionalShadow);
                pass.setRenderFunc([this, request](RenderGraphContext& context) {
                    m_shadowManager.beginCachedPass(context.encoder(), context.viewId(), context.frameBuffer(),
                                                    request.atlasSize);
                });
                continue;
            }
            RenderGraphBuilder pass = graph.addRasterPass("Shadow Static " + std::to_string(index));
            pass.setDepthAttachment(cachedDirectionalShadow);
            pass.setRenderFunc([this, &scene, index](RenderGraphContext& context) {
                renderShadowPass(context.encoder(), context.viewId(), context.frameBuffer(), scene, index,
                                 m_shadowStaticDrawList, true);
            });
        }
        addShadowCopyPass(graph, directionalShadow, cachedDirectionalShadow,
                          m_shadowManager.directionalAtlasSize());
        for (uint32_t index = 0; index < requests.size(); ++index) {
            const ShadowRequest& request = requests[index];
            if (request.atlas != ShadowAtlasType::Directional || !request.enabled) {
                continue;
            }
            RenderGraphBuilder pass = graph.addRasterPass("Shadow Dynamic " + std::to_string(index));
            pass.setDepthAttachment(directionalShadow);
            pass.setRenderFunc([this, &scene, index](RenderGraphContext& context) {
                renderShadowPass(context.encoder(), context.viewId(), context.frameBuffer(), scene, index,
                                 m_shadowDynamicDrawList, false);
            });
        }
    }

    for (uint32_t index = 0; index < requests.size(); ++index) {
        const ShadowRequest& request = requests[index];
        if (request.atlas == ShadowAtlasType::Directional) {
            if (mixedDirectional || !directionalShadow.valid()) {
                continue;
            }
            if (request.isCached) {
                RenderGraphBuilder pass = graph.addRasterPass("Shadow Cached " + std::to_string(index));
                pass.setDepthAttachment(directionalShadow);
                pass.setRenderFunc([this, request](RenderGraphContext& context) {
                    m_shadowManager.beginCachedPass(context.encoder(), context.viewId(), context.frameBuffer(),
                                                    request.atlasSize);
                });
                continue;
            }
            RenderGraphBuilder pass = graph.addRasterPass("Shadow " + std::to_string(index));
            pass.setDepthAttachment(directionalShadow);
            pass.setRenderFunc([this, &scene, index](RenderGraphContext& context) {
                renderShadowPass(context.encoder(), context.viewId(), context.frameBuffer(), scene, index,
                                 m_shadowDrawList, true);
            });
            continue;
        }
        const RenderGraphTextureHandle atlas =
            request.atlas == ShadowAtlasType::Punctual ? punctualShadow : areaShadow;
        if (!atlas.valid()) {
            continue;
        }
        if (request.isCached) {
            RenderGraphBuilder pass = graph.addRasterPass("Shadow Cached " + std::to_string(index));
            pass.setDepthAttachment(atlas);
            pass.setRenderFunc([this, request](RenderGraphContext& context) {
                m_shadowManager.beginCachedPass(context.encoder(), context.viewId(), context.frameBuffer(),
                                                request.atlasSize);
            });
            continue;
        }
        RenderGraphBuilder pass = graph.addRasterPass("Shadow " + std::to_string(index));
        pass.setDepthAttachment(atlas);
        pass.setRenderFunc([this, &scene, index](RenderGraphContext& context) {
            renderShadowPass(context.encoder(), context.viewId(), context.frameBuffer(), scene, index,
                             m_shadowDrawList, true);
        });
    }
}

void ForwardRenderer::addShadowCopyPass(RenderGraph& graph, RenderGraphTextureHandle directionalShadow,
                                        RenderGraphTextureHandle cachedDirectionalShadow, uint16_t atlasSize) {
    if (!directionalShadow.valid() || !cachedDirectionalShadow.valid() || !m_fullscreenVertexBuffer.valid()) {
        return;
    }
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::ShadowCopyDepth, 0, 0});
    if (!program.valid() || !m_shadowCopySampler.valid()) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("Shadow Cached Copy");
    pass.readTexture(cachedDirectionalShadow);
    pass.setDepthAttachment(directionalShadow);
    const Vec4 copyParams(static_cast<float>(atlasSize), static_cast<float>(atlasSize),
                          1.0f / static_cast<float>(atlasSize), 1.0f / static_cast<float>(atlasSize));
    pass.setRenderFunc([this, atlasSize, copyParams, program, cachedDirectionalShadow](
                           RenderGraphContext& context) {
        gfx::Encoder& encoder = context.encoder();
        const bgfx::ViewId view = context.viewId();
        encoder.setViewRect(view, 0, 0, atlasSize, atlasSize);
        encoder.setViewFrameBuffer(view, context.frameBuffer());
        encoder.setViewClear(view, BGFX_CLEAR_NONE, 0x000000ff, 1.0f, 0);
        encoder.touch(view);
        encoder.setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_ALWAYS);
        encoder.setUniform(m_shadowCopyParamsUniform, &copyParams.x);
        encoder.setTexture(0, m_shadowCopySampler, context.texture(cachedDirectionalShadow),
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
        encoder.submit(view, program);
    });
}

void ForwardRenderer::addShadowMomentPasses(RenderGraph& graph, RenderGraphTextureHandle directionalShadow) {
    if (!directionalShadow.valid()) {
        return;
    }
    const bool vsmActive = m_shadowManager.directionalMomentBlurActive();
    const bool msmActive = m_shadowManager.directionalMsmBlurActive();
    if (vsmActive || msmActive) {
        const gfx::ProgramHandle blurProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::ShadowEvsmBlur, 0, 0});
        const gfx::TextureHandle momentTexture =
            vsmActive ? m_shadowManager.directionalMomentBlurTexture()
                      : m_shadowManager.directionalMsmMomentTexture();
        if (!blurProgram.valid() || !momentTexture.valid()) {
            return;
        }
        const ShadowFilter momentFilter = vsmActive ? ShadowFilter::Vsm : ShadowFilter::Msm;
        const std::string momentLabel = vsmActive ? "VSM" : "MSM";
        const Vec4 params(0.0f, 0.0f, 0.0f, vsmActive ? 1.0f : 2.0f);
        const RenderGraphTextureHandle moment = graph.importTexture(
            momentTexture, vsmActive ? "shadow.directionalVsmMoments" : "shadow.directionalMsmMoments");
        for (const ShadowRequest& request : m_shadowManager.requests()) {
            if (!request.enabled || request.atlas != ShadowAtlasType::Directional ||
                request.filter != momentFilter) {
                continue;
            }
            const Vec4 slot(static_cast<float>(request.viewportWidth),
                            static_cast<float>(request.viewportHeight),
                            static_cast<float>(request.viewportX),
                            static_cast<float>(request.viewportY));
            const uint16_t atlasSize = request.atlasSize;
            const uint32_t halfWidth = (request.viewportWidth + 1u) / 2u;
            const uint32_t halfHeight = (request.viewportHeight + 1u) / 2u;
            const uint32_t groupsX = std::max<uint32_t>((halfWidth + 7u) / 8u, 1u);
            const uint32_t groupsY = std::max<uint32_t>((halfHeight + 7u) / 8u, 1u);
            const std::string suffix = std::to_string(request.requestIndex);

            graph.addComputePass("Shadow Directional " + momentLabel + " Moments " + suffix)
                .readTexture(directionalShadow)
                .writeTexture(moment)
                .allowCulling(false)
                .setRenderFunc([this, blurProgram, moment, directionalShadow, slot, params, atlasSize,
                                groupsX, groupsY](RenderGraphContext& context) {
                    gfx::Encoder& encoder = context.encoder();
                    const bgfx::ViewId view = context.viewId();
                    encoder.setViewRect(view, 0, 0, atlasSize, atlasSize);
                    encoder.setViewFrameBuffer(view, {});
                    encoder.setUniform(m_shadowMomentSlotUniform, &slot.x);
                    encoder.setUniform(m_shadowMomentParamsUniform, &params.x);
                    encoder.setTexture(0, m_shadowMomentDepthSampler, context.texture(directionalShadow),
                                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                    encoder.setImage(1, context.texture(moment), 0, bgfx::Access::Write,
                                     gfx::TextureFormat::RGBA32F);
                    encoder.dispatch(view, blurProgram, groupsX, groupsY, 1);
                });
        }
        return;
    }
    if (!m_shadowManager.imsActive()) {
        return;
    }
    const gfx::ProgramHandle momentsProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::ShadowMoments, 0, 0});
    const gfx::ProgramHandle satHorizontalProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::ShadowSatHorizontal, 0, 0});
    const gfx::ProgramHandle satVerticalProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::ShadowSatVertical, 0, 0});
    const gfx::TextureHandle momentTexture = m_shadowManager.directionalMomentTexture();
    const gfx::TextureHandle satTexture = m_shadowManager.directionalMomentSatTexture();
    if (!momentsProgram.valid() || !satHorizontalProgram.valid() || !satVerticalProgram.valid() ||
        !momentTexture.valid() || !satTexture.valid()) {
        return;
    }

    const std::vector<ShadowRequest>& requests = m_shadowManager.requests();
    for (const ShadowRequest& request : requests) {
        if (!request.enabled || request.atlas != ShadowAtlasType::Directional ||
            request.filter != ShadowFilter::Ims) {
            continue;
        }
        const Vec4 slot(static_cast<float>(request.viewportWidth),
                        static_cast<float>(request.viewportHeight),
                        static_cast<float>(request.viewportX),
                        static_cast<float>(request.viewportY));
        const uint16_t atlasSize = request.atlasSize;
        const Vec4 momentParams(request.imsKernelSize, 0.0f, 0.0f, 0.0f);
        const uint32_t momentGroupsX = (request.viewportWidth + 7u) / 8u;
        const uint32_t momentGroupsY = (request.viewportHeight + 7u) / 8u;
        const uint32_t horizontalGroups = (request.viewportHeight + 63u) / 64u;
        const uint32_t verticalGroups = (request.viewportWidth + 63u) / 64u;
        const std::string suffix = std::to_string(request.requestIndex);

        graph.addComputePass("Shadow IMS Moments " + suffix)
            .readTexture(directionalShadow)
            .allowCulling(false)
            .setRenderFunc([this, momentsProgram, momentTexture, directionalShadow, slot, atlasSize,
                            momentGroupsX, momentGroupsY](RenderGraphContext& context) {
                gfx::Encoder& encoder = context.encoder();
                const bgfx::ViewId view = context.viewId();
                encoder.setViewRect(view, 0, 0, atlasSize, atlasSize);
                encoder.setViewFrameBuffer(view, {});
                encoder.setUniform(m_shadowMomentSlotUniform, &slot.x);
                encoder.setTexture(0, m_shadowMomentDepthSampler, context.texture(directionalShadow),
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                encoder.setImage(1, momentTexture, 0, bgfx::Access::Write, gfx::TextureFormat::RGBA32F);
                encoder.dispatch(view, momentsProgram, momentGroupsX, momentGroupsY, 1);
            });

        graph.addComputePass("Shadow IMS SAT Horizontal " + suffix)
            .allowCulling(false)
            .setRenderFunc([this, satHorizontalProgram, momentTexture, satTexture, slot, momentParams,
                            atlasSize, horizontalGroups](RenderGraphContext& context) {
                gfx::Encoder& encoder = context.encoder();
                const bgfx::ViewId view = context.viewId();
                encoder.setViewRect(view, 0, 0, atlasSize, atlasSize);
                encoder.setViewFrameBuffer(view, {});
                encoder.setUniform(m_shadowMomentSlotUniform, &slot.x);
                encoder.setUniform(m_shadowMomentParamsUniform, &momentParams.x);
                encoder.setTexture(0, m_shadowMomentSampler, momentTexture,
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                encoder.setImage(1, satTexture, 0, bgfx::Access::Write, gfx::TextureFormat::RGBA32U);
                encoder.dispatch(view, satHorizontalProgram, horizontalGroups, 1, 1);
            });

        graph.addComputePass("Shadow IMS SAT Vertical " + suffix)
            .allowCulling(false)
            .setRenderFunc([this, satVerticalProgram, satTexture, slot, atlasSize,
                            verticalGroups](RenderGraphContext& context) {
                gfx::Encoder& encoder = context.encoder();
                const bgfx::ViewId view = context.viewId();
                encoder.setViewRect(view, 0, 0, atlasSize, atlasSize);
                encoder.setViewFrameBuffer(view, {});
                encoder.setUniform(m_shadowMomentSlotUniform, &slot.x);
                encoder.setImage(0, satTexture, 0, bgfx::Access::ReadWrite, gfx::TextureFormat::RGBA32U);
                encoder.dispatch(view, satVerticalProgram, verticalGroups, 1, 1);
            });
    }
}

void ForwardRenderer::addAreaEvsmMomentPasses(RenderGraph& graph, RenderGraphTextureHandle areaShadow) {
    if (!m_shadowManager.areaEvsmBlurActive() || !areaShadow.valid()) {
        return;
    }
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::ShadowEvsmBlur, 0, 0});
    const gfx::ProgramHandle momentBlurProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::ShadowEvsmBlurMoments, 0, 0});
    const gfx::TextureHandle momentTexture = m_shadowManager.areaEvsmMomentTexture();
    const gfx::TextureHandle momentCopyTexture = m_shadowManager.areaEvsmMomentCopyTexture();
    if (!program.valid() || !momentTexture.valid() || !momentCopyTexture.valid()) {
        return;
    }

    const RenderGraphTextureHandle moment0 =
        graph.importTexture(momentTexture, "shadow.areaEvsmMoments");
    const RenderGraphTextureHandle moment1 =
        graph.importTexture(momentCopyTexture, "shadow.areaEvsmMomentsCopy");
    bool warnedMissingBlur = false;

    for (const ShadowRequest& request : m_shadowManager.requests()) {
        if (!request.enabled || request.atlas != ShadowAtlasType::Area ||
            request.filter != ShadowFilter::Evsm) {
            continue;
        }
        const Vec4 slot(static_cast<float>(request.viewportWidth),
                        static_cast<float>(request.viewportHeight),
                        static_cast<float>(request.viewportX),
                        static_cast<float>(request.viewportY));
        const Vec4 params(request.evsmExponent, request.evsmLightLeakBias, request.evsmVarianceBias, 0.0f);
        const uint16_t atlasSize = request.atlasSize;
        const uint32_t halfWidth = (request.viewportWidth + 1u) / 2u;
        const uint32_t halfHeight = (request.viewportHeight + 1u) / 2u;
        const uint32_t groupsX = std::max<uint32_t>((halfWidth + 7u) / 8u, 1u);
        const uint32_t groupsY = std::max<uint32_t>((halfHeight + 7u) / 8u, 1u);
        const std::string suffix = std::to_string(request.requestIndex);

        graph.addComputePass("Shadow Area EVSM " + suffix)
            .readTexture(areaShadow)
            .writeTexture(moment0)
            .allowCulling(false)
            .setRenderFunc([this, program, moment0, areaShadow, slot, params, atlasSize, groupsX,
                            groupsY](RenderGraphContext& context) {
                gfx::Encoder& encoder = context.encoder();
                const bgfx::ViewId view = context.viewId();
                encoder.setViewRect(view, 0, 0, atlasSize, atlasSize);
                encoder.setViewFrameBuffer(view, {});
                encoder.setUniform(m_shadowMomentSlotUniform, &slot.x);
                encoder.setUniform(m_shadowMomentParamsUniform, &params.x);
                encoder.setTexture(0, m_shadowMomentDepthSampler, context.texture(areaShadow),
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                encoder.setImage(1, context.texture(moment0), 0, bgfx::Access::Write,
                                 gfx::TextureFormat::RGBA32F);
                encoder.dispatch(view, program, groupsX, groupsY, 1);
            });

        const uint32_t blurPasses =
            std::min<uint32_t>(request.evsmBlurPasses, kEvsmMaxBlurPasses);
        if (blurPasses == 0) {
            continue;
        }
        if (!momentBlurProgram.valid()) {
            if (!warnedMissingBlur && !m_areaEvsmBlurWarningLogged) {
                HBG_LOGW("shadow area EVSM: blur-pass shader unavailable; ignoring evsmBlurPasses");
                m_areaEvsmBlurWarningLogged = true;
            }
            warnedMissingBlur = true;
            continue;
        }

        const Vec4 halfSlot(static_cast<float>(halfWidth), static_cast<float>(halfHeight),
                            static_cast<float>(request.viewportX / 2u),
                            static_cast<float>(request.viewportY / 2u));
        const auto addMomentBlurPass = [&](const std::string& name, RenderGraphTextureHandle source,
                                           RenderGraphTextureHandle target, bool copy) {
            const Vec4 mode(copy ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
            graph.addComputePass(name)
                .readTexture(source)
                .writeTexture(target)
                .allowCulling(false)
                .setRenderFunc([this, momentBlurProgram, source, target, halfSlot, mode, atlasSize,
                                groupsX, groupsY](RenderGraphContext& context) {
                    gfx::Encoder& encoder = context.encoder();
                    const bgfx::ViewId view = context.viewId();
                    encoder.setViewRect(view, 0, 0, atlasSize, atlasSize);
                    encoder.setViewFrameBuffer(view, {});
                    encoder.setUniform(m_shadowMomentSlotUniform, &halfSlot.x);
                    encoder.setUniform(m_shadowMomentParamsUniform, &mode.x);
                    encoder.setTexture(0, m_shadowMomentSampler, context.texture(source),
                                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                    encoder.setImage(1, context.texture(target), 0, bgfx::Access::Write,
                                     gfx::TextureFormat::RGBA32F);
                    encoder.dispatch(view, momentBlurProgram, groupsX, groupsY, 1);
                });
        };

        uint32_t surface = 0;
        for (uint32_t pass = 0; pass < blurPasses; ++pass) {
            const RenderGraphTextureHandle source = surface == 0 ? moment0 : moment1;
            const RenderGraphTextureHandle target = surface == 0 ? moment1 : moment0;
            addMomentBlurPass("Shadow Area EVSM Blur " + suffix + "." + std::to_string(pass), source,
                              target, false);
            surface ^= 1u;
        }
        if (surface != 0) {
            addMomentBlurPass("Shadow Area EVSM Copy " + suffix, moment1, moment0, true);
        }
    }
}

gfx::TextureHandle ForwardRenderer::directionalShadowSamplingTexture(gfx::TextureHandle fallback) const {
    if (m_shadowManager.directionalMomentBlurActive()) {
        const gfx::TextureHandle moment = m_shadowManager.directionalMomentBlurTexture();
        if (moment.valid()) {
            return moment;
        }
    }
    if (m_shadowManager.directionalMsmBlurActive()) {
        const gfx::TextureHandle moment = m_shadowManager.directionalMsmMomentTexture();
        if (moment.valid()) {
            return moment;
        }
    }
    return fallback;
}

uint32_t ForwardRenderer::directionalShadowSamplingFlags() const {
    return (m_shadowManager.directionalMomentBlurActive() ||
            m_shadowManager.directionalMsmBlurActive())
               ? BGFX_SAMPLER_UVW_CLAMP
               : (BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
}

void ForwardRenderer::bindShadowMomentSat(gfx::Encoder& encoder, gfx::TextureHandle directionalShadow) const {
    if (!m_shadowPointSampler.valid()) {
        return;
    }
    const gfx::TextureHandle sat = m_shadowManager.directionalMomentSatTexture().valid()
                                       ? m_shadowManager.directionalMomentSatTexture()
                                       : m_shadowMomentSatFallback;
    const gfx::TextureHandle texture =
        m_shadowManager.imsActive() && sat.valid() ? sat : directionalShadow;
    if (texture.valid()) {
        encoder.setTexture(12, m_shadowPointSampler, texture,
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    }
}

void ForwardRenderer::addGpuLightListPasses(RenderGraph& graph, const FramePassResources& resources) {
    if (!m_gpuLightListActive) {
        return;
    }

    const gfx::ProgramHandle aabbProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightScreenAABB, 0, 0});
    const gfx::ProgramHandle bigTileProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightBigTile, 0, 0});
    const gfx::ProgramHandle fptlProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightFptl, 0, 0});
    const gfx::ProgramHandle clusterProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightCluster, 0, 0});
    const uint16_t bigTileCountX = LightListBuilder::tileCount(m_viewState.renderWidth(), kLightBigTileSize);
    const uint16_t bigTileCountY = LightListBuilder::tileCount(m_viewState.renderHeight(), kLightBigTileSize);
    const uint16_t tileCountX = LightListBuilder::tileCount(m_viewState.renderWidth(), kLightTileSize);
    const uint16_t tileCountY = LightListBuilder::tileCount(m_viewState.renderHeight(), kLightTileSize);
    const uint16_t clusterTileCountX = LightListBuilder::tileCount(m_viewState.renderWidth(), kClusterTileSize);
    const uint16_t clusterTileCountY = LightListBuilder::tileCount(m_viewState.renderHeight(), kClusterTileSize);
    const bool clusterPass = m_lightClusterEnabled;
    if (m_gpuLightListBufferActive) {
        const gfx::ProgramHandle aabbBufferProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightScreenAABBBuffer, 0, 0});
        const gfx::ProgramHandle bigTileBufferProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightBigTileBuffer, 0, 0});
        const gfx::ProgramHandle fptlBufferProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightFptlBuffer, 0, 0});
        const gfx::ProgramHandle clusterBufferProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightClusterBuffer, 0, 0});
        const gfx::ProgramHandle indirectArgsProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LightIndirectArgs, 0, 0});
        const bool clusterBufferPass = clusterPass;
        if (!aabbBufferProgram.valid() || !bigTileBufferProgram.valid() || !fptlBufferProgram.valid() ||
            !resources.lightData.valid() || !resources.lightScreenBoundsBuffer.valid() ||
            !resources.lightBigTileBuffer.valid() || !resources.lightFptlBuffer.valid() ||
            (clusterBufferPass &&
             (!clusterBufferProgram.valid() || !resources.lightClusterBuffer.valid()))) {
            m_gpuLightListActive = false;
            m_gpuLightListBufferActive = false;
            m_gpuLightListPasses = 0;
            updateCpuLightList();
            if (!m_gpuLightListWarningLogged) {
                HBG_LOGW("gpu light list: buffer passes unavailable; falling back to the texture/CPU list");
                m_gpuLightListWarningLogged = true;
            }
            return;
        }

        graph.addComputePass("GPU Light Screen AABB Buffer")
            .readTexture(resources.lightData)
            .writeBuffer(resources.lightScreenBoundsBuffer)
            .allowCulling(false)
            .setRenderFunc([this, resources, aabbBufferProgram](RenderGraphContext& context) {
                gfx::Encoder& encoder = context.encoder();
                encoder.setViewRect(context.viewId(), 0, 0, context.width(), context.height());
                encoder.setViewFrameBuffer(context.viewId(), {});
                m_globalBinder.upload(m_globalUniforms);
                m_passBinder.upload(m_passUniforms);
                m_clusterBinder.upload(m_clusterUniforms);
                encoder.setTexture(5, m_lightDataSampler, context.texture(resources.lightData),
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                encoder.setTexture(6, m_lightViewRowsSampler, m_lightViewRowsTexture,
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                encoder.setStorageBuffer(1, context.buffer(resources.lightScreenBoundsBuffer), bgfx::Access::Write);
                encoder.dispatch(context.viewId(), aabbBufferProgram, 1, 1, 1);
            });

        graph.addComputePass("GPU Big Tile Light List Buffer")
            .readBuffer(resources.lightScreenBoundsBuffer)
            .readTexture(resources.lightData)
            .writeBuffer(resources.lightBigTileBuffer)
            .allowCulling(false)
            .setRenderFunc([this, resources, bigTileBufferProgram, bigTileCountX,
                            bigTileCountY](RenderGraphContext& context) {
                gfx::Encoder& encoder = context.encoder();
                encoder.setViewRect(context.viewId(), 0, 0, context.width(), context.height());
                encoder.setViewFrameBuffer(context.viewId(), {});
                m_globalBinder.upload(m_globalUniforms);
                m_passBinder.upload(m_passUniforms);
                m_clusterBinder.upload(m_clusterUniforms);
                encoder.setStorageBuffer(1, context.buffer(resources.lightScreenBoundsBuffer), bgfx::Access::Read);
                encoder.setStorageBuffer(2, context.buffer(resources.lightBigTileBuffer), bgfx::Access::Write);
                encoder.setTexture(5, m_lightDataSampler, context.texture(resources.lightData),
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                encoder.dispatch(context.viewId(), bigTileBufferProgram, bigTileCountX, bigTileCountY, 1);
            });

        if (m_gpuLightListIndirectActive && resources.lightIndirectArgs.valid() && indirectArgsProgram.valid()) {
            graph.addComputePass("GPU Light List Indirect Args")
                .writeBuffer(resources.lightIndirectArgs)
                .allowCulling(false)
                .setRenderFunc([this, resources, indirectArgsProgram](RenderGraphContext& context) {
                    gfx::Encoder& encoder = context.encoder();
                    encoder.setViewRect(context.viewId(), 0, 0, context.width(), context.height());
                    encoder.setViewFrameBuffer(context.viewId(), {});
                    m_clusterBinder.upload(m_clusterUniforms);
                    encoder.setStorageBuffer(1, context.buffer(resources.lightIndirectArgs), bgfx::Access::Write);
                    encoder.dispatch(context.viewId(), indirectArgsProgram, 1, 1, 1);
                });
        }

        graph.addComputePass("GPU FPTL Light List Buffer")
            .readBuffer(resources.lightScreenBoundsBuffer)
            .readBuffer(resources.lightBigTileBuffer)
            .readTexture(resources.lightData)
            .writeBuffer(resources.lightFptlBuffer)
            .allowCulling(false)
            .setRenderFunc([this, resources, fptlBufferProgram, tileCountX, tileCountY,
                            indirect = m_gpuLightListIndirectActive &&
                                       resources.lightIndirectArgs.valid()](RenderGraphContext& context) {
                gfx::Encoder& encoder = context.encoder();
                encoder.setViewRect(context.viewId(), 0, 0, context.width(), context.height());
                encoder.setViewFrameBuffer(context.viewId(), {});
                m_globalBinder.upload(m_globalUniforms);
                m_passBinder.upload(m_passUniforms);
                m_clusterBinder.upload(m_clusterUniforms);
                encoder.setStorageBuffer(1, context.buffer(resources.lightScreenBoundsBuffer), bgfx::Access::Read);
                encoder.setStorageBuffer(2, context.buffer(resources.lightBigTileBuffer), bgfx::Access::Read);
                encoder.setStorageBuffer(3, context.buffer(resources.lightFptlBuffer), bgfx::Access::Write);
                encoder.setTexture(5, m_lightDataSampler, context.texture(resources.lightData),
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                if (indirect) {
                    encoder.dispatchIndirect(context.viewId(), fptlBufferProgram,
                                             context.buffer(resources.lightIndirectArgs), 0, 1);
                } else {
                    encoder.dispatch(context.viewId(), fptlBufferProgram, tileCountX, tileCountY, 1);
                }
            });

        if (clusterBufferPass) {
            graph.addComputePass("GPU Light Cluster Buffer")
                .readBuffer(resources.lightScreenBoundsBuffer)
                .readBuffer(resources.lightBigTileBuffer)
                .readTexture(resources.lightData)
                .writeBuffer(resources.lightClusterBuffer)
                .allowCulling(false)
                .setRenderFunc([this, resources, clusterBufferProgram, clusterTileCountX,
                                clusterTileCountY](RenderGraphContext& context) {
                    gfx::Encoder& encoder = context.encoder();
                    encoder.setViewRect(context.viewId(), 0, 0, context.width(), context.height());
                    encoder.setViewFrameBuffer(context.viewId(), {});
                    m_globalBinder.upload(m_globalUniforms);
                    m_passBinder.upload(m_passUniforms);
                    m_clusterBinder.upload(m_clusterUniforms);
                    encoder.setStorageBuffer(1, context.buffer(resources.lightScreenBoundsBuffer),
                                             bgfx::Access::Read);
                    encoder.setStorageBuffer(2, context.buffer(resources.lightBigTileBuffer), bgfx::Access::Read);
                    encoder.setTexture(5, m_lightDataSampler, context.texture(resources.lightData),
                                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                    encoder.setTexture(6, m_lightViewRowsSampler, m_lightViewRowsTexture,
                                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                    encoder.setStorageBuffer(3, context.buffer(resources.lightClusterBuffer),
                                             bgfx::Access::Write);
                    encoder.dispatch(context.viewId(), clusterBufferProgram, clusterTileCountX, clusterTileCountY,
                                     kClusterSliceCount);
                });
        }

        m_gpuLightListPasses = (clusterBufferPass ? 4 : 3) + (m_gpuLightListIndirectActive ? 1 : 0);        return;
    }

    if (!gpuLightListPathAvailable() || !aabbProgram.valid() || !bigTileProgram.valid() || !fptlProgram.valid() ||
        !resources.lightData.valid() || !resources.lightScreenBounds.valid() || !resources.lightBigTile.valid() ||
        !resources.lightList.valid() || bigTileCountX == 0 || bigTileCountY == 0 || tileCountX == 0 ||
        tileCountY == 0 ||
        (clusterPass && (!clusterProgram.valid() || !resources.clusterLightList.valid() ||
                         clusterTileCountX == 0 || clusterTileCountY == 0))) {
        m_gpuLightListActive = false;
        m_gpuLightListPasses = 0;
        updateCpuLightList();
        if (!m_gpuLightListWarningLogged) {
            HBG_LOGW("gpu light list: GPU passes unavailable; falling back to the CPU list");
            m_gpuLightListWarningLogged = true;
        }
        return;
    }

    graph.addComputePass("GPU Light Screen AABB")
        .readTexture(resources.lightData)
        .writeTexture(resources.lightScreenBounds)
        .allowCulling(false)
        .setRenderFunc([this, resources, aabbProgram](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setViewRect(context.viewId(), 0, 0, context.width(), context.height());
            encoder.setViewFrameBuffer(context.viewId(), {});

            m_globalBinder.upload(m_globalUniforms);
            m_passBinder.upload(m_passUniforms);
            m_clusterBinder.upload(m_clusterUniforms);
            encoder.setTexture(5, m_lightDataSampler, context.texture(resources.lightData),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setTexture(6, m_lightViewRowsSampler, m_lightViewRowsTexture,
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setImage(1, context.texture(resources.lightScreenBounds), 0, bgfx::Access::Write,
                             gfx::TextureFormat::RGBA32F);
            encoder.dispatch(context.viewId(), aabbProgram, 1, 1, 1);
        });

    graph.addComputePass("GPU Big Tile Light List")
        .readTexture(resources.lightScreenBounds)
        .readTexture(resources.lightData)
        .writeTexture(resources.lightBigTile)
        .allowCulling(false)
        .setRenderFunc([this, resources, bigTileProgram, bigTileCountX,
                        bigTileCountY](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setViewRect(context.viewId(), 0, 0, context.width(), context.height());
            encoder.setViewFrameBuffer(context.viewId(), {});

            m_globalBinder.upload(m_globalUniforms);
            m_passBinder.upload(m_passUniforms);
            m_clusterBinder.upload(m_clusterUniforms);
            encoder.setTexture(1, m_lightScreenBoundsSampler, context.texture(resources.lightScreenBounds),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setTexture(5, m_lightDataSampler, context.texture(resources.lightData),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setImage(2, context.texture(resources.lightBigTile), 0, bgfx::Access::Write,
                             gfx::TextureFormat::R32U);
            encoder.dispatch(context.viewId(), bigTileProgram, bigTileCountX, bigTileCountY, 1);
        });

    graph.addComputePass("GPU FPTL Light List")
        .readTexture(resources.lightScreenBounds)
        .readTexture(resources.lightBigTile)
        .readTexture(resources.lightData)
        .writeTexture(resources.lightList)
        .allowCulling(false)
        .setRenderFunc([this, resources, fptlProgram, tileCountX, tileCountY](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setViewRect(context.viewId(), 0, 0, context.width(), context.height());
            encoder.setViewFrameBuffer(context.viewId(), {});

            m_globalBinder.upload(m_globalUniforms);
            m_passBinder.upload(m_passUniforms);
            m_clusterBinder.upload(m_clusterUniforms);
            encoder.setTexture(1, m_lightScreenBoundsSampler, context.texture(resources.lightScreenBounds),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setTexture(2, m_lightBigTileSampler, context.texture(resources.lightBigTile),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setTexture(5, m_lightDataSampler, context.texture(resources.lightData),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setImage(3, context.texture(resources.lightList), 0, bgfx::Access::Write,
                             gfx::TextureFormat::R32U);
            encoder.dispatch(context.viewId(), fptlProgram, tileCountX, tileCountY, 1);
        });

    if (clusterPass) {
        graph.addComputePass("GPU Light Cluster")
            .readTexture(resources.lightScreenBounds)
            .readTexture(resources.lightBigTile)
            .readTexture(resources.lightData)
            .writeTexture(resources.clusterLightList)
            .allowCulling(false)
            .setRenderFunc([this, resources, clusterProgram, clusterTileCountX,
                            clusterTileCountY](RenderGraphContext& context) {
                gfx::Encoder& encoder = context.encoder();
                encoder.setViewRect(context.viewId(), 0, 0, context.width(), context.height());
                encoder.setViewFrameBuffer(context.viewId(), {});

                m_globalBinder.upload(m_globalUniforms);
                m_passBinder.upload(m_passUniforms);
                m_clusterBinder.upload(m_clusterUniforms);
                encoder.setTexture(1, m_lightScreenBoundsSampler, context.texture(resources.lightScreenBounds),
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                encoder.setTexture(2, m_lightBigTileSampler, context.texture(resources.lightBigTile),
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                encoder.setTexture(5, m_lightDataSampler, context.texture(resources.lightData),
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                encoder.setTexture(6, m_lightViewRowsSampler, m_lightViewRowsTexture,
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                encoder.setImage(3, context.texture(resources.clusterLightList), 0, bgfx::Access::Write,
                                 gfx::TextureFormat::R32U);
                encoder.dispatch(context.viewId(), clusterProgram, clusterTileCountX, clusterTileCountY,
                                 kClusterSliceCount);
            });
    }

    m_gpuLightListPasses = clusterPass ? 4 : 3;
}

RenderGraphTextureHandle ForwardRenderer::addContactShadowPass(RenderGraph& graph,
                                                                       const FramePassResources& resources,
                                                                       bool deferred) {
    const bool lightListAvailable =
        m_gpuLightListBufferActive ? resources.lightFptlBuffer.valid()
                                   : (resources.lightList.valid() && m_lightListTexture.valid());
    if (!contactShadowsActive() || !m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects) ||
        !resources.depth.valid() || !resources.lightData.valid() || !lightListAvailable) {
        return {};
    }

    const gfx::ProgramHandle program = lightListProgram(shader::PassId::ContactShadows);
    if (!program.valid()) {
        if (!m_contactShadowLogged) {
            HBG_LOGW("contact shadows: compute program is invalid; skipping");
            m_contactShadowLogged = true;
        }
        return deferred ? resources.gbuffer[1] : RenderGraphTextureHandle{};
    }

    const bool halfResolution = m_shadowSettings.contactShadows.halfResolution;
    const std::array<uint32_t, 2> resolution =
        contactShadowResolution(m_viewState.renderWidth(), m_viewState.renderHeight(), halfResolution);
    m_contactShadowWidth = static_cast<uint16_t>(resolution[0]);
    m_contactShadowHeight = static_cast<uint16_t>(resolution[1]);

    RenderGraphTextureHandle output;
    if (deferred) {
        output = resources.gbuffer[1];
        if (!output.valid()) {
            return {};
        }
    } else {
        gfx::TextureDesc desc;
        desc.width = static_cast<uint16_t>(resolution[0]);
        desc.height = static_cast<uint16_t>(resolution[1]);
        desc.format = gfx::TextureFormat::RGBA8;
        desc.computeWrite = true;
        desc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        desc.name = "contactShadows";
        output = graph.createTexture(desc, "Contact Shadows");
        if (!output.valid()) {
            return {};
        }
    }

    RenderGraphBuilder pass = graph.addComputePass("Contact Shadows");
    pass.readTexture(resources.depth);
    pass.readTexture(resources.lightData);
    if (m_gpuLightListBufferActive && resources.lightFptlBuffer.valid()) {
        pass.readBuffer(resources.lightFptlBuffer);
    } else if (resources.lightList.valid()) {
        pass.readTexture(resources.lightList);
    }
    if (deferred) {
        pass.readTexture(output);
    }
    pass.writeTexture(output);
    pass.setViewSize(static_cast<uint16_t>(resolution[0]), static_cast<uint16_t>(resolution[1]));
    pass.setRenderFunc([this, output, resources, deferred, halfResolution, resolution](RenderGraphContext& context) {
        renderContactShadowPass(context.encoder(), context.viewId(), context.texture(output),
                                context.texture(resources.depth), context.texture(resources.lightData),
                                context.texture(resources.lightList), deferred, halfResolution,
                                static_cast<uint16_t>(resolution[0]), static_cast<uint16_t>(resolution[1]));
    });
    if (!m_contactShadowLogged && m_frameIndex == 0) {
        HBG_LOGI("contact shadows: {}x{} {}", resolution[0], resolution[1],
                 halfResolution ? "half-resolution" : "full-resolution");
        m_contactShadowLogged = true;
    }
    return output;
}

void ForwardRenderer::addShadowAtlasClearPass(RenderGraph& graph, RenderGraphTextureHandle atlas, uint16_t size,
                                              bool clear) {
    if (!atlas.valid() || size == 0) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("Shadow Atlas Clear");
    pass.setDepthAttachment(atlas);
    pass.allowCulling(false);
    pass.setRenderFunc([size, clear](RenderGraphContext& context) {
        gfx::Encoder& encoder = context.encoder();
        const bgfx::ViewId view = context.viewId();
        encoder.setViewRect(view, 0, 0, size, size);
        encoder.setViewFrameBuffer(view, context.frameBuffer());
        encoder.setViewClear(view, clear ? BGFX_CLEAR_DEPTH : BGFX_CLEAR_NONE, 0x000000ff, 1.0f, 0);
        encoder.touch(view);
    });
}

void ForwardRenderer::addClearPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                                   bool debugBlack) {
    RenderGraphBuilder pass = graph.addRasterPass("Scene Clear");
    pass.setColorAttachment(color);
    pass.setDepthAttachment(depth);
    pass.setRenderFunc([debugBlack](RenderGraphContext& context) {
        gfx::Encoder& encoder = context.encoder();
        const bgfx::ViewId view = context.viewId();
        encoder.setViewRect(view, 0, 0, context.width(), context.height());
        encoder.setViewFrameBuffer(view, context.frameBuffer());
        encoder.setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, debugBlack ? 0x000000ff : 0x101018ff, 1.0f,
                             0);
        encoder.touch(view);
    });
}

void ForwardRenderer::addPrepassPass(RenderGraph& graph, RenderGraphTextureHandle normal,
                                     RenderGraphTextureHandle depth, const RenderScene& scene, bool depthOnly) {
    if (!m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects) || m_prepassDrawList.empty()) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("Forward Depth Prepass");
    if (!depthOnly && normal.valid()) {
        pass.setColorAttachment(normal);
    }
    pass.readTexture(depth);
    pass.setDepthAttachment(depth);
    pass.allowCulling(false);
    pass.setRenderFunc([this, &scene, depthOnly](RenderGraphContext& context) {
        renderPrepass(context.encoder(), context.viewId(), context.frameBuffer(), scene, depthOnly);
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
    if (!motionVectorsActive()) {
        return;
    }
    const RenderGraphTextureHandle depthSource = depthPyramid.valid() ? depthPyramid : depth;
    if (!depthSource.valid()) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("Camera Motion Vectors");
    pass.setColorAttachment(motionVector);
    pass.setDepthAttachment(depth);
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

RenderGraphTextureHandle ForwardRenderer::addVolumetricFogPass(RenderGraph& graph,
                                                                const FramePassResources& resources,
                                                                RenderGraphTextureHandle color,
                                                                const RenderScene& scene) {
    if (!m_volumetricFogActive || !color.valid() || !resources.depth.valid() ||
        !resources.fullscreen.valid() || !m_fogSourceSampler.valid() || !m_fogHistorySampler.valid() ||
        !m_fogLowResSampler.valid()) {
        return {};
    }

    const gfx::ProgramHandle compositeProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::VolumetricFogComposite, 0, 0});
    if (!compositeProgram.valid()) {
        if (!m_volumetricFogLogged) {
            HBG_LOGW("volumetric fog: shader program is unavailable");
            m_volumetricFogLogged = true;
        }
        return {};
    }

    const uint16_t renderWidth = m_viewState.renderWidth();
    const uint16_t renderHeight = m_viewState.renderHeight();
    if (renderWidth == 0 || renderHeight == 0) {
        return {};
    }

    const float resolutionScale = std::clamp(m_volumetricFogSettings.resolutionScale, 0.25f, 1.0f);
    const uint16_t fogWidth = static_cast<uint16_t>(
        std::max(1, static_cast<int>(std::lround(static_cast<float>(renderWidth) * resolutionScale))));
    const uint16_t fogHeight = static_cast<uint16_t>(
        std::max(1, static_cast<int>(std::lround(static_cast<float>(renderHeight) * resolutionScale))));
    if (!ensureVolumetricFogHistory(fogWidth, fogHeight)) {
        if (!m_volumetricFogLogged) {
            HBG_LOGW("volumetric fog: history resources are unavailable");
            m_volumetricFogLogged = true;
        }
        return {};
    }

    if (!m_viewState.historyValid()) {
        m_fogHistoryValid = false;
    }

    const GpuLightData& lightData = m_lightBuilder.data();
    const bool punctualLights =
        m_volumetricFogSettings.punctualLights && m_lightDataTexture.valid() && lightData.punctualCount > 0;
    const bool useCluster = punctualLights && m_lightClusterEnabled && m_clusterUniforms.depthParams.w > 0.5f;
    const bool fogTexturePath = !m_gpuLightListBufferActive;
    const gfx::ProgramHandle raymarchProgram =
        lightListProgram(shader::PassId::VolumetricFog, 0);
    if (!raymarchProgram.valid()) {
        if (!m_volumetricFogLogged) {
            HBG_LOGW("volumetric fog: light-list shader variant is unavailable");
            m_volumetricFogLogged = true;
        }
        return {};
    }
    const RenderGraphTextureHandle punctualLightList =
        useCluster ? resources.clusterLightList : resources.lightList;
    const RenderGraphBufferHandle punctualLightBuffer =
        useCluster ? resources.lightClusterBuffer : resources.lightFptlBuffer;
    const bool historyValid = m_fogHistoryValid && m_viewState.historyValid();
    const bool temporalActive = m_volumetricFogSettings.temporal && historyValid && resources.motionVector.valid();
    m_volumetricFogWidth = fogWidth;
    m_volumetricFogHeight = fogHeight;
    m_volumetricFogTemporalActive = temporalActive;
    m_volumetricFogPunctualLights =
        punctualLights ? std::min(lightData.punctualCount, m_volumetricFogSettings.maxPunctualLights) : 0;
    updateVolumetricFogUniforms(true, temporalActive, temporalActive, punctualLights, fogWidth, fogHeight);
    uint32_t localVolumeCount = 0;
    updateLocalFogUniforms(scene, localVolumeCount);
    m_volumetricFogLocalVolumes = localVolumeCount;
    m_volumetricFogLocalMasks = localVolumeCount > 0 ? m_localFogMaskCount : 0;
    m_fogUniforms.params3.w =
        (m_volumetricFogSettings.punctualShadows && resources.punctualShadow.valid() &&
         resources.shadowContext.valid())
            ? 1.0f
            : 0.0f;
    m_fogUniforms.params5.z = useCluster ? 1.0f : 0.0f;

    const uint32_t previousIndex = m_fogHistoryIndex;
    const uint32_t nextIndex = previousIndex ^ 1u;
    const RenderGraphTextureHandle previous =
        graph.importTexture(m_fogHistoryTextures[previousIndex], "Volumetric Fog History Previous");
    const RenderGraphTextureHandle next =
        graph.importTexture(m_fogHistoryTextures[nextIndex], "Volumetric Fog History");
    const bool readHistory = temporalActive && m_fogHistoryTextures[previousIndex].valid();

    RenderGraphBuilder raymarch = graph.addRasterPass("Volumetric Fog Raymarch");
    raymarch.setColorAttachment(next);
    raymarch.readTexture(resources.depth);
    if (readHistory) {
        raymarch.readTexture(previous);
    }
    if (temporalActive) {
        raymarch.readTexture(resources.motionVector);
    }
    if (punctualLights) {
        raymarch.readTexture(resources.lightData);
        if (!fogTexturePath && punctualLightBuffer.valid()) {
            raymarch.readBuffer(punctualLightBuffer);
        } else if (punctualLightList.valid()) {
            raymarch.readTexture(punctualLightList);
        }
    }
    if (resources.directionalShadow.valid()) {
        raymarch.readTexture(resources.directionalShadow);
    }
    if (resources.punctualShadow.valid()) {
        raymarch.readTexture(resources.punctualShadow);
    }
    if (resources.shadowContext.valid()) {
        raymarch.readBuffer(resources.shadowContext);
    }
    raymarch.readBuffer(resources.fullscreen);
    raymarch.setViewSize(fogWidth, fogHeight);
    raymarch.allowCulling(false);
    raymarch.setRenderFunc([this, resources, previous, next, readHistory, temporalActive, punctualLights,
                            useCluster, fogTexturePath, localVolumeCount, fogWidth, fogHeight,
                            raymarchProgram](RenderGraphContext& context) {
        gfx::Encoder& encoder = context.encoder();
        const bgfx::ViewId view = context.viewId();
        encoder.setViewRect(view, 0, 0, fogWidth, fogHeight);
        encoder.setViewFrameBuffer(view, context.frameBuffer());
        encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
        encoder.touch(view);
        encoder.setState(gfx::state::kFullscreen);
        m_globalBinder.upload(m_globalUniforms);
        m_passBinder.upload(m_passUniforms);
        m_clusterBinder.upload(m_clusterUniforms);
        m_environmentBinder.upload(m_environmentUniforms);
        m_fogBinder.upload(m_fogUniforms);
        encoder.setTexture(2, m_depthSourceSampler, context.texture(resources.depth),
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        if (temporalActive && resources.motionVector.valid() && m_motionVectorSampler.valid()) {
            encoder.setTexture(3, m_motionVectorSampler, context.texture(resources.motionVector),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        }
        if (readHistory && m_fogHistorySampler.valid()) {
            encoder.setTexture(4, m_fogHistorySampler, context.texture(previous), BGFX_SAMPLER_UVW_CLAMP);
        }
        if (punctualLights && resources.lightData.valid() && m_lightDataSampler.valid()) {
            encoder.setTexture(5, m_lightDataSampler, context.texture(resources.lightData),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        }
        if (resources.directionalShadow.valid() && m_shadowSampler.valid()) {
            encoder.setTexture(6, m_shadowSampler, context.texture(resources.directionalShadow),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        }
        if (punctualLights) {
            if (fogTexturePath) {
                bindLightListTexture(encoder, useCluster);
            } else {
                bindLightList(encoder, useCluster);
            }
        }
        if (resources.shadowContext.valid()) {
            encoder.setStorageBuffer(8, context.buffer(resources.shadowContext), bgfx::Access::Read);
        }
        if (resources.punctualShadow.valid() && m_shadowPunctualSampler.valid()) {
            encoder.setTexture(9, m_shadowPunctualSampler, context.texture(resources.punctualShadow),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        }
        if (localVolumeCount > 0) {
            for (std::size_t index = 0; index < m_fogMaskSamplers.size(); ++index) {
                if (!m_fogMaskSamplers[index].valid()) {
                    continue;
                }
                const bool used = index < m_localFogMaskCount && m_fogMaskTextures[index].valid();
                const gfx::TextureHandle texture = used ? m_fogMaskTextures[index] : m_fogMaskFallback;
                if (texture.valid()) {
                    encoder.setTexture(static_cast<uint8_t>(10 + index), m_fogMaskSamplers[index], texture,
                                       BGFX_SAMPLER_UVW_CLAMP);
                }
            }
        }
        encoder.setVertexBuffer(0, context.buffer(resources.fullscreen));
        encoder.submit(view, raymarchProgram);
    });

    gfx::TextureDesc desc;
    desc.width = renderWidth;
    desc.height = renderHeight;
    desc.format = selectHdrFormat();
    desc.renderTarget = true;
    desc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    desc.name = "atmosphere.volumetricFog.composite";
    const RenderGraphTextureHandle output = graph.createTexture(desc, "Volumetric Fog Composite");

    RenderGraphBuilder composite = graph.addRasterPass("Volumetric Fog Composite");
    composite.setColorAttachment(output);
    composite.readTexture(color);
    composite.readTexture(next);
    composite.readTexture(resources.depth);
    composite.readBuffer(resources.fullscreen);
    composite.setViewSize(renderWidth, renderHeight);
    composite.allowCulling(false);
    composite.setRenderFunc([this, color, next, resources, renderWidth, renderHeight, compositeProgram](
                                RenderGraphContext& context) {
        gfx::Encoder& encoder = context.encoder();
        const bgfx::ViewId view = context.viewId();
        encoder.setViewRect(view, 0, 0, renderWidth, renderHeight);
        encoder.setViewFrameBuffer(view, context.frameBuffer());
        encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
        encoder.touch(view);
        encoder.setState(gfx::state::kFullscreen);
        m_globalBinder.upload(m_globalUniforms);
        m_fogBinder.upload(m_fogUniforms);
        encoder.setTexture(0, m_fogSourceSampler, context.texture(color), BGFX_SAMPLER_UVW_CLAMP);
        encoder.setTexture(1, m_fogLowResSampler, context.texture(next), BGFX_SAMPLER_UVW_CLAMP);
        encoder.setTexture(2, m_depthSourceSampler, context.texture(resources.depth),
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        encoder.setVertexBuffer(0, context.buffer(resources.fullscreen));
        encoder.submit(view, compositeProgram);
    });

    m_fogHistoryIndex = nextIndex;
    m_fogHistoryValid = true;
    ++m_fogHistoryFrame;
    return output;
}

void ForwardRenderer::addGBufferClearPass(RenderGraph& graph, const std::array<RenderGraphTextureHandle, 5>& gbuffer,
                                          RenderGraphTextureHandle depth,
                                          RenderGraphBufferHandle fullscreen) {
    if (!std::all_of(gbuffer.begin(), gbuffer.end(),
                     [](RenderGraphTextureHandle texture) { return texture.valid(); }) ||
        !depth.valid() || !fullscreen.valid()) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("GBuffer Clear");
    for (std::size_t index = 0; index < gbuffer.size(); ++index) {
        pass.setColorAttachment(gbuffer[index], static_cast<uint8_t>(index));
    }
    pass.readTexture(depth);
    pass.setDepthAttachment(depth);
    pass.readBuffer(fullscreen);
    pass.allowCulling(false);
    const gfx::ProgramHandle clearProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::GBufferClear, 0, 0});
    const bool useShader = clearProgram.valid();
    pass.setRenderFunc([this, useShader, fullscreen, clearProgram](RenderGraphContext& context) {
        renderGBufferClear(context.encoder(), context.viewId(), context.frameBuffer(), useShader);
        if (useShader) {
            context.encoder().setVertexBuffer(0, context.buffer(fullscreen));
            context.encoder().submit(context.viewId(), clearProgram);
        }
    });
}

void ForwardRenderer::addDecalPasses(RenderGraph& graph, const FramePassResources& resources,
                                     const RenderScene& scene) {
    if (!std::all_of(resources.decal.begin(), resources.decal.end(),
                     [](RenderGraphTextureHandle texture) { return texture.valid(); }) ||
        !resources.depth.valid() || !resources.depthPyramid.valid() || !resources.fullscreen.valid() ||
        scene.decals.empty()) {
        return;
    }

    RenderGraphBuilder clear = graph.addRasterPass("DBuffer Clear");
    for (std::size_t index = 0; index < resources.decal.size(); ++index) {
        clear.setColorAttachment(resources.decal[index], static_cast<uint8_t>(index));
    }
    clear.readBuffer(resources.fullscreen);
    clear.allowCulling(false);
    clear.setRenderFunc([this](RenderGraphContext& context) {
        renderDecalClear(context.encoder(), context.viewId(), context.frameBuffer());
    });

    for (std::size_t index = 0; index < scene.decals.size(); ++index) {
        if (!scene.decals[index].enabled) {
            continue;
        }
        RenderGraphBuilder pass = graph.addRasterPass("Decal " + scene.decals[index].name);
        for (std::size_t target = 0; target < resources.decal.size(); ++target) {
            pass.setColorAttachment(resources.decal[target], static_cast<uint8_t>(target));
        }
        pass.readTexture(resources.depthPyramid);
        pass.allowCulling(false);
        pass.setRenderFunc([this, &scene, index, resources](RenderGraphContext& context) {
            renderDecalPass(context.encoder(), context.viewId(), context.frameBuffer(), scene, index,
                            context.texture(resources.depthPyramid));
        });
    }
}

void ForwardRenderer::addGBufferPass(RenderGraph& graph, const FramePassResources& resources,
                                     const RenderScene& scene) {
    if (!m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects) || m_gbufferDrawList.empty() ||
        !resources.depth.valid()) {
        return;
    }
    if (!std::all_of(resources.gbuffer.begin(), resources.gbuffer.end(),
                     [](RenderGraphTextureHandle texture) { return texture.valid(); })) {
        return;
    }

    const bool decals = std::all_of(resources.decal.begin(), resources.decal.end(),
                                    [](RenderGraphTextureHandle texture) { return texture.valid(); });
    RenderGraphBuilder pass = graph.addRasterPass(decals ? "GBuffer + Decals" : "GBuffer");
    for (std::size_t index = 0; index < resources.gbuffer.size(); ++index) {
        pass.setColorAttachment(resources.gbuffer[index], static_cast<uint8_t>(index));
    }
    pass.readTexture(resources.depth);
    pass.setDepthAttachment(resources.depth);
    if (decals) {
        for (RenderGraphTextureHandle decal : resources.decal) {
            pass.readTexture(decal);
        }
    }
    pass.allowCulling(false);
    pass.setRenderFunc([this, &scene, decals](RenderGraphContext& context) {
        renderGBufferPass(context.encoder(), context.viewId(), context.frameBuffer(), scene, decals);
    });
}

void ForwardRenderer::addSSAOPasses(RenderGraph& graph, FramePassResources& resources) {
    if (!resources.ssao.valid() || !resources.ssaoRaw.valid() || !resources.ssaoBlur.valid() ||
        !resources.depthPyramid.valid() || !resources.gbuffer[2].valid() || !resources.fullscreen.valid()) {
        return;
    }
    if (!m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSAO, 0, 0}).valid() ||
        !m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSAOBlur, 0, 0}).valid() ||
        !m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSAOUpsample, 0, 0}).valid()) {
        return;
    }

    const std::array<uint16_t, 2> resolution =
        ssaoResolution(m_viewState.renderWidth(), m_viewState.renderHeight(), m_ssaoSettings.halfResolution);

    RenderGraphBuilder trace = graph.addRasterPass("SSAO");
    trace.setColorAttachment(resources.ssaoRaw);
    trace.readTexture(resources.depthPyramid);
    trace.readTexture(resources.gbuffer[2]);
    trace.readBuffer(resources.fullscreen);
    trace.setViewSize(resolution[0], resolution[1]);
    trace.allowCulling(false);
    trace.setRenderFunc([this, resources](RenderGraphContext& context) {
        renderSSAOPass(context.encoder(), context.viewId(), context.frameBuffer(), context.width(), context.height(),
                       context.texture(resources.depthPyramid), context.texture(resources.gbuffer[2]));
    });

    RenderGraphBuilder blur = graph.addRasterPass("SSAO Blur");
    blur.setColorAttachment(resources.ssaoBlur);
    blur.readTexture(resources.ssaoRaw);
    blur.readTexture(resources.depthPyramid);
    blur.readTexture(resources.gbuffer[2]);
    blur.readBuffer(resources.fullscreen);
    blur.setViewSize(resolution[0], resolution[1]);
    blur.allowCulling(false);
    blur.setRenderFunc([this, resources](RenderGraphContext& context) {
        renderSSAOBlurPass(context.encoder(), context.viewId(), context.frameBuffer(), context.width(),
                           context.height(), context.texture(resources.ssaoRaw),
                           context.texture(resources.depthPyramid), context.texture(resources.gbuffer[2]));
    });

    RenderGraphTextureHandle upsampleInput = resources.ssaoBlur;
    const bool temporal = m_ssaoTemporalActive && resources.motionVector.valid() &&
                          m_ssaoHistoryTextures[0].valid() && m_ssaoHistoryTextures[1].valid() &&
                          m_ssaoHistoryWidth == resolution[0] && m_ssaoHistoryHeight == resolution[1];
    if (temporal) {
        const uint32_t previousIndex = m_ssaoHistoryIndex;
        const uint32_t nextIndex = previousIndex ^ 1u;
        const RenderGraphTextureHandle previous =
            graph.importTexture(m_ssaoHistoryTextures[previousIndex], "SSAO History Previous");
        const RenderGraphTextureHandle next =
            graph.importTexture(m_ssaoHistoryTextures[nextIndex], "SSAO History");
        const bool historyValid = m_ssaoHistoryValid && m_ssaoHistoryFrame > 0 && m_viewState.historyValid();
        if (historyValid) {
            resources.ssaoHistoryPrevious = previous;
        }

        RenderGraphBuilder temporalPass = graph.addRasterPass("SSAO Temporal");
        temporalPass.setColorAttachment(next);
        temporalPass.readTexture(resources.ssaoBlur);
        temporalPass.readTexture(resources.depthPyramid);
        temporalPass.readTexture(resources.motionVector);
        temporalPass.readTexture(previous);
        temporalPass.readBuffer(resources.fullscreen);
        temporalPass.setViewSize(resolution[0], resolution[1]);
        temporalPass.allowCulling(false);
        temporalPass.setRenderFunc([this, resources, previous, historyValid](RenderGraphContext& context) {
            renderSSAOTemporalPass(context.encoder(), context.viewId(), context.frameBuffer(), context.width(),
                                   context.height(), context.texture(resources.ssaoBlur),
                                   context.texture(resources.depthPyramid),
                                   context.texture(resources.motionVector), context.texture(previous),
                                   historyValid);
        });

        m_ssaoHistoryIndex = nextIndex;
        m_ssaoHistoryValid = true;
        ++m_ssaoHistoryFrame;
        resources.ssaoHistory = next;
        upsampleInput = next;
    } else {
        m_ssaoHistoryValid = false;
    }

    RenderGraphBuilder upsample = graph.addRasterPass("SSAO Upsample");
    upsample.setColorAttachment(resources.ssao);
    upsample.readTexture(upsampleInput);
    upsample.readTexture(resources.depthPyramid);
    upsample.readTexture(resources.gbuffer[2]);
    upsample.readBuffer(resources.fullscreen);
    upsample.allowCulling(false);
    upsample.setRenderFunc([this, resources, upsampleInput](RenderGraphContext& context) {
        renderSSAOUpsamplePass(context.encoder(), context.viewId(), context.frameBuffer(), context.width(),
                               context.height(), context.texture(upsampleInput),
                               context.texture(resources.depthPyramid), context.texture(resources.gbuffer[2]));
    });
}

void ForwardRenderer::addSSGIPasses(RenderGraph& graph, FramePassResources& resources) {
    if (!resources.ssgiRaw.valid() || !resources.ssgi.valid() || !resources.color.valid() ||
        !resources.depth.valid() || !resources.depthPyramid.valid() || !resources.gbuffer[0].valid() ||
        !resources.gbuffer[1].valid() || !resources.gbuffer[2].valid() || !resources.fullscreen.valid()) {
        return;
    }
    if (!m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSGI, 0, 0}).valid() ||
        !m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSGIDenoise, 0, 0}).valid() ||
        !m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSGIComposite, 0, 0}).valid()) {
        return;
    }

    const std::array<uint16_t, 2> resolution =
        ssgiResolution(m_viewState.renderWidth(), m_viewState.renderHeight(), m_ssgiSettings.halfResolution);

    RenderGraphBuilder trace = graph.addRasterPass("SSGI Trace");
    trace.setColorAttachment(resources.ssgiRaw);
    trace.readTexture(resources.color);
    trace.readTexture(resources.depthPyramid);
    trace.readTexture(resources.gbuffer[2]);
    if (resources.environment.valid()) {
        trace.readTexture(resources.environment);
    }
    trace.readBuffer(resources.fullscreen);
    trace.setViewSize(resolution[0], resolution[1]);
    trace.allowCulling(false);
    trace.setRenderFunc([this, resources](RenderGraphContext& context) {
        renderSSGITracePass(context.encoder(), context.viewId(), context.frameBuffer(), context.width(),
                            context.height(), context.texture(resources.color),
                            context.texture(resources.depthPyramid), context.texture(resources.gbuffer[2]),
                            context.texture(resources.environment));
    });

    RenderGraphTextureHandle denoiseInput = resources.ssgiRaw;
    const bool temporal = m_ssgiTemporalActive && resources.motionVector.valid() &&
                          m_ssgiHistoryTextures[0].valid() && m_ssgiHistoryTextures[1].valid() &&
                          m_ssgiHistoryDepthTextures[0].valid() && m_ssgiHistoryDepthTextures[1].valid() &&
                          m_ssgiHistoryNormalTextures[0].valid() && m_ssgiHistoryNormalTextures[1].valid() &&
                          m_ssgiHistoryWidth == resolution[0] && m_ssgiHistoryHeight == resolution[1] &&
                          m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSGITemporal, 0, 0}).valid();
    if (temporal) {
        const uint32_t previousIndex = m_ssgiHistoryIndex;
        const uint32_t nextIndex = previousIndex ^ 1u;
        const RenderGraphTextureHandle previous =
            graph.importTexture(m_ssgiHistoryTextures[previousIndex], "SSGI History Previous");
        const RenderGraphTextureHandle next =
            graph.importTexture(m_ssgiHistoryTextures[nextIndex], "SSGI History");
        const RenderGraphTextureHandle previousDepth =
            graph.importTexture(m_ssgiHistoryDepthTextures[previousIndex], "SSGI History Depth Previous");
        const RenderGraphTextureHandle nextDepth =
            graph.importTexture(m_ssgiHistoryDepthTextures[nextIndex], "SSGI History Depth");
        const RenderGraphTextureHandle previousNormal =
            graph.importTexture(m_ssgiHistoryNormalTextures[previousIndex], "SSGI History Normal Previous");
        const RenderGraphTextureHandle nextNormal =
            graph.importTexture(m_ssgiHistoryNormalTextures[nextIndex], "SSGI History Normal");
        const bool historyValid = m_ssgiHistoryValid && m_ssgiHistoryFrame > 0 && m_viewState.historyValid();
        if (historyValid) {
            resources.ssgiHistoryPrevious = previous;
        }

        RenderGraphBuilder temporalPass = graph.addRasterPass("SSGI Temporal");
        temporalPass.setColorAttachment(next, 0);
        temporalPass.setColorAttachment(nextDepth, 1);
        temporalPass.setColorAttachment(nextNormal, 2);
        temporalPass.readTexture(resources.ssgiRaw);
        temporalPass.readTexture(resources.depthPyramid);
        temporalPass.readTexture(resources.motionVector);
        temporalPass.readTexture(resources.gbuffer[2]);
        temporalPass.readTexture(previous);
        temporalPass.readTexture(previousDepth);
        temporalPass.readTexture(previousNormal);
        temporalPass.readBuffer(resources.fullscreen);
        temporalPass.setViewSize(resolution[0], resolution[1]);
        temporalPass.allowCulling(false);
        temporalPass.setRenderFunc([this, resources, previous, previousDepth, previousNormal,
                                     historyValid](RenderGraphContext& context) {
            renderSSGITemporalPass(context.encoder(), context.viewId(), context.frameBuffer(), context.width(),
                                   context.height(), context.texture(resources.ssgiRaw),
                                   context.texture(resources.depthPyramid),
                                   context.texture(resources.motionVector), context.texture(previous),
                                   context.texture(previousDepth), context.texture(previousNormal),
                                   context.texture(resources.gbuffer[2]), historyValid);
        });

        m_ssgiHistoryIndex = nextIndex;
        m_ssgiHistoryValid = true;
        ++m_ssgiHistoryFrame;
        denoiseInput = next;
    } else {
        m_ssgiHistoryValid = false;
    }

    RenderGraphBuilder denoise = graph.addRasterPass("SSGI Denoise");
    denoise.setColorAttachment(resources.ssgi);
    denoise.readTexture(denoiseInput);
    denoise.readTexture(resources.depthPyramid);
    denoise.readBuffer(resources.fullscreen);
    denoise.setViewSize(resolution[0], resolution[1]);
    denoise.allowCulling(false);
    denoise.setRenderFunc([this, resources, denoiseInput](RenderGraphContext& context) {
        renderSSGIDenoisePass(context.encoder(), context.viewId(), context.frameBuffer(), context.width(),
                              context.height(), context.texture(denoiseInput),
                              context.texture(resources.depthPyramid));
    });

    RenderGraphBuilder composite = graph.addRasterPass("SSGI Composite");
    composite.setColorAttachment(resources.color);
    composite.setDepthAttachment(resources.depth);
    composite.readTexture(resources.ssgi);
    composite.readTexture(resources.depth);
    composite.readTexture(resources.depthPyramid);
    composite.readTexture(resources.gbuffer[0]);
    composite.readTexture(resources.gbuffer[1]);
    if (resources.ssao.valid()) {
        composite.readTexture(resources.ssao);
    }
    if (resources.bakedIndirect.valid()) {
        composite.readTexture(resources.bakedIndirect);
    }
    composite.readBuffer(resources.fullscreen);
    composite.allowCulling(false);
    composite.setRenderFunc([this, resources](RenderGraphContext& context) {
        renderSSGICompositePass(context.encoder(), context.viewId(), context.frameBuffer(), context.width(),
                                context.height(), context.texture(resources.ssgi),
                                context.texture(resources.depthPyramid), context.texture(resources.gbuffer[0]),
                                context.texture(resources.gbuffer[1]), context.texture(resources.ssao),
                                resources.bakedIndirect.valid() ? context.texture(resources.bakedIndirect)
                                                                : gfx::TextureHandle{});
    });
}

void ForwardRenderer::addSSRPasses(RenderGraph& graph, FramePassResources& resources) {
    if (!resources.ssr.valid() || !resources.color.valid() || !resources.depth.valid() ||
        !resources.depthPyramid.valid() || !resources.gbuffer[0].valid() || !resources.gbuffer[1].valid() ||
        !resources.gbuffer[2].valid() || !resources.fullscreen.valid()) {
        return;
    }
    if (!m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSR, 0, 0}).valid() ||
        !m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSRComposite, 0, 0}).valid()) {
        return;
    }

    resources.ssrRaw = resources.ssr;
    RenderGraphBuilder trace = graph.addRasterPass("SSR");
    trace.setColorAttachment(resources.ssr);
    trace.setDepthAttachment(resources.depth);
    trace.readTexture(resources.color);
    trace.readTexture(resources.depth);
    trace.readTexture(resources.depthPyramid);
    trace.readTexture(resources.gbuffer[0]);
    trace.readTexture(resources.gbuffer[1]);
    trace.readTexture(resources.gbuffer[2]);
    if (resources.environment.valid()) {
        trace.readTexture(resources.environment);
    }
    if (resources.ssao.valid()) {
        trace.readTexture(resources.ssao);
    }
    trace.readBuffer(resources.fullscreen);
    trace.allowCulling(false);
    trace.setRenderFunc([this, resources](RenderGraphContext& context) {
        renderSSRTracePass(context.encoder(), context.viewId(), context.frameBuffer(), context.width(),
                           context.height(), context.texture(resources.color),
                           context.texture(resources.depthPyramid), context.texture(resources.gbuffer[0]),
                           context.texture(resources.gbuffer[1]), context.texture(resources.gbuffer[2]),
                           context.texture(resources.environment), context.texture(resources.ssao));
    });

    const bool temporal = m_ssrTemporalActive && resources.motionVector.valid() &&
                          m_ssrHistoryTextures[0].valid() && m_ssrHistoryTextures[1].valid() &&
                          m_ssrHistoryWidth == m_viewState.renderWidth() &&
                          m_ssrHistoryHeight == m_viewState.renderHeight() &&
                          m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSRTemporal, 0, 0}).valid();
    if (temporal) {
        const uint32_t previousIndex = m_ssrHistoryIndex;
        const uint32_t nextIndex = previousIndex ^ 1u;
        const RenderGraphTextureHandle previous =
            graph.importTexture(m_ssrHistoryTextures[previousIndex], "SSR History Previous");
        const RenderGraphTextureHandle next =
            graph.importTexture(m_ssrHistoryTextures[nextIndex], "SSR History");
        const bool historyValid = m_ssrHistoryValid && m_ssrHistoryFrame > 0 && m_viewState.historyValid();
        if (historyValid) {
            resources.ssrHistoryPrevious = previous;
        }

        RenderGraphBuilder temporalPass = graph.addRasterPass("SSR Temporal");
        temporalPass.setColorAttachment(next);
        temporalPass.readTexture(resources.ssr);
        temporalPass.readTexture(resources.depthPyramid);
        temporalPass.readTexture(resources.motionVector);
        temporalPass.readTexture(previous);
        temporalPass.readBuffer(resources.fullscreen);
        temporalPass.allowCulling(false);
        temporalPass.setRenderFunc([this, resources, previous, historyValid](RenderGraphContext& context) {
            renderSSRTemporalPass(context.encoder(), context.viewId(), context.frameBuffer(), context.width(),
                                  context.height(), context.texture(resources.ssr),
                                  context.texture(resources.depthPyramid),
                                  context.texture(resources.motionVector), context.texture(previous),
                                  historyValid);
        });

        m_ssrHistoryIndex = nextIndex;
        m_ssrHistoryValid = true;
        ++m_ssrHistoryFrame;
        resources.ssr = next;
    } else {
        m_ssrHistoryValid = false;
    }

    RenderGraphBuilder composite = graph.addRasterPass("SSR Composite");
    composite.setColorAttachment(resources.color);
    composite.setDepthAttachment(resources.depth);
    composite.readTexture(resources.ssr);
    composite.readTexture(resources.depth);
    composite.readBuffer(resources.fullscreen);
    composite.allowCulling(false);
    composite.setRenderFunc([this, resources](RenderGraphContext& context) {
        renderSSRCompositePass(context.encoder(), context.viewId(), context.frameBuffer(), context.width(),
                               context.height(), context.texture(resources.ssr));
    });
}

void ForwardRenderer::addLitVariantTilePasses(RenderGraph& graph, const FramePassResources& resources) {
    if (!m_litVariantTileActive || !resources.litTileFeatureFlags.valid() ||
        !resources.litIndirectArgs.valid() || !resources.lightFptlBuffer.valid() ||
        !resources.litTileFeatureFlags.valid() || m_litTileWidth == 0 || m_litTileHeight == 0 ||
        !std::all_of(resources.gbuffer.begin(), resources.gbuffer.end(),
                     [](RenderGraphTextureHandle texture) { return texture.valid(); })) {
        return;
    }

    const gfx::ProgramHandle materialFlagsProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LitMaterialFlags, 0, 0});
    const gfx::ProgramHandle clearProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LitVariantIndirectClear, 0, 0});
    const gfx::ProgramHandle indirectProgram =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LitVariantIndirect, 0, 0});
    if (!materialFlagsProgram.valid() || !clearProgram.valid() || !indirectProgram.valid()) {
        return;
    }

    graph.addComputePass("Lit Tile Feature Flags")
        .readTexture(resources.gbuffer[4])
        .readTexture(resources.depthPyramid)
        .writeTexture(resources.litTileFeatureFlags)
        .allowCulling(false)
        .setRenderFunc([this, resources, materialFlagsProgram](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setViewRect(context.viewId(), 0, 0, context.width(), context.height());
            encoder.setViewFrameBuffer(context.viewId(), {});
            m_passBinder.upload(m_passUniforms);
            encoder.setTexture(1, m_gbufferSamplers[4], context.texture(resources.gbuffer[4]),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setTexture(2, m_depthPyramidSampler, context.texture(resources.depthPyramid),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setImage(0, context.texture(resources.litTileFeatureFlags), 0, bgfx::Access::Write,
                             gfx::TextureFormat::R32U);
            encoder.dispatch(context.viewId(), materialFlagsProgram, m_litTileWidth, m_litTileHeight, 1);
        });

    const uint32_t tileCount = static_cast<uint32_t>(m_litTileWidth) * m_litTileHeight;
    const uint32_t clearGroups = (tileCount * kLitVariantCount + 63u) / 64u;
    const uint32_t groups = (tileCount + 63u) / 64u;
    graph.addComputePass("Lit Variant Indirect Clear")
        .readBuffer(resources.lightFptlBuffer)
        .writeBuffer(resources.lightFptlBuffer)
        .writeBuffer(resources.litIndirectArgs)
        .allowCulling(false)
        .setRenderFunc([this, resources, clearProgram, clearGroups](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setViewRect(context.viewId(), 0, 0, context.width(), context.height());
            encoder.setViewFrameBuffer(context.viewId(), {});
            m_passBinder.upload(m_passUniforms);
            encoder.setStorageBuffer(1, context.buffer(resources.litIndirectArgs), bgfx::Access::Write);
            encoder.setStorageBuffer(2, context.buffer(resources.lightFptlBuffer), bgfx::Access::Write);
            encoder.dispatch(context.viewId(), clearProgram, clearGroups, 1, 1);
        });
    graph.addComputePass("Lit Variant Indirect")
        .readTexture(resources.litTileFeatureFlags)
        .readBuffer(resources.lightFptlBuffer)
        .writeBuffer(resources.lightFptlBuffer)
        .writeBuffer(resources.litIndirectArgs)
        .allowCulling(false)
        .setRenderFunc([this, resources, indirectProgram, groups](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setViewRect(context.viewId(), 0, 0, context.width(), context.height());
            encoder.setViewFrameBuffer(context.viewId(), {});
            m_passBinder.upload(m_passUniforms);
            encoder.setTexture(0, m_lightListSampler, context.texture(resources.litTileFeatureFlags),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setStorageBuffer(1, context.buffer(resources.lightFptlBuffer), bgfx::Access::Write);
            encoder.setStorageBuffer(2, context.buffer(resources.litIndirectArgs), bgfx::Access::Write);
            encoder.dispatch(context.viewId(), indirectProgram, groups, 1, 1);
        });
}

void ForwardRenderer::addDeferredLightingComputePass(RenderGraph& graph,
                                                     const FramePassResources& resources) {
    if (!m_litVariantTileActive || !resources.color.valid() || !resources.depthPyramid.valid() ||
        !resources.lightData.valid() || !resources.lightFptlBuffer.valid() ||
        !resources.litIndirectArgs.valid() ||
        !std::all_of(resources.gbuffer.begin(), resources.gbuffer.end(),
                     [](RenderGraphTextureHandle texture) { return texture.valid(); })) {
        return;
    }

    RenderGraphBuilder pass = graph.addComputePass("Deferred Lighting Compute");
    pass.writeTexture(resources.color);
    pass.readTexture(resources.depthPyramid);
    for (RenderGraphTextureHandle gbuffer : resources.gbuffer) {
        pass.readTexture(gbuffer);
    }
    pass.readTexture(resources.lightData);
    pass.readBuffer(resources.lightFptlBuffer);
    pass.readBuffer(resources.litIndirectArgs);
    if (resources.directionalShadow.valid()) {
        pass.readTexture(resources.directionalShadow);
    }
    if (resources.punctualShadow.valid()) {
        pass.readTexture(resources.punctualShadow);
    }
    if (resources.areaShadow.valid() && m_shadowManager.areaRequestCount() > 0) {
        pass.readTexture(resources.areaShadow);
    }
    if (resources.shadowContext.valid()) {
        pass.readBuffer(resources.shadowContext);
    }
    if (resources.environment.valid()) {
        pass.readTexture(resources.environment);
    }
    if (resources.ssao.valid()) {
        pass.readTexture(resources.ssao);
    }
    pass.allowCulling(false);
    pass.setRenderFunc([this, resources](RenderGraphContext& context) {
        gfx::Encoder& encoder = context.encoder();
        encoder.setViewRect(context.viewId(), 0, 0, context.width(), context.height());
        encoder.setViewFrameBuffer(context.viewId(), {});
        encoder.setViewClear(context.viewId(), BGFX_CLEAR_NONE, 0, 1.0f, 0);

        m_globalBinder.upload(m_globalUniforms);
        m_contactBinder.upload(m_contactUniforms);
        m_passBinder.upload(m_passUniforms);
        gfx::generated::ClusterUniforms clusterUniforms = m_clusterUniforms;
        clusterUniforms.depthParams.w = 0.0f;
        m_clusterBinder.upload(clusterUniforms);
        m_environmentBinder.upload(m_environmentUniforms);
        m_probeBinder.upload(m_probeUniforms);
        gfx::generated::ObjectUniforms objectUniforms = m_objectUniforms;
        objectUniforms.lightLayerMask =
            Vec4(static_cast<float>(kRenderingLayersMask), 127.0f, 0.0f, m_deferredLightLayersActive ? 1.0f : 0.0f);
        m_objectBinder.upload(objectUniforms);

        const std::array<gfx::TextureHandle, 5> gbuffer = {
            context.texture(resources.gbuffer[0]), context.texture(resources.gbuffer[1]),
            context.texture(resources.gbuffer[2]), context.texture(resources.gbuffer[3]),
            context.texture(resources.gbuffer[4]),
        };
        for (std::size_t index = 0; index < 4; ++index) {
            encoder.setTexture(static_cast<uint8_t>(index), m_gbufferSamplers[index], gbuffer[index],
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        }
        encoder.setTexture(4, m_depthPyramidSampler, context.texture(resources.depthPyramid),
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        encoder.setTexture(5, m_lightDataSampler, context.texture(resources.lightData),
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        bindLightList(encoder, false);
        if (resources.shadowContext.valid()) {
            encoder.setStorageBuffer(8, context.buffer(resources.shadowContext), bgfx::Access::Read);
        }
        if (resources.punctualShadow.valid()) {
            encoder.setTexture(9, m_shadowPunctualSampler, context.texture(resources.punctualShadow),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        }
        const gfx::TextureHandle environmentTexture =
            resources.environment.valid() ? context.texture(resources.environment) : m_environmentFallback;
        encoder.setTexture(10, m_environmentSampler, environmentTexture,
                           resources.environment.valid() ? BGFX_SAMPLER_V_CLAMP : BGFX_SAMPLER_UVW_CLAMP);
        bindCookieAtlas(encoder, 11);
        if (resources.areaShadow.valid() && m_shadowManager.areaRequestCount() > 0) {
            const uint32_t flags = m_shadowManager.areaEvsmBlurActive()
                                       ? BGFX_SAMPLER_UVW_CLAMP
                                       : (BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setTexture(13, m_shadowPointPunctualSampler,
                               m_shadowManager.activeAreaSamplingTexture(), flags);
        }
        bindProbeTextures(encoder, false);
        const gfx::TextureHandle ssaoTexture =
            resources.ssao.valid() ? context.texture(resources.ssao) : m_ssaoFallback;
        encoder.setTexture(15, m_ssaoSampler, ssaoTexture,
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        encoder.setImage(14, context.texture(resources.color), 0, bgfx::Access::Write,
                         gfx::TextureFormat::RGBA16F);

        const uint32_t dispatchMask = m_usedLitVariantMask;
        const bool imsRequested = m_shadowManager.imsActive();
        if (m_frameIndex == 0) {
            HBG_LOGI("lit tile dispatch: used_mask=0x{:x} dispatch_mask=0x{:x}", m_usedLitVariantMask,
                     dispatchMask);
        }
        for (uint32_t variant = 0; variant < kLitVariantCount; ++variant) {
            if ((dispatchMask & (1u << variant)) == 0) {
                continue;
            }
            const bool variantAdvanced =
                (kLitFeatureVariantFlags[variant] & (kLitMaterialAll & ~kLitMaterialStandard)) != 0;
            gfx::ProgramHandle program;
            bool variantIms = false;
            if (imsRequested) {
                program = m_shaderLibrary.program(
                    shader::ProgramKey{shader::PassId::DeferredLightingVariantComputeIms, variant,
                                       shader::kLightListBufferProgramKey});
                variantIms = variantAdvanced && program.valid();
            }
            if (!program.valid()) {
                program = m_shaderLibrary.program(
                    shader::ProgramKey{shader::PassId::DeferredLightingVariantCompute, variant,
                                       shader::kLightListBufferProgramKey});
                if (program.valid() && variantAdvanced && imsRequested && !m_litVariantImsWarningLogged) {
                    HBG_LOGW("advanced lit-variant IMS compute program is unavailable; "
                             "falling back to raw-depth variant");
                    m_litVariantImsWarningLogged = true;
                }
            }
            if (!program.valid()) {
                continue;
            }
            if (variantIms) {
                const gfx::TextureHandle sat = m_shadowManager.directionalMomentSatTexture();
                if (sat.valid()) {
                    encoder.setTexture(6, m_shadowPointSampler, sat,
                                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                }
                encoder.setTexture(12, m_gbufferSamplers[4], gbuffer[4],
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            } else if (variantAdvanced) {
                if (resources.directionalShadow.valid()) {
                    encoder.setTexture(6, m_shadowSampler,
                                       directionalShadowSamplingTexture(context.texture(resources.directionalShadow)),
                                       directionalShadowSamplingFlags());
                }
                encoder.setTexture(12, m_gbufferSamplers[4], gbuffer[4],
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            } else {
                if (resources.directionalShadow.valid()) {
                    encoder.setTexture(6, m_shadowSampler,
                                       directionalShadowSamplingTexture(context.texture(resources.directionalShadow)),
                                       directionalShadowSamplingFlags());
                    bindShadowMomentSat(encoder, context.texture(resources.directionalShadow));
                }
            }
            encoder.dispatchIndirect(context.viewId(), program, context.buffer(resources.litIndirectArgs),
                                     variant, 1);
        }
    });
}

void ForwardRenderer::addDeferredLightingPass(RenderGraph& graph, const FramePassResources& resources) {
    if (!m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects) || m_gbufferDrawList.empty() ||
        !resources.color.valid() || !resources.depth.valid() || !resources.depthPyramid.valid() ||
        !resources.lightData.valid() || !resources.fullscreen.valid()) {
        return;
    }
    if (!std::all_of(resources.gbuffer.begin(), resources.gbuffer.end(),
                     [](RenderGraphTextureHandle texture) { return texture.valid(); })) {
        return;
    }
    if (m_litVariantTileActive) {
        addDeferredLightingComputePass(graph, resources);
        return;
    }

    const bool advancedRequested = m_advancedMaterialActive;
    bool advancedIms = advancedRequested && !m_litVariantsActive && m_shadowManager.imsActive();
    shader::PassId deferredPass =
        m_litVariantsActive
            ? shader::PassId::DeferredLightingVariant
            : (advancedIms ? shader::PassId::DeferredLightingAdvancedIms
                           : (advancedRequested ? shader::PassId::DeferredLightingAdvanced
                                                : shader::PassId::DeferredLighting));
    gfx::ProgramHandle program =
        m_litVariantsActive ? lightListProgram(shader::PassId::DeferredLightingVariant, 0)
                            : lightListProgram(deferredPass);
    if (!program.valid() && advancedIms) {
        if (m_frameIndex == 0) {
            HBG_LOGW("advanced deferred IMS program is invalid; falling back to raw-depth advanced deferred");
        }
        advancedIms = false;
        deferredPass = shader::PassId::DeferredLightingAdvanced;
        program = lightListProgram(deferredPass);
    }
    if (!program.valid() && advancedRequested) {
        if (m_frameIndex == 0) {
            HBG_LOGW("advanced deferred lighting program is invalid; falling back to standard deferred");
        }
        advancedIms = false;
        deferredPass = shader::PassId::DeferredLighting;
        program = lightListProgram(deferredPass);
    }
    if (!program.valid()) {
        if (m_frameIndex == 0) {
            HBG_LOGW("deferred lighting program is invalid; deferred opaque will not be lit");
        }
        return;
    }
    const bool advanced = deferredPass == shader::PassId::DeferredLightingAdvanced ||
                          deferredPass == shader::PassId::DeferredLightingAdvancedIms;
    m_advancedLightingActive = advanced;

    RenderGraphBuilder pass = graph.addRasterPass("Deferred Lighting");
    pass.setColorAttachment(resources.color);
    pass.setDepthAttachment(resources.depth);
    pass.readTexture(resources.depthPyramid);
    for (RenderGraphTextureHandle gbuffer : resources.gbuffer) {
        pass.readTexture(gbuffer);
    }
    pass.readTexture(resources.lightData);
    if (m_gpuLightListBufferActive && resources.lightFptlBuffer.valid()) {
        pass.readBuffer(resources.lightFptlBuffer);
    } else if (resources.lightList.valid()) {
        pass.readTexture(resources.lightList);
    }
    if (resources.directionalShadow.valid()) {
        pass.readTexture(resources.directionalShadow);
    }
    if (resources.punctualShadow.valid()) {
        pass.readTexture(resources.punctualShadow);
    }
    if (resources.areaShadow.valid() && m_shadowManager.areaRequestCount() > 0) {
        pass.readTexture(resources.areaShadow);
    }
    if (resources.shadowContext.valid()) {
        pass.readBuffer(resources.shadowContext);
    }
    if (resources.environment.valid()) {
        pass.readTexture(resources.environment);
    }
    if (resources.ssao.valid()) {
        pass.readTexture(resources.ssao);
    }
    pass.readBuffer(resources.fullscreen);
    pass.setRenderFunc([this, resources, program, advanced, advancedIms](RenderGraphContext& context) {
        const std::array<gfx::TextureHandle, 5> gbuffer = {
            context.texture(resources.gbuffer[0]),
            context.texture(resources.gbuffer[1]),
            context.texture(resources.gbuffer[2]),
            context.texture(resources.gbuffer[3]),
            context.texture(resources.gbuffer[4]),
        };
        renderDeferredLighting(context.encoder(), context.viewId(), context.frameBuffer(),
                               context.texture(resources.depthPyramid), gbuffer,
                               context.texture(resources.lightData),
                               context.texture(resources.lightList), context.texture(resources.directionalShadow),
                               context.texture(resources.punctualShadow), context.buffer(resources.shadowContext),
                               context.texture(resources.environment), context.texture(resources.ssao),
                               advanced, advancedIms);
    });
}

bool ForwardRenderer::ensureVfxRuntime() {
    if (m_vfxRuntime != nullptr) {
        return true;
    }
    if (!m_pipelineSettings.supportVFX || m_resources == nullptr) {
        return false;
    }

    m_vfxBackend = std::make_unique<hbg::vfxbridge::HbgVfxBackend>(*m_resources);
    m_vfxRuntime = std::make_unique<::vfx::Runtime>(m_vfxBackend->device(), &m_vfxBackend->shading());
    m_vfxRuntime->setErrorCallback([](std::string_view message) {
        HBG_LOGW("VFX runtime: {}", message);
    });
    m_vfxInitialized = true;
    return true;
}

bool ForwardRenderer::replaceVfxRuntime(VfxInstanceRuntime& runtime,
                                         const std::filesystem::path& binaryPath, bool preview,
                                         std::string& error) {
    if (m_vfxRuntime == nullptr) {
        error = "no VFX runtime is active";
        return false;
    }

    std::string localError;
    const std::shared_ptr<const ::vfx::Asset> asset = m_vfxRuntime->loadAsset(binaryPath, &localError);
    if (asset == nullptr) {
        error = localError;
        return false;
    }

    ::vfx::EffectDesc desc;
    desc.transform = runtime.instance != nullptr ? toVfxMatrix(vfxInstanceWorld(*runtime.instance))
                                                  : ::vfx::Mat4::identity();
    desc.playOnAwake = runtime.playing;
    desc.playRate = runtime.playRate;
    desc.randomSeed = runtime.randomSeed;
    ::vfx::Effect* replacement =
        runtime.effect != nullptr
            ? m_vfxRuntime->reloadEffect(runtime.effect, *asset, desc, &localError)
            : m_vfxRuntime->createEffect(*asset, desc, &localError);
    if (replacement == nullptr) {
        error = localError;
        return false;
    }

    runtime.effect = replacement;
    runtime.preview = preview;
    replacement->setPlayRate(runtime.playRate);
    if (runtime.paused) {
        replacement->pause();
    } else if (runtime.playing) {
        replacement->play();
    }

    if (runtime.instance != nullptr) {
        for (const VfxPropertyValue& property : runtime.instance->properties) {
            if (!replacement->setProperty(property.name, toVfxProperty(property))) {
                HBG_LOGW("VFX effect '{}': property '{}' was not applied", runtime.instance->name,
                         property.name);
            }
        }
    }
    return true;
}

void ForwardRenderer::ensureVfx(const RenderScene& scene) {
    if (!m_pipelineSettings.supportVFX || scene.vfxInstances.empty() || m_vfxSceneInitialized) {
        return;
    }
    if (!ensureVfxRuntime()) {
        return;
    }
    m_vfxSceneInitialized = true;

    for (const VfxInstance& instance : scene.vfxInstances) {
        if (!instance.enabled) {
            continue;
        }
        VfxInstanceRuntime runtime;
        runtime.instance = &instance;
        runtime.assetPath = instance.asset;
        runtime.playRate = instance.playRate;
        runtime.randomSeed = instance.randomSeed;
        runtime.playing = instance.playOnAwake;
        runtime.paused = !instance.playOnAwake;
        std::string error;
        if (!replaceVfxRuntime(runtime, instance.asset, false, error)) {
            HBG_LOGW("VFX instance '{}': {}", instance.name, error);
            continue;
        }
        m_vfxInstanceRuntimes.push_back(std::move(runtime));
    }
    m_vfxActive = !m_vfxInstanceRuntimes.empty();
    m_vfxHasLitOutputs = m_vfxRuntime->hasLitOutputs();
    if (m_vfxActive) {
        HBG_LOGI("VFX runtime: active effects={}", m_vfxInstanceRuntimes.size());
    }
}

bool ForwardRenderer::reloadVfxAsset(const std::filesystem::path& binaryPath, std::string& error) {
    if (m_vfxRuntime == nullptr) {
        error = "no VFX runtime is active for the loaded scene";
        return false;
    }

    bool matched = false;
    bool reloaded = false;
    for (VfxInstanceRuntime& runtime : m_vfxInstanceRuntimes) {
        if (runtime.instance == nullptr || !equivalentPath(runtime.instance->asset, binaryPath)) {
            continue;
        }
        matched = true;
        std::string localError;
        if (!replaceVfxRuntime(runtime, runtime.instance->asset, false, localError)) {
            error = localError;
            continue;
        }
        reloaded = true;
    }

    if (!matched) {
        error = "no scene VFX instance uses '" + binaryPath.string() + "'";
        return false;
    }
    if (!reloaded) {
        return false;
    }
    m_vfxActive = true;
    m_vfxHasLitOutputs = m_vfxRuntime->hasLitOutputs();
    HBG_LOGI("VFX hot reload: '{}'", binaryPath.string());
    error.clear();
    return true;
}

bool ForwardRenderer::previewVfxAsset(const std::filesystem::path& sourceAssetPath,
                                      const std::filesystem::path& binaryPath, std::string& error) {
    if (!ensureVfxRuntime()) {
        error = "VFX runtime is unavailable (supportVFX is disabled)";
        return false;
    }

    bool matched = false;
    bool replaced = false;
    for (VfxInstanceRuntime& runtime : m_vfxInstanceRuntimes) {
        if (!equivalentPath(runtime.assetPath, sourceAssetPath)) {
            continue;
        }
        matched = true;
        std::string localError;
        if (!replaceVfxRuntime(runtime, binaryPath, true, localError)) {
            error = localError;
            continue;
        }
        replaced = true;
    }

    if (!matched) {
        VfxInstanceRuntime runtime;
        runtime.assetPath = sourceAssetPath;
        runtime.playRate = 1.0f;
        runtime.randomSeed = 0;
        runtime.playing = true;
        runtime.paused = false;
        if (!replaceVfxRuntime(runtime, binaryPath, true, error)) {
            return false;
        }
        m_vfxInstanceRuntimes.push_back(std::move(runtime));
        replaced = true;
    }

    if (!replaced) {
        if (error.empty()) {
            error = "VFX preview could not be loaded";
        }
        return false;
    }
    m_vfxActive = true;
    m_vfxHasLitOutputs = m_vfxRuntime->hasLitOutputs();
    HBG_LOGI("VFX preview: '{}' -> '{}'", sourceAssetPath.string(), binaryPath.string());
    error.clear();
    return true;
}

bool ForwardRenderer::setVfxPlayRate(const std::filesystem::path& assetPath, float rate,
                                     std::string& error) {
    if (!std::isfinite(rate)) {
        error = "VFX play rate must be finite";
        return false;
    }
    bool matched = false;
    for (VfxInstanceRuntime& runtime : m_vfxInstanceRuntimes) {
        if (!equivalentPath(runtime.assetPath, assetPath)) {
            continue;
        }
        matched = true;
        runtime.playRate = std::clamp(rate, 0.0f, 100.0f);
        if (runtime.effect != nullptr) {
            runtime.effect->setPlayRate(runtime.playRate);
        }
    }
    if (!matched) {
        error = "no VFX effect uses '" + assetPath.string() + "'";
        return false;
    }
    error.clear();
    return true;
}

bool ForwardRenderer::toggleVfxPaused(const std::filesystem::path& assetPath, std::string& error) {
    bool matched = false;
    for (VfxInstanceRuntime& runtime : m_vfxInstanceRuntimes) {
        if (!equivalentPath(runtime.assetPath, assetPath)) {
            continue;
        }
        matched = true;
        if (runtime.effect == nullptr) {
            continue;
        }
        if (!runtime.playing || runtime.paused) {
            runtime.effect->play();
            runtime.playing = true;
            runtime.paused = false;
        } else {
            runtime.effect->pause();
            runtime.paused = true;
        }
    }
    if (!matched) {
        error = "no VFX effect uses '" + assetPath.string() + "'";
        return false;
    }
    error.clear();
    return true;
}

bool ForwardRenderer::stepVfx(const std::filesystem::path& assetPath, std::string& error) {
    bool matched = false;
    for (VfxInstanceRuntime& runtime : m_vfxInstanceRuntimes) {
        if (!equivalentPath(runtime.assetPath, assetPath)) {
            continue;
        }
        matched = true;
        if (runtime.effect == nullptr) {
            continue;
        }
        runtime.effect->advanceOneFrame();
        runtime.playing = true;
        runtime.paused = true;
    }
    if (!matched) {
        error = "no VFX effect uses '" + assetPath.string() + "'";
        return false;
    }
    error.clear();
    return true;
}

bool ForwardRenderer::restartVfx(const std::filesystem::path& assetPath, std::string& error) {
    bool matched = false;
    for (VfxInstanceRuntime& runtime : m_vfxInstanceRuntimes) {
        if (!equivalentPath(runtime.assetPath, assetPath)) {
            continue;
        }
        matched = true;
        if (runtime.effect == nullptr) {
            continue;
        }
        runtime.effect->reset();
        runtime.effect->play();
        runtime.playing = true;
        runtime.paused = false;
    }
    if (!matched) {
        error = "no VFX effect uses '" + assetPath.string() + "'";
        return false;
    }
    error.clear();
    return true;
}

ForwardRenderer::VfxPlaybackStatus ForwardRenderer::vfxPlaybackStatus(
    const std::filesystem::path& assetPath) const {
    VfxPlaybackStatus status;
    for (const VfxInstanceRuntime& runtime : m_vfxInstanceRuntimes) {
        if (!equivalentPath(runtime.assetPath, assetPath)) {
            continue;
        }
        if (!status.available) {
            status.playRate = runtime.playRate;
            status.randomSeed = runtime.randomSeed;
            status.preview = runtime.preview;
        }
        status.available = true;
        status.paused = status.paused || runtime.paused || !runtime.playing;
        status.preview = status.preview || runtime.preview;
        ++status.instances;
        if (runtime.effect != nullptr) {
            const ::vfx::Bounds bounds = runtime.effect->bounds();
            if (bounds.valid) {
                status.bounds.expand(Vec3{bounds.min.x, bounds.min.y, bounds.min.z});
                status.bounds.expand(Vec3{bounds.max.x, bounds.max.y, bounds.max.z});
            }
        }
    }
    return status;
}

void ForwardRenderer::releaseVfx() {
    m_vfxInstanceRuntimes.clear();
    m_vfxRuntime.reset();
    m_vfxBackend.reset();
    m_vfxInitialized = false;
    m_vfxSceneInitialized = false;
    m_vfxActive = false;
    m_vfxHasLitOutputs = false;
    m_vfxClusterReady = false;
}

void ForwardRenderer::updateVfx(const RenderScene& scene, float deltaSeconds) {
    (void)scene;
    if (m_vfxRuntime == nullptr) {
        return;
    }
    for (VfxInstanceRuntime& runtime : m_vfxInstanceRuntimes) {
        if (runtime.instance != nullptr && runtime.effect != nullptr) {
            runtime.effect->setTransform(toVfxMatrix(vfxInstanceWorld(*runtime.instance)));
        }
    }
    m_vfxDeltaSeconds = deltaSeconds;
    m_vfxRuntime->update(deltaSeconds);
}

bool ForwardRenderer::prepareVfxClusterLighting() {
    m_vfxClusterReady = false;
    m_vfxHasLitOutputs = m_vfxRuntime != nullptr && m_vfxRuntime->hasLitOutputs();
    if (!m_vfxActive || !m_vfxHasLitOutputs || !m_lightClusterEnabled || m_resources == nullptr ||
        !m_clusterTexture.valid() || m_lightListTextureCapacity == 0) {
        return false;
    }

    if (!m_gpuLightListBufferActive) {
        if (m_clusterUniforms.depthParams.w <= 0.5f) {
            return false;
        }
        m_vfxClusterUniforms = m_clusterUniforms;
        m_vfxLightListParams = m_passUniforms.lightListParams;
        m_vfxLightListParams.w = 1.0f;
        m_vfxLightListParams2 = m_passUniforms.lightListParams2;
        m_vfxLightScreenSize = m_passUniforms.lightScreenSize;
        m_vfxClusterReady = true;
        if (m_frameIndex == 0) {
            HBG_LOGI("VFX lighting: lit outputs use the cluster texture light list");
        }
        return true;
    }

    const ViewConstants& constants = m_viewState.constants();
    m_lightListBuilder.build(m_lightBuilder.data(), constants.viewProj, constants.view, m_camera.nearClip,
                             m_camera.farClip, m_viewState.renderWidth(), m_viewState.renderHeight(),
                             m_lightListTextureCapacity);
    const GpuLightListData& list = m_lightListBuilder.data();
    const GpuLightClusterData& cluster = m_lightListBuilder.clusterData();
    const gfx::Texture* texture = m_resources->texture(m_clusterTexture);
    if (!cluster.enabled || cluster.offsets.empty() || texture == nullptr) {
        return false;
    }

    const uint32_t offsetCount = static_cast<uint32_t>(cluster.offsets.size());
    const uint32_t flagCount = static_cast<uint32_t>(cluster.tileCountX) * cluster.tileCountY;
    const uint32_t combinedCount = offsetCount + cluster.indexCapacity + flagCount;
    const uint16_t width = texture->desc().width;
    const uint16_t height = static_cast<uint16_t>((combinedCount + width - 1u) / width);
    if (width == 0 || height == 0 || height > texture->desc().height) {
        return false;
    }

    std::vector<uint32_t> words(static_cast<std::size_t>(width) * height, 0u);
    std::copy(cluster.offsets.begin(), cluster.offsets.end(), words.begin());
    std::copy(cluster.indices.begin(), cluster.indices.end(), words.begin() + offsetCount);
    if (cluster.tileFeatureFlags.size() == flagCount) {
        std::copy(cluster.tileFeatureFlags.begin(), cluster.tileFeatureFlags.end(),
                  words.begin() + offsetCount + cluster.indexCapacity);
    }
    if (!m_resources->updateTexture(m_clusterTexture, words.data(),
                                    static_cast<uint32_t>(words.size() * sizeof(uint32_t)), width, height,
                                    static_cast<uint16_t>(width * sizeof(uint32_t)))) {
        return false;
    }

    const uint32_t capacity = list.capacity != 0 ? list.capacity : m_lightListTextureCapacity;
    m_vfxLightListParams =
        Vec4(static_cast<float>(kLightTileSize), static_cast<float>(list.tileCountX),
             static_cast<float>(list.tileCountY), 1.0f);
    m_vfxLightListParams2 = lightListCapacityParams(capacity);
    m_vfxLightListParams2.z = m_passUniforms.lightListParams2.z;
    m_vfxLightListParams2.w = m_passUniforms.lightListParams2.w;
    m_vfxLightScreenSize =
        Vec4(static_cast<float>(m_viewState.renderWidth()), static_cast<float>(m_viewState.renderHeight()),
             1.0f / static_cast<float>(m_viewState.renderWidth()),
             1.0f / static_cast<float>(m_viewState.renderHeight()));
    m_vfxClusterUniforms = {};
    m_vfxClusterUniforms.params = Vec4(static_cast<float>(kClusterTileSize),
                                       static_cast<float>(cluster.tileCountX),
                                       static_cast<float>(cluster.tileCountY),
                                       static_cast<float>(kClusterSliceCount));
    m_vfxClusterUniforms.depthParams =
        Vec4(m_camera.nearClip, m_camera.farClip, LightListBuilder::clusterLogBase(), 1.0f);
    m_vfxClusterUniforms.lightCounts = m_passUniforms.lightCounts;
    m_vfxClusterUniforms.screenSize = m_vfxLightScreenSize;
    m_vfxClusterUniforms.lightListParams2 =
        Vec4(static_cast<float>(lightListStrideFor(capacity)),
             static_cast<float>(cluster.capacity), static_cast<float>(capacity), static_cast<float>(capacity));
    m_vfxClusterUniforms.params2 =
        Vec4(static_cast<float>(width), static_cast<float>(offsetCount),
             static_cast<float>(cluster.indexCapacity), static_cast<float>(kLightCategoryCount));
    m_vfxClusterReady = true;
    if (m_frameIndex == 0) {
        HBG_LOGI("VFX lighting: lit outputs use a CPU cluster-texture mirror of the GPU light list");
    }
    return true;
}

::vfx::FrameContext ForwardRenderer::makeVfxFrameContext(const RenderGraphContext& context,
                                                          const FramePassResources& resources) const {
    const ViewConstants& constants = m_viewState.constants();
    ::vfx::FrameContext frame;
    frame.view = toVfxMatrix(constants.view);
    frame.projection = toVfxMatrix(constants.proj);
    frame.viewProjection = toVfxMatrix(constants.viewProj);
    frame.prevViewProjection = toVfxMatrix(constants.prevViewProj);
    frame.cameraPosition = toVfxVec3(constants.worldSpaceCameraPos);
    frame.cameraForward = toVfxVec3(m_cameraForward);
    frame.cameraRight = toVfxVec3(transformDirection(m_camera.world, Vec3(1.0f, 0.0f, 0.0f)));
    frame.cameraUp = toVfxVec3(transformDirection(m_camera.world, Vec3(0.0f, 1.0f, 0.0f)));
    frame.screenSize = ::vfx::Vec2{static_cast<float>(m_viewState.renderWidth()),
                                 static_cast<float>(m_viewState.renderHeight())};
    frame.time = m_globalUniforms.time.x;
    frame.deltaTime = m_vfxDeltaSeconds;
    frame.frameIndex = m_viewState.frameIndex();
    if (m_vfxBackend != nullptr && resources.depth.valid()) {
        frame.sceneDepth = m_vfxBackend->importTexture(context.texture(resources.depth));
    }
    return frame;
}

void ForwardRenderer::addVfxSimulationPass(RenderGraph& graph) {
    if (m_vfxRuntime == nullptr || !m_vfxActive) {
        return;
    }
    RenderGraphBuilder pass = graph.addComputePass("VFX Simulation");
    pass.allowCulling(false);
    pass.setRenderFunc([this](RenderGraphContext& context) {
        m_vfxBackend->begin(context.encoder(), context.viewId());
        const FramePassResources emptyResources;
        const ::vfx::FrameContext frame = makeVfxFrameContext(context, emptyResources);
        m_vfxRuntime->encodeSimulation(*m_vfxBackend, frame);
        m_vfxBackend->end();
    });
}

void ForwardRenderer::addVfxOutputPass(RenderGraph& graph, const FramePassResources& resources) {
    if (m_vfxRuntime == nullptr || !m_vfxActive || !resources.color.valid() || !resources.depth.valid()) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("VFX Output");
    pass.setColorAttachment(resources.color);
    pass.readTexture(resources.depth);
    pass.setDepthAttachment(resources.depth, 0, true);
    if (resources.lightData.valid()) {
        pass.readTexture(resources.lightData);
    }
    if (m_vfxClusterReady && resources.clusterLightList.valid()) {
        pass.readTexture(resources.clusterLightList);
    }
    if (resources.directionalShadow.valid()) {
        pass.readTexture(resources.directionalShadow);
    }
    if (resources.punctualShadow.valid()) {
        pass.readTexture(resources.punctualShadow);
    }
    if (resources.shadowContext.valid()) {
        pass.readBuffer(resources.shadowContext);
    }
    if (resources.environment.valid()) {
        pass.readTexture(resources.environment);
    }
    if (!m_deferredActive && resources.contactShadow.valid()) {
        pass.readTexture(resources.contactShadow);
    }
    pass.allowCulling(false);
    pass.setRenderFunc([this, resources](RenderGraphContext& context) {
        context.encoder().setViewRect(context.viewId(), 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
        context.encoder().setViewFrameBuffer(context.viewId(), context.frameBuffer());
        m_vfxBackend->begin(context.encoder(), context.viewId());
        ::vfx::FrameContext frame = makeVfxFrameContext(context, resources);
        gfx::generated::ObjectUniforms objectUniforms = m_objectUniforms;
        objectUniforms.lightLayerMask = Vec4(65535.0f, 0.0f, 0.0f, 0.0f);
        const bool contactShadows = !m_deferredActive && contactShadowsActive();
        const gfx::TextureHandle contactShadow =
            resources.contactShadow.valid() ? context.texture(resources.contactShadow) : gfx::TextureHandle{};
        ForwardLightingContext lighting = makeForwardLightingContext(
            objectUniforms, contactShadow, m_vfxClusterReady, contactShadows, false);
        lighting.passUniforms.lightColor.w = contactShadows ? 1.0f : 0.0f;
        if (m_vfxClusterReady) {
            lighting.lightListBuffer = {};
            lighting.lightListTexture = m_clusterTexture;
            lighting.gpuLightList = false;
            lighting.passUniforms.lightListParams = m_vfxLightListParams;
            lighting.passUniforms.lightListParams2 = m_vfxLightListParams2;
            lighting.passUniforms.lightScreenSize = m_vfxLightScreenSize;
            lighting.clusterUniforms = m_vfxClusterUniforms;
            lighting.clusterUniforms.depthParams.w = 1.0f;
            lighting.clusterUniforms.lightCounts = m_passUniforms.lightCounts;
        } else {
            lighting.passUniforms.lightListParams.w = 0.0f;
            lighting.clusterUniforms.depthParams.w = 0.0f;
        }
        frame.lighting = &lighting;
        m_vfxRuntime->encodeOutput(*m_vfxBackend, frame);
        m_vfxBackend->end();
    });
}

void ForwardRenderer::recordForwardFrame(RenderGraph& graph, FramePassResources& resources,
                                         const RenderScene& scene, DebugDisplayMode debugMode, bool materialDebug) {
    m_bakedGIActive = false;
    m_passUniforms.lightmapParams2.w = 0.0f;
    if (debugMode == DebugDisplayMode::Lightmap) {
        addDebugLightmapPass(graph, resources.color, resources.depth, scene);
        return;
    }
    if (materialDebug) {
        addDebugMaterialPass(graph, resources.color, resources.depth, scene, debugMode);
        return;
    }
    addSkyPass(graph, resources.color, resources.skyEnvironment);
    if (debugMode == DebugDisplayMode::None) {
        addVfxSimulationPass(graph);
    }
    resources.contactShadow = addContactShadowPass(graph, resources, false);
    m_bakedGIActive = !m_deferredActive && m_depthPrepassActive && m_lightmapSet.valid() &&
                      !m_bakedGIDrawList.empty() && bakedGIProgramsReady();
    m_passUniforms.lightmapParams2.w = m_bakedGIActive ? 1.0f : 0.0f;
    addBakedGIDrawPass(graph, resources, scene, m_bakedGIDrawList);
    addForwardPass(graph, resources.color, resources.depth, resources.lightData, resources.lightList,
                   resources.clusterLightList, resources.lightFptlBuffer, resources.lightClusterBuffer,
                   resources.directionalShadow, resources.punctualShadow, resources.areaShadow,
                   resources.shadowContext, resources.contactShadow, scene, false);
    if (debugMode == DebugDisplayMode::None) {
        addVfxOutputPass(graph, resources);
    }
    addForwardPass(graph, resources.color, resources.depth, resources.lightData, resources.lightList,
                   resources.clusterLightList, resources.lightFptlBuffer, resources.lightClusterBuffer,
                   resources.directionalShadow, resources.punctualShadow, resources.areaShadow,
                   resources.shadowContext, resources.contactShadow, scene, true);
    addBakedGIForwardCompositePass(graph, resources);
    addWireframePass(graph, resources.color, resources.depth, scene);
}

void ForwardRenderer::recordDeferredFrame(RenderGraph& graph, FramePassResources& resources,
                                          const RenderScene& scene, DebugDisplayMode debugMode, bool materialDebug) {
    m_bakedGIActive = false;
    m_passUniforms.lightmapParams2.w = 0.0f;
    if (debugMode == DebugDisplayMode::Lightmap) {
        addDebugLightmapPass(graph, resources.color, resources.depth, scene);
        return;
    }
    if (isGbufferDebug(debugMode)) {
        addDecalPasses(graph, resources, scene);
        addGBufferClearPass(graph, resources.gbuffer, resources.depth, resources.fullscreen);
        addGBufferPass(graph, resources, scene);
        addDebugGBufferPass(graph, resources, debugMode);
        return;
    }
    if (materialDebug) {
        addDebugMaterialPass(graph, resources.color, resources.depth, scene, debugMode);
        return;
    }
    m_bakedGIActive = debugMode == DebugDisplayMode::None && m_depthPrepassActive &&
                      m_lightmapSet.valid() && !m_bakedGIDrawList.empty() && bakedGIProgramsReady();
    m_litVariantTileActive = false;
    const bool d3dAdvancedTile =
        bgfx::getRendererType() == bgfx::RendererType::Direct3D11 && m_advancedMaterialActive;
    if (d3dAdvancedTile && m_litVariantTilesEnabled && m_frameIndex == 0 && !m_litVariantTileWarningLogged) {
        HBG_LOGW("D3D11 advanced lit-variant tiles use no-opt compute shaders; "
                 "falling back to the raster stencil path");
        m_litVariantTileWarningLogged = true;
    }
    if (m_litVariantTilesEnabled && m_litVariantsActive && m_gpuLightListBufferActive && !d3dAdvancedTile &&
        debugMode == DebugDisplayMode::None && resources.litTileFeatureFlags.valid() &&
        resources.litIndirectArgs.valid()) {
        bool programsValid =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LitMaterialFlags, 0, 0}).valid() &&
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LitVariantIndirectClear, 0, 0}).valid() &&
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::LitVariantIndirect, 0, 0}).valid();
        for (uint32_t variant = 0; programsValid && variant < kLitVariantCount; ++variant) {
            programsValid =
                m_shaderLibrary
                    .program(shader::ProgramKey{shader::PassId::DeferredLightingVariantCompute, variant,
                                                shader::kLightListBufferProgramKey})
                    .valid();
        }
        m_litVariantTileActive = programsValid;
        if (!programsValid && m_frameIndex == 0 && !m_litVariantTileWarningLogged) {
            HBG_LOGW("lit variant tiles requested but unavailable (compute programs); "
                     "falling back to the raster stencil path");
            m_litVariantTileWarningLogged = true;
        }
    }
    m_passUniforms.litVariantParams =
        Vec4(static_cast<float>(m_litLightFlags), static_cast<float>(m_litTileListBase),
             m_litVariantTileActive ? 1.0f : 0.0f, static_cast<float>(kLightTileSize));

    addSkyPass(graph, resources.color, resources.skyEnvironment);
    if (debugMode == DebugDisplayMode::None) {
        addVfxSimulationPass(graph);
    }
    addDecalPasses(graph, resources, scene);
    addGBufferClearPass(graph, resources.gbuffer, resources.depth, resources.fullscreen);
    addGBufferPass(graph, resources, scene);
    addBakedGIDrawPass(graph, resources, scene, m_gbufferDrawList);
    addContactShadowPass(graph, resources, true);
    resources.contactShadow = resources.gbuffer[1];
    addLitVariantTilePasses(graph, resources);
    addSSAOPasses(graph, resources);
    addDeferredLightingPass(graph, resources);
    addSSGIPasses(graph, resources);
    addBakedGICompensatePass(graph, resources);
    addSSRPasses(graph, resources);
    addForwardPass(graph, resources.color, resources.depth, resources.lightData, resources.lightList,
                   resources.clusterLightList, resources.lightFptlBuffer, resources.lightClusterBuffer,
                   resources.directionalShadow, resources.punctualShadow, resources.areaShadow,
                   resources.shadowContext, resources.contactShadow, scene, false, true);
    if (debugMode == DebugDisplayMode::None) {
        addVfxOutputPass(graph, resources);
    }
    addForwardPass(graph, resources.color, resources.depth, resources.lightData, resources.lightList,
                   resources.clusterLightList, resources.lightFptlBuffer, resources.lightClusterBuffer,
                   resources.directionalShadow, resources.punctualShadow, resources.areaShadow,
                   resources.shadowContext, resources.contactShadow, scene, true);
    addWireframePass(graph, resources.color, resources.depth, scene);
}

void ForwardRenderer::renderBakedGIDraws(gfx::Encoder& encoder, bgfx::ViewId view, const DrawList& list,
                                         const RenderScene& scene) {
    if (m_resources == nullptr || !m_lightmapSet.valid() || list.empty() || !m_lightmapSampler.valid()) {
        return;
    }

    m_globalBinder.upload(m_globalUniforms);
    m_passBinder.upload(m_passUniforms);
    m_objectUniforms.lightLayerMask = Vec4(static_cast<float>(kRenderingLayersMask), 0.0f, 0.0f, 0.0f);

    const std::vector<GpuMesh>& meshes = m_materialSystem.meshes();
    const std::vector<GpuMaterial>& materials = m_materialSystem.materials();
    for (const DrawBatch& batch : list.batches()) {
        if (batch.firstItem + batch.itemCount > list.items().size() ||
            batch.meshIndex >= meshes.size() || batch.materialIndex >= materials.size() ||
            batch.materialIndex >= scene.materials.size()) {
            continue;
        }

        const GpuMesh& mesh = meshes[batch.meshIndex];
        const GpuMaterial& material = materials[batch.materialIndex];
        const asset::Material& sourceMaterial = scene.materials[batch.materialIndex];
        if (sourceMaterial.type != asset::MaterialType::Lit) {
            continue;
        }
        const gfx::ProgramHandle program =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::BakedGI, material.variantKey, 0});
        if (!program.valid() || !mesh.vertexBuffer.valid() || !mesh.indexBuffer.valid()) {
            continue;
        }

        encoder.setState(bakedGIState(sourceMaterial));
        m_materialBinder.upload(material.uniforms);
        encoder.setTexture(1, m_materialSamplers.baseColorMap, material.baseColorTexture);
        encoder.setTexture(2, m_materialSamplers.normalMap, material.normalTexture);

        for (uint32_t index = 0; index < batch.itemCount; ++index) {
            const DrawItem& item = list.items()[batch.firstItem + index];
            const ObjectLightmapUniforms lightmap = objectLightmapUniforms(scene, item.objectIndex);
            if (lightmap.params.x < 0.5f) {
                continue;
            }
            m_objectUniforms.lightmapST = lightmap.st;
            m_objectUniforms.lightmapParams = lightmap.params;
            m_objectBinder.upload(m_objectUniforms);
            encoder.setTexture(15, m_lightmapSampler, m_lightmapSet.shArray(), BGFX_SAMPLER_UVW_CLAMP);
            encoder.setTransform(item.world.data());
            encoder.setVertexBuffer(0, mesh.vertexBuffer);
            encoder.setIndexBuffer(mesh.indexBuffer, item.indexOffset, item.indexCount);
            encoder.submit(view, program);
        }
    }
}

bool ForwardRenderer::bakedGIProgramsReady() {
    if (!m_lightmapSampler.valid()) {
        return false;
    }
    for (const DrawItem& item : m_bakedGIDrawList.items()) {
        if (item.materialIndex >= m_materialSystem.materials().size() ||
            !m_shaderLibrary
                 .program(shader::ProgramKey{shader::PassId::BakedGI,
                                             m_materialSystem.materials()[item.materialIndex].variantKey, 0})
                 .valid()) {
            return false;
        }
    }
    return true;
}

void ForwardRenderer::renderBakedGIPass(gfx::Encoder& encoder, bgfx::ViewId view,
                                        gfx::FrameBufferHandle frameBuffer, const DrawList& list,
                                        const RenderScene& scene) {
    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewTransform(view, m_viewState.constants().view.data(), m_viewState.constants().proj.data());
    encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
    encoder.touch(view);
    renderBakedGIDraws(encoder, view, list, scene);
}

void ForwardRenderer::renderBakedGICompensatePass(gfx::Encoder& encoder, bgfx::ViewId view,
                                                  gfx::FrameBufferHandle frameBuffer,
                                                  gfx::TextureHandle bakedIndirect,
                                                  gfx::TextureHandle depthPyramid,
                                                  gfx::TextureHandle ssao,
                                                  const std::array<gfx::TextureHandle, 5>& gbuffer) {
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::BakedGICompensate, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !bakedIndirect.valid() ||
        !depthPyramid.valid() || !std::all_of(gbuffer.begin(), gbuffer.end(),
                                              [](gfx::TextureHandle texture) { return texture.valid(); })) {
        return;
    }

    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    encoder.setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ADD | BGFX_STATE_MSAA);

    m_globalBinder.upload(m_globalUniforms);
    m_passBinder.upload(m_passUniforms);
    m_environmentBinder.upload(m_environmentUniforms);
    m_ssaoBinder.upload(m_ssaoUniforms);
    m_objectUniforms.lightLayerMask =
        Vec4(static_cast<float>(kRenderingLayersMask), 127.0f, 0.0f, m_deferredLightLayersActive ? 1.0f : 0.0f);
    m_objectBinder.upload(m_objectUniforms);

    for (std::size_t index = 0; index < 4; ++index) {
        encoder.setTexture(static_cast<uint8_t>(index), m_gbufferSamplers[index], gbuffer[index],
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    }
    encoder.setTexture(4, m_depthPyramidSampler, depthPyramid,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(11, m_ssaoSampler, ssao.valid() ? ssao : m_ssaoFallback,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(12, m_gbufferSamplers[4], gbuffer[4],
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(13, m_bakedIndirectSampler, bakedIndirect,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderBakedGIForwardCompositePass(gfx::Encoder& encoder, bgfx::ViewId view,
                                                        gfx::FrameBufferHandle frameBuffer,
                                                        gfx::TextureHandle bakedIndirect) {
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::BakedGIForwardComposite, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !bakedIndirect.valid()) {
        return;
    }

    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    encoder.setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ADD | BGFX_STATE_MSAA);
    encoder.setTexture(13, m_bakedIndirectSampler, bakedIndirect,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::addBakedGIDrawPass(RenderGraph& graph, const FramePassResources& resources,
                                         const RenderScene& scene, const DrawList& list) {
    if (!m_bakedGIActive || !resources.bakedIndirect.valid() || !resources.depth.valid()) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("Baked GI");
    pass.setColorAttachment(resources.bakedIndirect);
    pass.readTexture(resources.depth);
    pass.setDepthAttachment(resources.depth);
    pass.allowCulling(false);
    pass.setRenderFunc([this, &scene, &list](RenderGraphContext& context) {
        renderBakedGIPass(context.encoder(), context.viewId(), context.frameBuffer(), list, scene);
    });
}

void ForwardRenderer::addBakedGICompensatePass(RenderGraph& graph, const FramePassResources& resources) {
    if (!m_bakedGIActive || !resources.bakedIndirect.valid() || !resources.color.valid() ||
        !resources.depthPyramid.valid() || !resources.fullscreen.valid() ||
        !std::all_of(resources.gbuffer.begin(), resources.gbuffer.end(),
                     [](RenderGraphTextureHandle texture) { return texture.valid(); })) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("Baked GI Compensate");
    pass.setColorAttachment(resources.color);
    pass.readTexture(resources.bakedIndirect);
    pass.readTexture(resources.depthPyramid);
    for (RenderGraphTextureHandle gbuffer : resources.gbuffer) {
        pass.readTexture(gbuffer);
    }
    if (resources.ssao.valid()) {
        pass.readTexture(resources.ssao);
    }
    pass.readBuffer(resources.fullscreen);
    pass.allowCulling(false);
    pass.setRenderFunc([this, resources](RenderGraphContext& context) {
        const std::array<gfx::TextureHandle, 5> gbuffer = {
            context.texture(resources.gbuffer[0]),
            context.texture(resources.gbuffer[1]),
            context.texture(resources.gbuffer[2]),
            context.texture(resources.gbuffer[3]),
            context.texture(resources.gbuffer[4]),
        };
        renderBakedGICompensatePass(context.encoder(), context.viewId(), context.frameBuffer(),
                                    context.texture(resources.bakedIndirect),
                                    context.texture(resources.depthPyramid),
                                    resources.ssao.valid() ? context.texture(resources.ssao) : gfx::TextureHandle{},
                                    gbuffer);
    });
}

void ForwardRenderer::addBakedGIForwardCompositePass(RenderGraph& graph,
                                                     const FramePassResources& resources) {
    if (!m_bakedGIActive || !resources.bakedIndirect.valid() || !resources.color.valid() ||
        !resources.fullscreen.valid()) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("Baked GI Forward Composite");
    pass.setColorAttachment(resources.color);
    pass.readTexture(resources.bakedIndirect);
    pass.readBuffer(resources.fullscreen);
    pass.allowCulling(false);
    pass.setRenderFunc([this, resources](RenderGraphContext& context) {
        renderBakedGIForwardCompositePass(context.encoder(), context.viewId(), context.frameBuffer(),
                                          context.texture(resources.bakedIndirect));
    });
}

void ForwardRenderer::addForwardPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                                     RenderGraphTextureHandle lightData, RenderGraphTextureHandle lightList,
                                     RenderGraphTextureHandle clusterLightList,
                                     RenderGraphBufferHandle lightFptlBuffer,
                                     RenderGraphBufferHandle clusterLightListBuffer,
                                     RenderGraphTextureHandle directionalShadow,
                                     RenderGraphTextureHandle punctualShadow, RenderGraphTextureHandle areaShadow,
                                     RenderGraphBufferHandle shadowContext, RenderGraphTextureHandle contactShadow,
                                     const RenderScene& scene, bool transparent, bool forwardOnly) {
    const FrameSettingsField visibility =
        transparent ? FrameSettingsField::TransparentObjects : FrameSettingsField::OpaqueObjects;
    const DrawList& list = forwardOnly ? m_forwardOpaqueDrawList : forwardDrawList(transparent);
    if (!m_frameSettings.isEnabled(visibility) || list.empty()) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass(
        transparent ? "Forward Transparent" : (forwardOnly ? "Forward Only Opaque" : "Forward Opaque"));
    pass.setColorAttachment(color);
    pass.readTexture(depth);
    pass.setDepthAttachment(depth);
    pass.readTexture(lightData);
    if (m_gpuLightListBufferActive && lightFptlBuffer.valid()) {
        pass.readBuffer(lightFptlBuffer);
        if (clusterLightListBuffer.valid()) {
            pass.readBuffer(clusterLightListBuffer);
        }
    } else {
        if (lightList.valid()) {
            pass.readTexture(lightList);
        }
        if (clusterLightList.valid()) {
            pass.readTexture(clusterLightList);
        }
    }
    if (directionalShadow.valid()) {
        pass.readTexture(directionalShadow);
    }
    if (punctualShadow.valid()) {
        pass.readTexture(punctualShadow);
    }
    if (areaShadow.valid() && m_shadowManager.areaRequestCount() > 0) {
        pass.readTexture(areaShadow);
    }
    if (shadowContext.valid()) {
        pass.readBuffer(shadowContext);
    }
    if (contactShadow.valid()) {
        pass.readTexture(contactShadow);
    }
    pass.setRenderFunc([this, &scene, transparent, forwardOnly, contactShadow](RenderGraphContext& context) {
        renderForwardPass(context.encoder(), context.viewId(), context.frameBuffer(), scene, transparent,
                          forwardOnly, context.texture(contactShadow));
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

void ForwardRenderer::addDebugLightmapPass(RenderGraph& graph, RenderGraphTextureHandle color,
                                           RenderGraphTextureHandle depth, const RenderScene& scene) {
    if (!color.valid() || !depth.valid()) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("Debug Lightmap");
    pass.setColorAttachment(color);
    pass.readTexture(depth);
    pass.setDepthAttachment(depth);
    pass.allowCulling(false);
    pass.setRenderFunc([this, &scene](RenderGraphContext& context) {
        m_passUniforms.lightmapParams2.w = 0.0f;
        renderBakedGIPass(context.encoder(), context.viewId(), context.frameBuffer(), m_bakedGIDrawList,
                          scene);
    });
}

void ForwardRenderer::addDebugGBufferPass(RenderGraph& graph, const FramePassResources& resources,
                                          DebugDisplayMode debugMode) {
    if (!isGbufferDebug(debugMode) || !resources.color.valid() || !resources.depth.valid() ||
        !resources.fullscreen.valid() ||
        !std::all_of(resources.gbuffer.begin(), resources.gbuffer.end(),
                     [](RenderGraphTextureHandle texture) { return texture.valid(); })) {
        return;
    }
    RenderGraphBuilder pass = graph.addRasterPass("Debug GBuffer");
    pass.setColorAttachment(resources.color);
    pass.readTexture(resources.depth);
    for (RenderGraphTextureHandle gbuffer : resources.gbuffer) {
        pass.readTexture(gbuffer);
    }
    pass.readBuffer(resources.fullscreen);
    pass.allowCulling(false);
    pass.setRenderFunc([this, resources, debugMode](RenderGraphContext& context) {
        const std::array<gfx::TextureHandle, 5> gbuffer = {
            context.texture(resources.gbuffer[0]),
            context.texture(resources.gbuffer[1]),
            context.texture(resources.gbuffer[2]),
            context.texture(resources.gbuffer[3]),
            context.texture(resources.gbuffer[4]),
        };
        renderDebugGBufferPass(context.encoder(), context.viewId(), context.frameBuffer(), gbuffer,
                               context.texture(resources.depth), debugMode);
    });
}

void ForwardRenderer::addPresentationPass(RenderGraph& graph, const FramePassResources& resources,
                                          RenderGraphTextureHandle presentationColor, DebugDisplayMode debugMode,
                                          RenderGraphTextureHandle bloomColor, RenderGraphTextureHandle taaHistory,
                                          RenderGraphTextureHandle dofCoc) {
    const bool lightDataDebug = debugMode == DebugDisplayMode::LightData;
    const bool normalDebug = debugMode == DebugDisplayMode::Normal;
    const bool motionDebug = debugMode == DebugDisplayMode::MotionVectors;
    const bool depthDebug = debugMode == DebugDisplayMode::Depth;
    const bool pyramidDebug = debugMode == DebugDisplayMode::DepthPyramid;
    const bool clusterDebug = debugMode == DebugDisplayMode::LightCluster;
    const bool directionalAtlasDebug = debugMode == DebugDisplayMode::ShadowAtlasDirectional;
    const bool punctualAtlasDebug = debugMode == DebugDisplayMode::ShadowAtlasPunctual;
    const bool shadowCascadeDebug = debugMode == DebugDisplayMode::ShadowCascade;
    const bool shadowFilterDebug = debugMode == DebugDisplayMode::ShadowFilter;
    const bool ssaoDebug = debugMode == DebugDisplayMode::SSAO;
    const bool ssaoRawDebug = debugMode == DebugDisplayMode::SSAORaw;
    const bool ssaoHistoryDebug = debugMode == DebugDisplayMode::SSAOHistory;
    const bool ssrDebug = debugMode == DebugDisplayMode::SSR;
    const bool ssrRawDebug = debugMode == DebugDisplayMode::SSRRaw;
    const bool ssrHistoryDebug = debugMode == DebugDisplayMode::SSRHistory;
    const bool ssgiDebug = debugMode == DebugDisplayMode::SSGI;
    const bool ssgiRawDebug = debugMode == DebugDisplayMode::SSGIRaw;
    const bool ssgiHistoryDebug = debugMode == DebugDisplayMode::SSGIHistory;
    const bool ssaoAnyDebug = ssaoDebug || ssaoRawDebug || ssaoHistoryDebug;
    const bool ssrAnyDebug = ssrDebug || ssrRawDebug || ssrHistoryDebug;
    const bool ssgiAnyDebug = ssgiDebug || ssgiRawDebug || ssgiHistoryDebug;
    const RenderGraphTextureHandle ssaoSource =
        ssaoRawDebug ? resources.ssaoRaw : ssaoHistoryDebug ? resources.ssaoHistoryPrevious : resources.ssao;
    const RenderGraphTextureHandle ssrSource =
        ssrRawDebug ? resources.ssrRaw : ssrHistoryDebug ? resources.ssrHistoryPrevious : resources.ssr;
    const RenderGraphTextureHandle ssgiSource =
        ssgiRawDebug ? resources.ssgiRaw : ssgiHistoryDebug ? resources.ssgiHistoryPrevious : resources.ssgi;
    const bool contactDebug = debugMode == DebugDisplayMode::ContactShadows;
    const bool decalDebug = debugMode == DebugDisplayMode::Decals;
    const bool exposureDebug = debugMode == DebugDisplayMode::Exposure;
    const bool colorGradingDebug = debugMode == DebugDisplayMode::ColorGrading;
    const bool bloomDebug = debugMode == DebugDisplayMode::Bloom;
    const bool taaHistoryDebug = debugMode == DebugDisplayMode::TaaHistory;
    const bool dofDebug = debugMode == DebugDisplayMode::Dof;
    const bool fxaaDebug = debugMode == DebugDisplayMode::Fxaa;
    const bool lightmapDebug = debugMode == DebugDisplayMode::Lightmap;
    const RenderGraphTextureHandle colorGradingLut =
        colorGradingDebug && m_postProcessChain.colorGradingLutTexture().valid()
            ? graph.importTexture(m_postProcessChain.colorGradingLutTexture(), "Debug Color Grading LUT")
            : RenderGraphTextureHandle{};
    const RenderGraphTextureHandle exposureValue =
        exposureDebug && m_postProcessChain.exposureTexture().valid()
            ? graph.importTexture(m_postProcessChain.exposureTexture(), "Debug Exposure Value")
            : RenderGraphTextureHandle{};
    const RenderGraphTextureHandle exposureHistogram =
        exposureDebug && m_postProcessChain.histogramTexture().valid()
            ? graph.importTexture(m_postProcessChain.histogramTexture(), "Debug Exposure Histogram")
            : RenderGraphTextureHandle{};
    const bool gbufferDebug = isGbufferDebug(debugMode) && resources.gbuffer[0].valid();
    const bool materialDebug = isMaterialDebug(debugMode) && !gbufferDebug;
    const bool needsNormalSource = normalDebug || shadowCascadeDebug || shadowFilterDebug;
    const RenderGraphTextureHandle normalSource =
        needsNormalSource && !resources.normal.valid() && resources.gbuffer[2].valid() ? resources.gbuffer[2]
                                                                                       : resources.normal;
    const char* passName = lightDataDebug          ? "Debug Light Data"
                           : normalDebug           ? "Debug Normal Buffer"
                           : motionDebug           ? "Debug Motion Vectors"
                           : depthDebug            ? "Debug Depth"
                           : pyramidDebug          ? "Debug Depth Pyramid"
                           : clusterDebug          ? "Debug Light Cluster"
                           : directionalAtlasDebug ? "Debug Directional Shadow Atlas"
                           : punctualAtlasDebug    ? "Debug Punctual Shadow Atlas"
                           : shadowCascadeDebug    ? "Debug Shadow Cascade"
                           : shadowFilterDebug     ? "Debug Shadow Filter"
                           : ssaoDebug             ? "Debug SSAO"
                           : ssaoRawDebug          ? "Debug SSAO Raw"
                           : ssaoHistoryDebug      ? "Debug SSAO History"
                           : ssrDebug              ? "Debug SSR"
                           : ssrRawDebug           ? "Debug SSR Raw"
                           : ssrHistoryDebug       ? "Debug SSR History"
                           : ssgiDebug             ? "Debug SSGI"
                           : ssgiRawDebug          ? "Debug SSGI Raw"
                           : ssgiHistoryDebug      ? "Debug SSGI History"
                           : contactDebug          ? "Debug Contact Shadows"
                           : decalDebug            ? "Debug Decals"
                           : exposureDebug         ? "Debug Exposure"
                           : colorGradingDebug     ? "Debug Color Grading"
                           : bloomDebug            ? "Debug Bloom"
                           : taaHistoryDebug       ? "Debug TAA History"
                           : dofDebug              ? "Debug Depth of Field CoC"
                           : fxaaDebug             ? "Debug FXAA"
                           : lightmapDebug         ? "Debug Lightmap"
                           : gbufferDebug          ? "Present Debug GBuffer"
                           : materialDebug         ? "Debug Material"
                                                   : "Tonemap";
    RenderGraphBuilder pass = graph.addRasterPass(passName);
    if (lightDataDebug) {
        pass.readTexture(resources.lightData);
    } else if (normalDebug) {
        pass.readTexture(normalSource);
    } else if (motionDebug) {
        pass.readTexture(resources.motionVector);
    } else if (depthDebug) {
        pass.readTexture(resources.depth);
    } else if (pyramidDebug) {
        pass.readTexture(resources.depthPyramid);
    } else if (clusterDebug) {
        pass.readTexture(resources.lightData);
        if (m_gpuLightListBufferActive) {
            if (resources.lightFptlBuffer.valid()) {
                pass.readBuffer(resources.lightFptlBuffer);
            }
            if (resources.lightClusterBuffer.valid()) {
                pass.readBuffer(resources.lightClusterBuffer);
            }
        } else {
            pass.readTexture(resources.lightList);
            if (resources.clusterLightList.valid()) {
                pass.readTexture(resources.clusterLightList);
            }
        }
    } else if (directionalAtlasDebug) {
        pass.readTexture(resources.directionalShadow);
    } else if (punctualAtlasDebug) {
        pass.readTexture(resources.punctualShadow);
    } else if (shadowCascadeDebug || shadowFilterDebug) {
        pass.readTexture(normalSource);
        pass.readTexture(resources.depth);
        pass.readTexture(resources.directionalShadow);
        pass.readBuffer(resources.shadowContext);
    } else if (ssaoAnyDebug) {
        pass.readTexture(ssaoSource);
    } else if (ssrAnyDebug) {
        pass.readTexture(ssrSource);
    } else if (ssgiAnyDebug) {
        pass.readTexture(ssgiSource);
        if (ssgiDebug) {
            pass.readTexture(resources.depthPyramid);
        }
    } else if (contactDebug) {
        if (resources.contactShadow.valid()) {
            pass.readTexture(resources.contactShadow);
        }
    } else if (decalDebug) {
        pass.readTexture(resources.decal[0]);
    } else if (exposureDebug) {
        if (exposureValue.valid()) {
            pass.readTexture(exposureValue);
        }
        if (exposureHistogram.valid()) {
            pass.readTexture(exposureHistogram);
        }
    } else if (colorGradingDebug) {
        if (colorGradingLut.valid()) {
            pass.readTexture(colorGradingLut);
        }
    } else if (bloomDebug) {
        if (bloomColor.valid()) {
            pass.readTexture(bloomColor);
        }
    } else if (taaHistoryDebug) {
        if (taaHistory.valid()) {
            pass.readTexture(taaHistory);
        }
    } else if (dofDebug) {
        if (dofCoc.valid()) {
            pass.readTexture(dofCoc);
        }
    } else if (fxaaDebug) {
        pass.readTexture(presentationColor);
    } else if (lightmapDebug) {
        pass.readTexture(resources.color);
    } else {
        pass.readTexture(presentationColor);
    }
    pass.readBuffer(resources.fullscreen);
    pass.writeBackbuffer();
    pass.setRenderFunc([this, resources, normalSource, presentationColor, colorGradingLut, exposureValue,
                        exposureHistogram, bloomColor, taaHistory, dofCoc, ssaoSource, ssrSource, ssgiSource,
                        debugMode](RenderGraphContext& context) {
        renderPresentation(context.encoder(), context.viewId(), context.texture(presentationColor),
                           context.texture(resources.depth), context.texture(resources.lightData),
                           context.texture(resources.lightList), context.texture(normalSource),
                           context.texture(resources.motionVector), context.texture(resources.depthPyramid),
                           context.texture(resources.directionalShadow), context.texture(resources.punctualShadow),
                           context.buffer(resources.shadowContext), context.texture(resources.contactShadow),
                           context.texture(ssaoSource), context.texture(ssrSource), context.texture(ssgiSource),
                           context.texture(resources.decal[0]),
                            context.buffer(resources.fullscreen), debugMode, context.texture(exposureValue),
                            context.texture(exposureHistogram), context.texture(colorGradingLut),
                            context.texture(bloomColor), context.texture(taaHistory), context.texture(dofCoc));
    });
}

bool ForwardRenderer::drawShadowBatch(gfx::Encoder& encoder, bgfx::ViewId view, const DrawList& list,
                                      const DrawBatch& batch, const RenderScene& scene) {
    if (batch.itemCount < 2 || batch.firstItem + batch.itemCount > list.items().size()) {
        return false;
    }
    const DrawItem& first = list.items()[batch.firstItem];
    if (first.meshIndex >= m_materialSystem.meshes().size() ||
        first.materialIndex >= m_materialSystem.materials().size() || first.materialIndex >= scene.materials.size() ||
        first.indexCount == 0) {
        return false;
    }
    const GpuMesh& mesh = m_materialSystem.meshes()[first.meshIndex];
    if (!mesh.vertexBuffer.valid() || !mesh.indexBuffer.valid()) {
        return false;
    }
    const GpuMaterial& gpuMaterial = m_materialSystem.materials()[first.materialIndex];
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::ShadowInstanced, gpuMaterial.variantKey, 0});
    if (!program.valid() || gfx::InstanceData::available(batch.itemCount, kShadowInstanceStride) < batch.itemCount) {
        return false;
    }

    gfx::InstanceData instances;
    if (!instances.allocate(batch.itemCount, kShadowInstanceStride)) {
        return false;
    }
    float* data = instances.floats();
    for (uint32_t index = 0; index < batch.itemCount; ++index) {
        const DrawItem& item = list.items()[batch.firstItem + index];
        std::memcpy(data + static_cast<std::size_t>(index) * 16, item.world.data(), 16 * sizeof(float));
    }

    encoder.setState(shadowState(scene.materials[first.materialIndex]));
    m_passBinder.upload(m_passUniforms);
    m_materialBinder.upload(gpuMaterial.uniforms);
    encoder.setTexture(0, m_materialSamplers.baseColorMap, gpuMaterial.baseColorTexture);
    encoder.setVertexBuffer(0, mesh.vertexBuffer);
    encoder.setIndexBuffer(mesh.indexBuffer, first.indexOffset, first.indexCount);
    encoder.setInstanceDataBuffer(instances);
    encoder.submit(view, program);
    return true;
}

bool ForwardRenderer::renderGpuShadowPass(gfx::Encoder& encoder, bgfx::ViewId view,
                                          gfx::FrameBufferHandle frameBuffer, const RenderScene& scene,
                                          const ShadowRequest& request, uint32_t requestIndex) {
    if (!m_shadowGpuIndirectActive || requestIndex >= m_shadowGpuRequestCount ||
        m_shadowGpuCasterCount == 0 || m_shadowGpuBatchCount == 0) {
        return false;
    }
    const std::vector<DrawItem>& items = m_shadowDrawList.items();
    const std::vector<DrawBatch>& batches = m_shadowDrawList.batches();
    if (items.empty() || batches.empty()) {
        return false;
    }

    for (const DrawBatch& batch : batches) {
        if (batch.itemCount == 0 || batch.firstItem + batch.itemCount > items.size() ||
            batch.firstItem + batch.itemCount > m_shadowGpuCasterCount) {
            return false;
        }
        const DrawItem& first = items[batch.firstItem];
        if (first.meshIndex >= m_materialSystem.meshes().size() ||
            first.materialIndex >= m_materialSystem.materials().size() ||
            first.materialIndex >= scene.materials.size() || first.indexCount == 0) {
            return false;
        }
        const GpuMesh& mesh = m_materialSystem.meshes()[first.meshIndex];
        const GpuMaterial& gpuMaterial = m_materialSystem.materials()[first.materialIndex];
        if (!mesh.vertexBuffer.valid() || !mesh.indexBuffer.valid() ||
            !m_shaderLibrary
                 .program(shader::ProgramKey{shader::PassId::ShadowInstanced, gpuMaterial.variantKey, 0})
                 .valid()) {
            return false;
        }
    }

    m_passUniforms.shadowMatrix = request.worldToShadow;
    m_passUniforms.shadowViewProj = request.viewProj;
    m_shadowManager.beginPass(encoder, view, frameBuffer, request);
    for (std::size_t batchIndex = 0; batchIndex < batches.size(); ++batchIndex) {
        const DrawBatch& batch = batches[batchIndex];
        const DrawItem& first = items[batch.firstItem];
        const GpuMesh& mesh = m_materialSystem.meshes()[first.meshIndex];
        const GpuMaterial& gpuMaterial = m_materialSystem.materials()[first.materialIndex];
        const asset::Material& sourceMaterial = scene.materials[first.materialIndex];
        const gfx::ProgramHandle program =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::ShadowInstanced, gpuMaterial.variantKey, 0});
        encoder.setState(shadowState(sourceMaterial));
        m_passBinder.upload(m_passUniforms);
        m_materialBinder.upload(gpuMaterial.uniforms);
        encoder.setTexture(0, m_materialSamplers.baseColorMap, gpuMaterial.baseColorTexture);
        encoder.setVertexBuffer(0, mesh.vertexBuffer);
        encoder.setIndexBuffer(mesh.indexBuffer);
        const uint32_t instanceStart = requestIndex * m_shadowGpuCasterCount + batch.firstItem;
        encoder.setInstanceDataBuffer(m_shadowInstanceBuffer, instanceStart, batch.itemCount);
        const uint32_t record = requestIndex * m_shadowGpuBatchCount + static_cast<uint32_t>(batchIndex);
        encoder.submitIndirect(view, program, m_shadowIndirectBuffer, record, 1);
        ++m_shadowIndirectDraws;
    }
    return true;
}

bool ForwardRenderer::drawForwardBatch(gfx::Encoder& encoder, bgfx::ViewId view, const DrawList& list,
                                       const DrawBatch& batch, const RenderScene& scene,
                                       gfx::TextureHandle contactShadow, bool clusterList) {
    if (m_advancedMaterialActive || m_pipelineSettings.supportLightLayers || batch.itemCount < 2 ||
        batch.firstItem + batch.itemCount > list.items().size()) {
        return false;
    }
    const DrawItem& first = list.items()[batch.firstItem];
    if (first.transparent || first.meshIndex >= m_materialSystem.meshes().size() ||
        first.materialIndex >= m_materialSystem.materials().size() || first.materialIndex >= scene.materials.size() ||
        first.indexCount == 0 || batch.lightmapAtlas != first.lightmapAtlas) {
        return false;
    }
    const GpuMesh& mesh = m_materialSystem.meshes()[first.meshIndex];
    const GpuMaterial& material = m_materialSystem.materials()[first.materialIndex];
    const asset::Material& sourceMaterial = scene.materials[first.materialIndex];
    if (!mesh.vertexBuffer.valid() || !mesh.indexBuffer.valid()) {
        return false;
    }
    const gfx::ProgramHandle program = lightListProgram(shader::PassId::ForwardInstanced, material.variantKey);
    if (!program.valid() || gfx::InstanceData::available(batch.itemCount, kForwardInstanceStride) < batch.itemCount) {
        return false;
    }

    gfx::InstanceData instances;
    if (!instances.allocate(batch.itemCount, kForwardInstanceStride)) {
        return false;
    }
    float* data = instances.floats();
    for (uint32_t index = 0; index < batch.itemCount; ++index) {
        const DrawItem& item = list.items()[batch.firstItem + index];
        const ObjectLightmapUniforms lightmap = objectLightmapUniforms(scene, item.objectIndex);
        float* instance = data + static_cast<std::size_t>(index) * kForwardInstanceFloats;
        std::memcpy(instance, item.world.data(), 16 * sizeof(float));
        instance[16] = lightmap.st.x;
        instance[17] = lightmap.st.y;
        instance[18] = lightmap.st.z;
        instance[19] = lightmap.st.w;
    }

    encoder.setState(forwardState(sourceMaterial, false, m_depthPrepassActive));
    m_objectUniforms.lightLayerMask = Vec4(static_cast<float>(kRenderingLayersMask), 0.0f, 0.0f, 0.0f);
    const ObjectLightmapUniforms lightmap = objectLightmapUniforms(scene, first.objectIndex);
    m_objectUniforms.lightmapST = lightmap.st;
    m_objectUniforms.lightmapParams = lightmap.params;
    ForwardLightingContext lighting =
        makeForwardLightingContext(m_objectUniforms, contactShadow, clusterList, contactShadowsActive());
    lighting.passUniforms.lightColor.w = contactShadowsActive() ? 1.0f : 0.0f;
    lighting.clusterUniforms.depthParams.w = clusterList ? 1.0f : 0.0f;
    bindForwardLighting(encoder, lighting);
    m_materialBinder.upload(material.uniforms);
    encoder.setTexture(0, m_materialSamplers.baseColorMap, material.baseColorTexture);
    encoder.setTexture(1, m_materialSamplers.metallicRoughnessMap, material.metallicRoughnessTexture);
    encoder.setTexture(2, m_materialSamplers.normalMap, material.normalTexture);
    encoder.setTexture(3, m_materialSamplers.occlusionMap, material.occlusionTexture);
    encoder.setTexture(4, m_materialSamplers.emissiveMap, material.emissiveTexture);
    if (m_shadowManager.activeDirectionalTexture().valid()) {
        bindShadowMomentSat(encoder, m_shadowManager.activeDirectionalTexture());
    }
    const gfx::TextureHandle areaDepth = m_shadowManager.areaRequestCount() > 0
                                            ? m_shadowManager.activeAreaSamplingTexture()
                                            : m_shadowManager.activePunctualTexture();
    if (areaDepth.valid()) {
        const uint32_t flags = m_shadowManager.areaEvsmBlurActive()
                                   ? BGFX_SAMPLER_UVW_CLAMP
                                   : (BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        encoder.setTexture(13, m_shadowPointPunctualSampler, areaDepth, flags);
    }
    encoder.setVertexBuffer(0, mesh.vertexBuffer);
    encoder.setIndexBuffer(mesh.indexBuffer, first.indexOffset, first.indexCount);
    encoder.setInstanceDataBuffer(instances);
    encoder.submit(view, program);
    return true;
}

void ForwardRenderer::renderShadowPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                       const RenderScene& scene, uint32_t requestIndex, const DrawList& drawList,
                                       bool clearDepth) {
    if (!shadowsActive() || !m_shadowManager.valid() || requestIndex >= m_shadowManager.requests().size()) {
        return;
    }
    const gfx::ProgramHandle shadowProgram = m_shaderLibrary.program(shader::ProgramKey{shader::PassId::Shadow, 0, 0});
    if (!shadowProgram.valid()) {
        return;
    }

    const ShadowRequest& request = m_shadowManager.requests()[requestIndex];
    if (m_shadowGpuCullingActive && m_shadowGpuIndirectActive && clearDepth &&
        renderGpuShadowPass(encoder, view, frameBuffer, scene, request, requestIndex)) {
        return;
    }
    m_passUniforms.shadowMatrix = request.worldToShadow;
    m_passUniforms.shadowViewProj = request.viewProj;
    m_shadowManager.beginPass(encoder, view, frameBuffer, request, clearDepth);

    DrawList shadowList;
    shadowList.buildCulled(drawList, request.frustumPlanes);
    m_shadowDrawnItems += static_cast<uint32_t>(shadowList.size());
    m_shadowCulledItems += shadowList.culledCount();
    const std::vector<GpuMesh>& meshes = m_materialSystem.meshes();
    for (const DrawBatch& batch : shadowList.batches()) {
        if (drawShadowBatch(encoder, view, shadowList, batch, scene)) {
            continue;
        }
        for (uint32_t index = 0; index < batch.itemCount; ++index) {
            const DrawItem& item = shadowList.items()[batch.firstItem + index];
            if (item.meshIndex >= meshes.size() || item.materialIndex >= m_materialSystem.materials().size()) {
                continue;
            }
            const GpuMesh& mesh = meshes[item.meshIndex];
            const GpuMaterial& gpuMaterial = m_materialSystem.materials()[item.materialIndex];
            const asset::Material& material = scene.materials[item.materialIndex];
            const gfx::ProgramHandle itemProgram =
                m_shaderLibrary.program(shader::ProgramKey{shader::PassId::Shadow, gpuMaterial.variantKey, 0});
            if (!itemProgram.valid()) {
                continue;
            }
            encoder.setState(shadowState(material));
            m_passBinder.upload(m_passUniforms);
            m_materialBinder.upload(gpuMaterial.uniforms);
            encoder.setTexture(0, m_materialSamplers.baseColorMap, gpuMaterial.baseColorTexture);
            encoder.setTransform(item.world.data());
            encoder.setVertexBuffer(0, mesh.vertexBuffer);
            encoder.setIndexBuffer(mesh.indexBuffer, item.indexOffset, item.indexCount);
            encoder.submit(view, itemProgram);
        }
    }
}

void ForwardRenderer::renderGBufferClear(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                         bool useShader) {
    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    const uint16_t clearFlags = useShader
                                    ? static_cast<uint16_t>(BGFX_CLEAR_STENCIL | (m_deferredLightLayersActive ? BGFX_CLEAR_COLOR : 0))
                                    : (BGFX_CLEAR_COLOR | BGFX_CLEAR_STENCIL);
    encoder.setViewClear(view, clearFlags, 0x000000ff, 1.0f, 0);
    encoder.touch(view);
    if (useShader) {
        encoder.setState(gfx::state::kNoDepth);
    }
}

void ForwardRenderer::renderDecalClear(gfx::Encoder& encoder, bgfx::ViewId view,
                                       gfx::FrameBufferHandle frameBuffer) {
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DecalClear, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid()) {
        return;
    }
    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    encoder.touch(view);
    encoder.setState(gfx::state::kNoDepth);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::updateDecalUniforms(const Decal& decal) {
    m_decalUniforms.worldToDecal = decalWorldToDecal(decal);
    m_decalUniforms.decalToWorld = decalToWorld(decal);
    uint32_t flags = 0;
    if (decal.albedoImage >= 0) {
        flags |= 1u;
    }
    if (decal.normalImage >= 0) {
        flags |= 2u;
    }
    if (decal.maskImage >= 0) {
        flags |= 4u;
    }
    m_decalUniforms.params = Vec4(decal.opacity, decal.angleFade, decal.normalBlend, static_cast<float>(flags));
    m_decalUniforms.params2 = Vec4(decal.metallic, decal.ambientOcclusion, decal.roughness, 0.0f);
    m_decalUniforms.params3 = Vec4(static_cast<float>(decal.layerMask), static_cast<float>(decal.channelMask), 0.0f, 0.0f);
    m_decalUniforms.baseColor = decal.baseColor;
}

void ForwardRenderer::renderDecalPass(gfx::Encoder& encoder, bgfx::ViewId view,
                                      gfx::FrameBufferHandle frameBuffer, const RenderScene& scene,
                                      std::size_t decalIndex, gfx::TextureHandle depthPyramid) {
    if (decalIndex >= scene.decals.size()) {
        return;
    }
    const Decal& decal = scene.decals[decalIndex];
    const gfx::ProgramHandle program = m_shaderLibrary.program(shader::ProgramKey{shader::PassId::Decal, 0, 0});
    if (!decal.enabled || !program.valid() || !m_fullscreenVertexBuffer.valid() || !m_resources) {
        return;
    }

    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    encoder.touch(view);
    encoder.setState(gfx::state::kNoDepth);
    m_globalBinder.upload(m_globalUniforms);
    updateDecalUniforms(decal);
    m_decalBinder.upload(m_decalUniforms);

    const std::vector<GpuDecalTextures>& textures = m_materialSystem.decals();
    GpuDecalTextures gpuTextures;
    if (decalIndex < textures.size()) {
        gpuTextures = textures[decalIndex];
    }
    const auto bind = [&](uint8_t stage, gfx::UniformHandle sampler, gfx::TextureHandle texture,
                          gfx::TextureHandle fallback) {
        const gfx::TextureHandle source = texture.valid() ? texture : fallback;
        if (source.valid()) {
            encoder.setTexture(stage, sampler, source, BGFX_SAMPLER_UVW_CLAMP);
        }
    };
    bind(0, m_decalAlbedoSampler, gpuTextures.albedo, m_decalFallbackTextures[0]);
    bind(1, m_decalNormalSampler, gpuTextures.normal, m_decalFallbackTextures[1]);
    bind(2, m_decalMaskSampler, gpuTextures.mask, m_decalFallbackTextures[2]);
    if (depthPyramid.valid()) {
        encoder.setTexture(3, m_depthSourceSampler, depthPyramid,
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    }
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderSSAOPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                     uint16_t width, uint16_t height, gfx::TextureHandle depthPyramid,
                                     gfx::TextureHandle normalBuffer) {
    const gfx::ProgramHandle program = m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSAO, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !depthPyramid.valid() || !normalBuffer.valid() ||
        width == 0 || height == 0) {
        return;
    }

    encoder.setViewRect(view, 0, 0, width, height);
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0xffffffff, 1.0f, 0);
    encoder.touch(view);
    encoder.setState(gfx::state::kNoDepth);
    m_globalBinder.upload(m_globalUniforms);
    m_ssaoBinder.upload(m_ssaoUniforms);
    encoder.setTexture(2, m_gbufferSamplers[2], normalBuffer,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(4, m_depthPyramidSampler, depthPyramid,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderSSAOBlurPass(gfx::Encoder& encoder, bgfx::ViewId view,
                                         gfx::FrameBufferHandle frameBuffer, uint16_t width, uint16_t height,
                                         gfx::TextureHandle ssaoInput, gfx::TextureHandle depthPyramid,
                                         gfx::TextureHandle normalBuffer) {
    const gfx::ProgramHandle program = m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSAOBlur, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !ssaoInput.valid() || !depthPyramid.valid() ||
        !normalBuffer.valid() || width == 0 || height == 0) {
        return;
    }

    encoder.setViewRect(view, 0, 0, width, height);
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0xffffffff, 1.0f, 0);
    encoder.touch(view);
    encoder.setState(gfx::state::kNoDepth);
    m_globalBinder.upload(m_globalUniforms);
    m_ssaoBinder.upload(m_ssaoUniforms);
    encoder.setTexture(0, m_ssaoInputSampler, ssaoInput,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(2, m_gbufferSamplers[2], normalBuffer,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(4, m_depthPyramidSampler, depthPyramid,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderSSAOTemporalPass(gfx::Encoder& encoder, bgfx::ViewId view,
                                             gfx::FrameBufferHandle frameBuffer, uint16_t width, uint16_t height,
                                             gfx::TextureHandle ssaoInput, gfx::TextureHandle depthPyramid,
                                             gfx::TextureHandle motionVectors, gfx::TextureHandle history,
                                             bool historyValid) {
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSAOTemporal, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !ssaoInput.valid() || !depthPyramid.valid() ||
        !motionVectors.valid() || !history.valid() || width == 0 || height == 0) {
        return;
    }

    encoder.setViewRect(view, 0, 0, width, height);
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0xffffffff, 1.0f, 0);
    encoder.touch(view);
    encoder.setState(gfx::state::kNoDepth);
    m_globalBinder.upload(m_globalUniforms);
    m_ssaoUniforms.params3 =
        Vec4(historyValid ? 1.0f : 0.0f, 0.90f, 32.0f, 0.0f);
    m_ssaoBinder.upload(m_ssaoUniforms);
    encoder.setTexture(0, m_ssaoInputSampler, ssaoInput,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(1, m_motionVectorSampler, motionVectors,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(2, m_ssaoHistorySampler, history,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(4, m_depthPyramidSampler, depthPyramid,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderSSAOUpsamplePass(gfx::Encoder& encoder, bgfx::ViewId view,
                                             gfx::FrameBufferHandle frameBuffer, uint16_t width, uint16_t height,
                                             gfx::TextureHandle ssaoInput, gfx::TextureHandle depthPyramid,
                                             gfx::TextureHandle normalBuffer) {
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSAOUpsample, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !ssaoInput.valid() || !depthPyramid.valid() ||
        !normalBuffer.valid() || width == 0 || height == 0) {
        return;
    }

    encoder.setViewRect(view, 0, 0, width, height);
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0xffffffff, 1.0f, 0);
    encoder.touch(view);
    encoder.setState(gfx::state::kNoDepth);
    m_globalBinder.upload(m_globalUniforms);
    m_ssaoBinder.upload(m_ssaoUniforms);
    encoder.setTexture(0, m_ssaoInputSampler, ssaoInput,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(2, m_gbufferSamplers[2], normalBuffer,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(4, m_depthPyramidSampler, depthPyramid,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderSSRTracePass(gfx::Encoder& encoder, bgfx::ViewId view,
                                         gfx::FrameBufferHandle frameBuffer, uint16_t width, uint16_t height,
                                         gfx::TextureHandle sceneColor, gfx::TextureHandle depthPyramid,
                                         gfx::TextureHandle gbuffer0, gfx::TextureHandle gbuffer1,
                                         gfx::TextureHandle gbuffer2, gfx::TextureHandle environment,
                                         gfx::TextureHandle ssao) {
    const gfx::ProgramHandle program = m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSR, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !sceneColor.valid() || !depthPyramid.valid() ||
        !gbuffer0.valid() || !gbuffer1.valid() || width == 0 || height == 0) {
        return;
    }

    encoder.setViewRect(view, 0, 0, width, height);
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
    encoder.touch(view);
    encoder.setState(gfx::state::kNoDepth);
    encoder.setStencil(gfx::state::kDeferredLightingStencil, gfx::state::kDeferredLightingStencil);
    m_globalBinder.upload(m_globalUniforms);
    m_ssrBinder.upload(m_ssrUniforms);
    m_environmentBinder.upload(m_environmentUniforms);
    m_ssaoBinder.upload(m_ssaoUniforms);
    encoder.setTexture(0, m_gbufferSamplers[0], gbuffer0, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(1, m_gbufferSamplers[1], gbuffer1, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(2, m_gbufferSamplers[2], gbuffer2, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(4, m_depthPyramidSampler, depthPyramid, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(10, m_environmentSampler, environment.valid() ? environment : m_environmentFallback,
                       environment.valid() ? BGFX_SAMPLER_V_CLAMP : BGFX_SAMPLER_UVW_CLAMP);
    encoder.setTexture(11, m_ssaoSampler, ssao.valid() ? ssao : m_ssaoFallback,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(12, m_hdrSampler, sceneColor, BGFX_SAMPLER_UVW_CLAMP);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderSSRTemporalPass(gfx::Encoder& encoder, bgfx::ViewId view,
                                            gfx::FrameBufferHandle frameBuffer, uint16_t width, uint16_t height,
                                            gfx::TextureHandle ssrInput, gfx::TextureHandle depthPyramid,
                                            gfx::TextureHandle motionVectors, gfx::TextureHandle history,
                                            bool historyValid) {
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSRTemporal, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !ssrInput.valid() || !depthPyramid.valid() ||
        !motionVectors.valid() || !history.valid() || width == 0 || height == 0) {
        return;
    }

    encoder.setViewRect(view, 0, 0, width, height);
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
    encoder.touch(view);
    encoder.setState(gfx::state::kNoDepth);
    m_globalBinder.upload(m_globalUniforms);
    m_ssrUniforms.params3.x = historyValid ? 1.0f : 0.0f;
    m_ssrBinder.upload(m_ssrUniforms);
    encoder.setTexture(0, m_ssrSampler, ssrInput, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(1, m_motionVectorSampler, motionVectors,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(2, m_ssrHistorySampler, history,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(4, m_depthPyramidSampler, depthPyramid,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderSSRCompositePass(gfx::Encoder& encoder, bgfx::ViewId view,
                                             gfx::FrameBufferHandle frameBuffer, uint16_t width, uint16_t height,
                                             gfx::TextureHandle ssr) {
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSRComposite, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !ssr.valid() || width == 0 || height == 0) {
        return;
    }

    encoder.setViewRect(view, 0, 0, width, height);
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_NONE, 0x00000000, 1.0f, 0);
    encoder.touch(view);
    encoder.setStencil(gfx::state::kDeferredLightingStencil, gfx::state::kDeferredLightingStencil);
    encoder.setState(gfx::state::kAdditiveNoDepth);
    encoder.setTexture(0, m_ssrSampler, ssr, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderSSGITracePass(gfx::Encoder& encoder, bgfx::ViewId view,
                                         gfx::FrameBufferHandle frameBuffer, uint16_t width, uint16_t height,
                                         gfx::TextureHandle sceneColor, gfx::TextureHandle depthPyramid,
                                         gfx::TextureHandle gbuffer2, gfx::TextureHandle environment) {
    const gfx::ProgramHandle program = m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSGI, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !sceneColor.valid() || !depthPyramid.valid() ||
        !gbuffer2.valid() || width == 0 || height == 0) {
        return;
    }

    encoder.setViewRect(view, 0, 0, width, height);
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
    encoder.touch(view);
    encoder.setState(gfx::state::kNoDepth);
    m_globalBinder.upload(m_globalUniforms);
    m_passBinder.upload(m_passUniforms);
    m_ssgiBinder.upload(m_ssgiUniforms);
    m_environmentBinder.upload(m_environmentUniforms);
    encoder.setTexture(2, m_gbufferSamplers[2], gbuffer2, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(4, m_depthPyramidSampler, depthPyramid, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(10, m_environmentSampler, environment.valid() ? environment : m_environmentFallback,
                       environment.valid() ? BGFX_SAMPLER_V_CLAMP : BGFX_SAMPLER_UVW_CLAMP);
    encoder.setTexture(12, m_hdrSampler, sceneColor, BGFX_SAMPLER_UVW_CLAMP);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderSSGIDenoisePass(gfx::Encoder& encoder, bgfx::ViewId view,
                                            gfx::FrameBufferHandle frameBuffer, uint16_t width, uint16_t height,
                                            gfx::TextureHandle ssgiRaw, gfx::TextureHandle depthPyramid) {
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSGIDenoise, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !ssgiRaw.valid() || !depthPyramid.valid() ||
        width == 0 || height == 0) {
        return;
    }

    encoder.setViewRect(view, 0, 0, width, height);
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
    encoder.touch(view);
    encoder.setState(gfx::state::kNoDepth);
    m_globalBinder.upload(m_globalUniforms);
    m_ssgiBinder.upload(m_ssgiUniforms);
    encoder.setTexture(0, m_ssgiRawSampler, ssgiRaw, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(4, m_depthPyramidSampler, depthPyramid, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderSSGITemporalPass(gfx::Encoder& encoder, bgfx::ViewId view,
                                             gfx::FrameBufferHandle frameBuffer, uint16_t width, uint16_t height,
                                             gfx::TextureHandle ssgiRaw, gfx::TextureHandle depthPyramid,
                                             gfx::TextureHandle motionVectors, gfx::TextureHandle history,
                                             gfx::TextureHandle historyDepth, gfx::TextureHandle historyNormal,
                                             gfx::TextureHandle currentNormal, bool historyValid) {
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSGITemporal, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !ssgiRaw.valid() || !depthPyramid.valid() ||
        !motionVectors.valid() || !history.valid() || !historyDepth.valid() || !historyNormal.valid() ||
        !currentNormal.valid() || width == 0 || height == 0) {
        return;
    }

    encoder.setViewRect(view, 0, 0, width, height);
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
    encoder.touch(view);
    encoder.setState(gfx::state::kNoDepth);
    m_globalBinder.upload(m_globalUniforms);
    m_ssgiUniforms.params4.x = historyValid ? 1.0f : 0.0f;
    m_ssgiBinder.upload(m_ssgiUniforms);
    encoder.setTexture(0, m_ssgiRawSampler, ssgiRaw, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(1, m_motionVectorSampler, motionVectors, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(2, m_gbufferSamplers[2], currentNormal, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(3, m_ssgiHistorySampler, history, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(4, m_depthPyramidSampler, depthPyramid, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(5, m_ssgiHistoryDepthSampler, historyDepth,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(6, m_ssgiHistoryNormalSampler, historyNormal,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderSSGICompositePass(gfx::Encoder& encoder, bgfx::ViewId view,
                                               gfx::FrameBufferHandle frameBuffer, uint16_t width, uint16_t height,
                                               gfx::TextureHandle ssgi, gfx::TextureHandle depthPyramid,
                                               gfx::TextureHandle gbuffer0, gfx::TextureHandle gbuffer1,
                                               gfx::TextureHandle ssao, gfx::TextureHandle bakedIndirect) {
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSGIComposite, 0, 0});
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !ssgi.valid() || !depthPyramid.valid() ||
        !gbuffer0.valid() || !gbuffer1.valid() || width == 0 || height == 0) {
        return;
    }

    encoder.setViewRect(view, 0, 0, width, height);
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_NONE, 0x00000000, 1.0f, 0);
    encoder.touch(view);
    encoder.setStencil(gfx::state::kDeferredLightingStencil, gfx::state::kDeferredLightingStencil);
    encoder.setState(gfx::state::kAdditiveNoDepth);
    m_globalBinder.upload(m_globalUniforms);
    m_ssgiBinder.upload(m_ssgiUniforms);
    encoder.setTexture(0, m_ssgiSampler, ssgi, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(1, m_gbufferSamplers[0], gbuffer0, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(2, m_gbufferSamplers[1], gbuffer1, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(4, m_depthPyramidSampler, depthPyramid, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(11, m_ssaoSampler, ssao.valid() ? ssao : m_ssaoFallback,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(13, m_bakedIndirectSampler,
                       bakedIndirect.valid() ? bakedIndirect : m_bakedIndirectFallback,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderContactShadowPass(gfx::Encoder& encoder, bgfx::ViewId view,
                                                  gfx::TextureHandle output, gfx::TextureHandle depth,
                                                  gfx::TextureHandle lightData, gfx::TextureHandle lightList,
                                                  bool gbufferTarget, bool halfResolution, uint16_t width,
                                                  uint16_t height) {
    const gfx::ProgramHandle program = lightListProgram(shader::PassId::ContactShadows);
    const bool listValid =
        m_gpuLightListBufferActive ? m_activeLightListBuffer.valid() : lightList.valid();
    if (!program.valid() || !output.valid() || !depth.valid() || !lightData.valid() || !listValid ||
        width == 0 || height == 0) {
        return;
    }

    gfx::generated::ContactUniforms contactUniforms = m_contactUniforms;
    contactUniforms.params3.w = halfResolution ? 1.0f : 0.0f;
    contactUniforms.params4.x = gbufferTarget ? 1.0f : 0.0f;
    contactUniforms.params4.y = 0.0f;

    encoder.setViewRect(view, 0, 0, width, height);
    encoder.setViewFrameBuffer(view, {});
    m_globalBinder.upload(m_globalUniforms);
    m_passBinder.upload(m_passUniforms);
    m_contactBinder.upload(contactUniforms);
    encoder.setImage(0, output, 0, gbufferTarget ? bgfx::Access::ReadWrite : bgfx::Access::Write,
                     gfx::TextureFormat::RGBA8);
    encoder.setTexture(1, m_depthSourceSampler, depth, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(5, m_lightDataSampler, lightData, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    bindLightList(encoder, false);
    const uint32_t groupsX = (static_cast<uint32_t>(width) + 7u) / 8u;
    const uint32_t groupsY = (static_cast<uint32_t>(height) + 7u) / 8u;
    encoder.dispatch(view, program, groupsX, groupsY, 1);
}

void ForwardRenderer::renderDeferredLighting(gfx::Encoder& encoder, bgfx::ViewId view,
                                             gfx::FrameBufferHandle frameBuffer, gfx::TextureHandle depthPyramid,
                                             const std::array<gfx::TextureHandle, 5>& gbuffer,
                                             gfx::TextureHandle lightData, gfx::TextureHandle lightList,
                                             gfx::TextureHandle directionalShadow, gfx::TextureHandle punctualShadow,
                                             gfx::BufferHandle shadowContext, gfx::TextureHandle environment,
                                             gfx::TextureHandle ssao, bool advanced, bool advancedIms) {
    (void)lightList;
    const shader::PassId deferredPass =
        advancedIms ? shader::PassId::DeferredLightingAdvancedIms
                    : (advanced ? shader::PassId::DeferredLightingAdvanced : shader::PassId::DeferredLighting);
    const gfx::ProgramHandle program = lightListProgram(deferredPass);
    if (!program.valid() || !m_fullscreenVertexBuffer.valid() || !depthPyramid.valid() || !lightData.valid() ||
        !std::all_of(gbuffer.begin(), gbuffer.end(), [](gfx::TextureHandle texture) { return texture.valid(); })) {
        return;
    }

    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_NONE, 0x000000ff, 1.0f, 0);
    encoder.setState(gfx::state::kNoDepth);
    encoder.setStencil(gfx::state::kDeferredLightingStencil, gfx::state::kDeferredLightingStencil);

    m_globalBinder.upload(m_globalUniforms);
    m_contactBinder.upload(m_contactUniforms);
    gfx::generated::PassUniforms passUniforms = m_passUniforms;
    passUniforms.lightColor.w = contactShadowsActive() ? 1.0f : 0.0f;
    m_passBinder.upload(passUniforms);
    gfx::generated::ClusterUniforms clusterUniforms = m_clusterUniforms;
    clusterUniforms.depthParams.w = 0.0f;
    m_clusterBinder.upload(clusterUniforms);
    m_environmentBinder.upload(m_environmentUniforms);
    m_probeBinder.upload(m_probeUniforms);
    m_ssaoBinder.upload(m_ssaoUniforms);
    m_objectUniforms.lightLayerMask =
        Vec4(static_cast<float>(kRenderingLayersMask), 127.0f, 0.0f, m_deferredLightLayersActive ? 1.0f : 0.0f);
    m_objectBinder.upload(m_objectUniforms);

    const gfx::TextureHandle variantAreaDepth = m_shadowManager.areaRequestCount() > 0
                                            ? m_shadowManager.activeAreaSamplingTexture()
                                            : punctualShadow;
    const gfx::TextureHandle variantEnvironmentTexture = environment.valid() ? environment : m_environmentFallback;
    if (m_litVariantsActive) {
        auto bindVariantTextures = [&](bool imsStage6) {
            for (std::size_t index = 0; index < 4; ++index) {
                encoder.setTexture(static_cast<uint8_t>(index), m_gbufferSamplers[index], gbuffer[index],
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            }
            encoder.setTexture(4, m_depthPyramidSampler, depthPyramid,
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setTexture(5, m_lightDataSampler, lightData,
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            if (directionalShadow.valid()) {
                if (imsStage6) {
                    const gfx::TextureHandle sat = m_shadowManager.directionalMomentSatTexture();
                    if (sat.valid()) {
                        encoder.setTexture(6, m_shadowPointSampler, sat,
                                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                    }
                } else {
                    encoder.setTexture(6, m_shadowSampler,
                                       directionalShadowSamplingTexture(directionalShadow),
                                       directionalShadowSamplingFlags());
                }
            }
            bindLightList(encoder, false);
            if (shadowContext.valid()) {
                encoder.setStorageBuffer(8, shadowContext, bgfx::Access::Read);
                }
            if (punctualShadow.valid()) {
                encoder.setTexture(9, m_shadowPunctualSampler, punctualShadow,
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            }
            encoder.setTexture(10, m_environmentSampler, variantEnvironmentTexture,
                               environment.valid() ? BGFX_SAMPLER_V_CLAMP : BGFX_SAMPLER_UVW_CLAMP);
            encoder.setTexture(11, m_ssaoSampler, ssao.valid() ? ssao : m_ssaoFallback,
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            if (variantAreaDepth.valid()) {
                const uint32_t flags = m_shadowManager.areaEvsmBlurActive()
                                           ? BGFX_SAMPLER_UVW_CLAMP
                                           : (BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
                encoder.setTexture(13, m_shadowPointPunctualSampler, variantAreaDepth, flags);
            }
            bindProbeTextures(encoder);
        };

        const bool imsRequested = m_shadowManager.imsActive();
        for (uint32_t variant = 0; variant < kLitVariantCount; ++variant) {
            if ((m_usedLitVariantMask & (1u << variant)) == 0) {
                continue;
            }
            const bool variantAdvanced =
                (kLitFeatureVariantFlags[variant] & (kLitMaterialAll & ~kLitMaterialStandard)) != 0;
            gfx::ProgramHandle variantProgram;
            bool variantIms = false;
            if (imsRequested) {
                variantProgram =
                    lightListProgram(shader::PassId::DeferredLightingVariantIms, variant);
                variantIms = variantAdvanced && variantProgram.valid();
            }
            if (!variantProgram.valid()) {
                variantProgram = lightListProgram(shader::PassId::DeferredLightingVariant, variant);
                if (variantAdvanced && imsRequested && variantProgram.valid() &&
                    !m_litVariantImsWarningLogged) {
                    HBG_LOGW("advanced lit-variant IMS raster program is unavailable; "
                             "falling back to raw-depth variant");
                    m_litVariantImsWarningLogged = true;
                }
            }
            if (!variantProgram.valid()) {
                continue;
            }
            bindVariantTextures(variantIms);
            if (variantAdvanced) {
                encoder.setTexture(12, m_gbufferSamplers[4], gbuffer[4],
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            } else if (directionalShadow.valid()) {
                bindShadowMomentSat(encoder, directionalShadow);
            }
            const uint32_t stencil = litVariantStencilTest(variant);
            encoder.setStencil(stencil, stencil);
            encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
            encoder.submit(view, variantProgram);
        }
        return;
    }

    for (std::size_t index = 0; index < 4; ++index) {
        encoder.setTexture(static_cast<uint8_t>(index), m_gbufferSamplers[index], gbuffer[index],
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    }
    encoder.setTexture(4, m_depthPyramidSampler, depthPyramid, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setTexture(5, m_lightDataSampler, lightData, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    if (directionalShadow.valid()) {
        if (advancedIms) {
            const gfx::TextureHandle sat = m_shadowManager.directionalMomentSatTexture();
            if (sat.valid()) {
                encoder.setTexture(6, m_shadowPointSampler, sat,
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            }
        } else {
            encoder.setTexture(6, m_shadowSampler,
                               directionalShadowSamplingTexture(directionalShadow),
                               directionalShadowSamplingFlags());
        }
    }
    bindLightList(encoder, false);
    if (shadowContext.valid()) {
        encoder.setStorageBuffer(8, shadowContext, bgfx::Access::Read);
    }
    if (punctualShadow.valid()) {
        encoder.setTexture(9, m_shadowPunctualSampler, punctualShadow,
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    }
    if (!m_litVariantsActive) {
        if (advanced) {
            encoder.setTexture(12, m_gbufferSamplers[4], gbuffer[4],
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        } else if (directionalShadow.valid()) {
            bindShadowMomentSat(encoder, directionalShadow);
        }
    }
    const gfx::TextureHandle areaDepth = m_shadowManager.areaRequestCount() > 0
                                            ? m_shadowManager.activeAreaSamplingTexture()
                                            : punctualShadow;
    if (areaDepth.valid()) {
        const uint32_t flags = m_shadowManager.areaEvsmBlurActive()
                                   ? BGFX_SAMPLER_UVW_CLAMP
                                   : (BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        encoder.setTexture(13, m_shadowPointPunctualSampler, areaDepth, flags);
    }
    const gfx::TextureHandle environmentTexture = environment.valid() ? environment : m_environmentFallback;
    encoder.setTexture(10, m_environmentSampler, environmentTexture,
                       environment.valid() ? BGFX_SAMPLER_V_CLAMP : BGFX_SAMPLER_UVW_CLAMP);
    encoder.setTexture(11, m_ssaoSampler, ssao.valid() ? ssao : m_ssaoFallback,
                       BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    bindProbeTextures(encoder);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderPrepass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                    const RenderScene& scene, bool depthOnly) {
    if (!m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects) || m_prepassDrawList.empty()) {
        return;
    }

    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewTransform(view, m_viewState.constants().view.data(), m_viewState.constants().proj.data());
    encoder.setViewClear(view, depthOnly ? BGFX_CLEAR_NONE : BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
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

        encoder.setState(depthOnly ? prepassDepthState(sourceMaterial) : prepassState(sourceMaterial));
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
        const ObjectLightmapUniforms lightmap = objectLightmapUniforms(scene, item.objectIndex);
        m_objectUniforms.lightmapST = lightmap.st;
        m_objectUniforms.lightmapParams = lightmap.params;
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

void ForwardRenderer::renderGBufferPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                        const RenderScene& scene, bool decals) {
    if (!m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects) || m_gbufferDrawList.empty()) {
        return;
    }

    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewTransform(view, m_viewState.constants().view.data(), m_viewState.constants().proj.data());
    m_globalBinder.upload(m_globalUniforms);

    const std::vector<GpuMesh>& meshes = m_materialSystem.meshes();
    const std::vector<GpuMaterial>& materials = m_materialSystem.materials();
    for (const DrawItem& item : m_gbufferDrawList.items()) {
        if (item.meshIndex >= meshes.size() || item.materialIndex >= materials.size() ||
            item.materialIndex >= scene.materials.size()) {
            continue;
        }
        const GpuMesh& mesh = meshes[item.meshIndex];
        const GpuMaterial& gpuMaterial = materials[item.materialIndex];
        const asset::Material& sourceMaterial = scene.materials[item.materialIndex];
        if (!mesh.vertexBuffer.valid() || !mesh.indexBuffer.valid()) {
            continue;
        }

        const uint64_t pipelineKey = m_deferredLightLayersActive ? shader::kLightLayersProgramKey : 0;
        const shader::ProgramKey programKey{decals ? shader::PassId::GBufferDecals : shader::PassId::GBuffer,
                                            gpuMaterial.variantKey, pipelineKey};
        const gfx::ProgramHandle program = m_shaderLibrary.program(programKey);
        if (!program.valid()) {
            if (m_frameIndex == 0) {
                HBG_LOGW("gbuffer program invalid for material variant {}", gpuMaterial.variantKey);
            }
            continue;
        }

        encoder.setState(gbufferState(sourceMaterial));
        if (m_litVariantsActive) {
            const uint32_t variant = litVariantForMaterial(gpuMaterial.features, m_litLightFlags);
            m_usedLitVariantMask |= 1u << variant;
            const uint32_t stencil = litVariantStencilWrite(variant);
            encoder.setStencil(stencil, stencil);
        } else {
            encoder.setStencil(gfx::state::kGBufferStencil, gfx::state::kGBufferStencil);
        }
        encoder.setTransform(item.world.data());
        uint32_t objectLayerMask = kRenderingLayersMask;
        uint32_t objectDecalMask = 0x7fu;
        if (item.objectIndex < scene.objects.size()) {
            objectDecalMask = scene.objects[item.objectIndex].decalLayerMask;
            if (m_deferredLightLayersActive) {
                objectLayerMask = scene.objects[item.objectIndex].lightLayerMask;
            }
        }
        m_objectUniforms.lightLayerMask =
            Vec4(static_cast<float>(objectLayerMask), static_cast<float>(objectDecalMask), 0.0f,
                 m_deferredLightLayersActive ? 1.0f : 0.0f);
        const ObjectLightmapUniforms lightmap = objectLightmapUniforms(scene, item.objectIndex);
        m_objectUniforms.lightmapST = lightmap.st;
        m_objectUniforms.lightmapParams = lightmap.params;
        m_objectBinder.upload(m_objectUniforms);
        m_materialBinder.upload(gpuMaterial.uniforms);
        encoder.setTexture(0, m_materialSamplers.baseColorMap, gpuMaterial.baseColorTexture);
        encoder.setTexture(1, m_materialSamplers.metallicRoughnessMap, gpuMaterial.metallicRoughnessTexture);
        encoder.setTexture(2, m_materialSamplers.normalMap, gpuMaterial.normalTexture);
        encoder.setTexture(3, m_materialSamplers.occlusionMap, gpuMaterial.occlusionTexture);
        encoder.setTexture(4, m_materialSamplers.emissiveMap, gpuMaterial.emissiveTexture);
        if (decals) {
            for (std::size_t index = 0; index < m_decalTextures.size(); ++index) {
                encoder.setTexture(static_cast<uint8_t>(11 + index), m_decalSamplers[index], m_decalTextures[index],
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            }
        }
        encoder.setVertexBuffer(0, mesh.vertexBuffer);
        encoder.setIndexBuffer(mesh.indexBuffer, item.indexOffset, item.indexCount);
        encoder.submit(view, program);
    }
}

void ForwardRenderer::renderForwardPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                        const RenderScene& scene, bool transparent, bool forwardOnly,
                                        gfx::TextureHandle contactShadow) {
    const FrameSettingsField visibility =
        transparent ? FrameSettingsField::TransparentObjects : FrameSettingsField::OpaqueObjects;
    if (!m_frameSettings.isEnabled(visibility)) {
        return;
    }
    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewTransform(view, m_viewState.constants().view.data(), m_viewState.constants().proj.data());
    m_contactBinder.upload(m_contactUniforms);

    const bool clusterListAvailable =
        m_gpuLightListBufferActive ? m_lightClusterBuffer.valid() : m_clusterTexture.valid();
    const bool useCluster = m_lightClusterEnabled && (transparent || forwardOnly) && clusterListAvailable &&
                            m_clusterUniforms.depthParams.w > 0.5f;

    const auto drawItem = [&](const DrawItem& item) {
        const GpuMesh& gpuMesh = m_materialSystem.meshes()[item.meshIndex];
        const GpuMaterial& gpuMaterial = m_materialSystem.materials()[item.materialIndex];
        const asset::Material& material = scene.materials[item.materialIndex];

        const gfx::ProgramHandle program = lightListProgram(
            m_advancedMaterialActive ? shader::PassId::ForwardAdvanced : shader::PassId::Forward,
            gpuMaterial.variantKey);
        if (!program.valid()) {
            return;
        }

        encoder.setState(forwardState(material, item.transparent, m_depthPrepassActive));
        encoder.setTransform(item.world.data());
        uint32_t objectLayerMask = kRenderingLayersMask;
        if (m_pipelineSettings.supportLightLayers && item.objectIndex < scene.objects.size()) {
            objectLayerMask = scene.objects[item.objectIndex].lightLayerMask;
        }
        m_objectUniforms.lightLayerMask =
            Vec4(static_cast<float>(objectLayerMask), 0.0f, 0.0f,
                 m_pipelineSettings.supportLightLayers ? 1.0f : 0.0f);
        const ObjectLightmapUniforms lightmap = objectLightmapUniforms(scene, item.objectIndex);
        m_objectUniforms.lightmapST = lightmap.st;
        m_objectUniforms.lightmapParams = lightmap.params;
        ForwardLightingContext lighting =
            makeForwardLightingContext(m_objectUniforms, contactShadow, useCluster,
                                       contactShadowsActive() && !transparent);
        lighting.passUniforms.lightColor.w = contactShadowsActive() && !transparent ? 1.0f : 0.0f;
        lighting.clusterUniforms.depthParams.w = useCluster ? 1.0f : 0.0f;
        bindForwardLighting(encoder, lighting);
        if (m_shadowManager.activeDirectionalTexture().valid()) {
            bindShadowMomentSat(encoder, m_shadowManager.activeDirectionalTexture());
        }
        m_materialBinder.upload(gpuMaterial.uniforms);
        encoder.setTexture(0, m_materialSamplers.baseColorMap, gpuMaterial.baseColorTexture);
        encoder.setTexture(1, m_materialSamplers.metallicRoughnessMap, gpuMaterial.metallicRoughnessTexture);
        encoder.setTexture(2, m_materialSamplers.normalMap, gpuMaterial.normalTexture);
        encoder.setTexture(3, m_materialSamplers.occlusionMap, gpuMaterial.occlusionTexture);
        encoder.setTexture(4, m_materialSamplers.emissiveMap, gpuMaterial.emissiveTexture);
        const gfx::TextureHandle areaDepth = m_shadowManager.areaRequestCount() > 0
                                                ? m_shadowManager.activeAreaSamplingTexture()
                                                : m_shadowManager.activePunctualTexture();
        if (areaDepth.valid()) {
            const uint32_t flags = m_shadowManager.areaEvsmBlurActive()
                                       ? BGFX_SAMPLER_UVW_CLAMP
                                       : (BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setTexture(13, m_shadowPointPunctualSampler, areaDepth, flags);
        }

        encoder.setVertexBuffer(0, gpuMesh.vertexBuffer);
        encoder.setIndexBuffer(gpuMesh.indexBuffer, item.indexOffset, item.indexCount);
        const uint32_t depth =
            item.transparent ? static_cast<uint32_t>(transparentDepth(item.distance) & 0xffffffffu) : 0;
        encoder.submit(view, program, depth);
    };

    const DrawList& list = forwardOnly ? m_forwardOpaqueDrawList : forwardDrawList(transparent);
    if (transparent) {
        for (const DrawItem& item : list.items()) {
            drawItem(item);
        }
        return;
    }

    for (const DrawBatch& batch : list.batches()) {
        if (drawForwardBatch(encoder, view, list, batch, scene, contactShadow, useCluster)) {
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

void ForwardRenderer::renderDebugGBufferPass(gfx::Encoder& encoder, bgfx::ViewId view,
                                                gfx::FrameBufferHandle frameBuffer,
                                                const std::array<gfx::TextureHandle, 5>& gbuffer,
                                                gfx::TextureHandle depth, DebugDisplayMode debugMode) {
    if (!isGbufferDebug(debugMode) || !depth.valid() || !m_fullscreenVertexBuffer.valid() ||
        !std::all_of(gbuffer.begin(), gbuffer.end(), [](gfx::TextureHandle texture) { return texture.valid(); })) {
        return;
    }
    const gfx::ProgramHandle program =
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugGBuffer, 0, 0});
    if (!program.valid()) {
        return;
    }

    encoder.setViewRect(view, 0, 0, m_viewState.renderWidth(), m_viewState.renderHeight());
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_NONE, 0x000000ff, 1.0f, 0);
    encoder.setState(gfx::state::kNoDepth);

    m_debugUniforms.clusterParams.y = gbufferDebugMode(debugMode);
    m_debugBinder.upload(m_debugUniforms);
    m_objectUniforms.lightLayerMask = Vec4(static_cast<float>(kRenderingLayersMask), 0.0f, 0.0f, 0.0f);
    m_objectBinder.upload(m_objectUniforms);
    for (std::size_t index = 0; index < 4; ++index) {
        encoder.setTexture(static_cast<uint8_t>(index), m_gbufferSamplers[index], gbuffer[index],
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    }
    encoder.setTexture(4, m_depthDebugSampler, depth, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setVertexBuffer(0, m_fullscreenVertexBuffer);
    encoder.submit(view, program);
}

void ForwardRenderer::renderPresentation(gfx::Encoder& encoder, bgfx::ViewId view, gfx::TextureHandle color,
                                         gfx::TextureHandle depth, gfx::TextureHandle lightData,
                                         gfx::TextureHandle lightList, gfx::TextureHandle normal,
                                         gfx::TextureHandle motionVector, gfx::TextureHandle depthPyramid,
                                         gfx::TextureHandle directionalShadow, gfx::TextureHandle punctualShadow,
                                         gfx::BufferHandle shadowContext, gfx::TextureHandle contactShadow,
                                         gfx::TextureHandle ssao, gfx::TextureHandle ssr, gfx::TextureHandle ssgi,
                                         gfx::TextureHandle decal0, gfx::BufferHandle fullscreen,
                                         DebugDisplayMode debugMode,
                                         gfx::TextureHandle exposureValue, gfx::TextureHandle exposureHistogram,
                                         gfx::TextureHandle colorGradingLut, gfx::TextureHandle bloom,
                                         gfx::TextureHandle taaHistory, gfx::TextureHandle dofCoc) {
    (void)lightList;
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
        const gfx::ProgramHandle debugProgram = lightListProgram(shader::PassId::DebugLightCluster, 0);
        if (debugProgram.valid() && lightData.valid()) {
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
    case DebugDisplayMode::ShadowCascade: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugShadowCascade, 0, 0});
        if (debugProgram.valid() && directionalShadow.valid() && shadowContext.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::ShadowFilter: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugShadowFilter, 0, 0});
        if (debugProgram.valid() && directionalShadow.valid() && shadowContext.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::Lightmap: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugLightmap, 0, 0});
        if (debugProgram.valid() && color.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::Albedo:
    case DebugDisplayMode::Roughness:
    case DebugDisplayMode::Metallic:
    case DebugDisplayMode::AmbientOcclusion:
    case DebugDisplayMode::Emissive: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugColor, 0, 0});
        if (debugProgram.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::SSAO:
    case DebugDisplayMode::SSAORaw:
    case DebugDisplayMode::SSAOHistory: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugSSAO, 0, 0});
        if (debugProgram.valid() && ssao.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::SSR:
    case DebugDisplayMode::SSRRaw:
    case DebugDisplayMode::SSRHistory: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugSSR, 0, 0});
        if (debugProgram.valid() && ssr.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::SSGI: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugSSGI, 0, 0});
        if (debugProgram.valid() && ssgi.valid() && depthPyramid.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::SSGIRaw:
    case DebugDisplayMode::SSGIHistory: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugSSGIRaw, 0, 0});
        if (debugProgram.valid() && ssgi.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::ContactShadows: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugContactShadows, 0, 0});
        if (debugProgram.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::Decals: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugDecals, 0, 0});
        if (debugProgram.valid() && decal0.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::Exposure: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugExposure, 0, 0});
        if (debugProgram.valid() && exposureValue.valid() && exposureHistogram.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::ColorGrading: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugColorGrading, 0, 0});
        if (debugProgram.valid() && colorGradingLut.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::Bloom: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugBloom, 0, 0});
        if (debugProgram.valid() && bloom.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::TaaHistory: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugTaaHistory, 0, 0});
        if (debugProgram.valid() && taaHistory.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::Dof: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugDofCoc, 0, 0});
        if (debugProgram.valid() && dofCoc.valid()) {
            presentation = debugProgram;
        }
        break;
    }
    case DebugDisplayMode::Fxaa: {
        const gfx::ProgramHandle debugProgram =
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DebugFxaa, 0, 0});
        if (debugProgram.valid() && color.valid()) {
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
    if (debugMode == DebugDisplayMode::None || debugMode == DebugDisplayMode::Fxaa) {
        updatePostProcessUniforms();
        m_postProcessChain.uploadUniforms(m_postProcessUniforms);
    } else if (debugMode == DebugDisplayMode::Depth || debugMode == DebugDisplayMode::DepthPyramid ||
               debugMode == DebugDisplayMode::LightCluster) {
        m_debugBinder.upload(m_debugUniforms);
    } else if (debugMode == DebugDisplayMode::SSGI) {
        m_ssgiBinder.upload(m_ssgiUniforms);
    } else if (debugMode == DebugDisplayMode::ContactShadows) {
        gfx::generated::ContactUniforms contactUniforms = m_contactUniforms;
        contactUniforms.params4.y = m_deferredActive ? 1.0f : 0.0f;
        m_contactBinder.upload(contactUniforms);
    } else if (debugMode == DebugDisplayMode::ShadowCascade ||
               debugMode == DebugDisplayMode::ShadowFilter) {
        gfx::generated::DebugUniforms debugUniforms = m_debugUniforms;
        debugUniforms.depthParams.z = m_deferredActive ? 1.0f : 0.0f;
        m_debugBinder.upload(debugUniforms);
    } else if (debugMode == DebugDisplayMode::Lightmap) {
        gfx::generated::DebugUniforms debugUniforms = m_debugUniforms;
        debugUniforms.lightmapParams =
            Vec4(1.0f, m_debugLightmapMask ? 1.0f : 0.0f, 0.0f, 0.0f);
        m_debugBinder.upload(debugUniforms);
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
    case DebugDisplayMode::LightCluster: {
        const bool legacyFptl = m_debugLightClusterSlice == -2;
        gfx::generated::ClusterUniforms clusterUniforms = m_clusterUniforms;
        if (legacyFptl) {
            clusterUniforms.depthParams.w = 0.0f;
        }
        m_clusterBinder.upload(clusterUniforms);
        encoder.setTexture(1, m_depthSourceSampler, depth, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);

        encoder.setTexture(5, m_lightDataSampler, lightData, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        bindLightList(encoder, !legacyFptl);
        break;
    }
    case DebugDisplayMode::ShadowAtlasDirectional:
        encoder.setTexture(1, m_shadowAtlasSampler, directionalShadow, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::ShadowAtlasPunctual:
        encoder.setTexture(1, m_shadowAtlasSampler, punctualShadow, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::ShadowCascade:
    case DebugDisplayMode::ShadowFilter:
        encoder.setTexture(0, m_normalSampler, normal, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        encoder.setTexture(1, m_depthSourceSampler, depth, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        encoder.setTexture(6, m_shadowSampler,
                           directionalShadowSamplingTexture(directionalShadow),
                           directionalShadowSamplingFlags());
        encoder.setStorageBuffer(8, shadowContext, bgfx::Access::Read);
        break;
    case DebugDisplayMode::Lightmap:
        encoder.setTexture(0, m_hdrSampler, color, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::Albedo:
    case DebugDisplayMode::Roughness:
    case DebugDisplayMode::Metallic:
    case DebugDisplayMode::AmbientOcclusion:
    case DebugDisplayMode::Emissive:
        encoder.setTexture(0, m_hdrSampler, color, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::SSAO:
    case DebugDisplayMode::SSAORaw:
    case DebugDisplayMode::SSAOHistory:
        encoder.setTexture(0, m_ssaoInputSampler, ssao, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::SSR:
    case DebugDisplayMode::SSRRaw:
    case DebugDisplayMode::SSRHistory:
        encoder.setTexture(0, m_ssrSampler, ssr, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::SSGI:
        encoder.setTexture(0, m_ssgiSampler, ssgi, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        encoder.setTexture(4, m_depthPyramidSampler, depthPyramid,
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::SSGIRaw:
    case DebugDisplayMode::SSGIHistory:
        encoder.setTexture(0, m_ssgiSampler, ssgi, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::ContactShadows:
        encoder.setTexture(0, m_contactShadowSampler,
                           contactShadow.valid() ? contactShadow : m_contactShadowFallback,
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::Decals:
        encoder.setTexture(0, m_decalSamplers[0], decal0, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::Exposure:
        encoder.setTexture(0, m_postProcessChain.exposureSampler(), exposureValue,
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        encoder.setTexture(1, m_postProcessChain.histogramSampler(), exposureHistogram,
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        break;
    case DebugDisplayMode::ColorGrading:
        encoder.setTexture(1, m_postProcessChain.colorGradingLutSampler(), colorGradingLut,
                           BGFX_SAMPLER_UVW_CLAMP);
        break;
    case DebugDisplayMode::Bloom:
        encoder.setTexture(0, m_postProcessChain.bloomSampler(), bloom, BGFX_SAMPLER_UVW_CLAMP);
        break;
    case DebugDisplayMode::TaaHistory:
        encoder.setTexture(0, m_postProcessChain.taaHistorySampler(), taaHistory, BGFX_SAMPLER_UVW_CLAMP);
        break;
    case DebugDisplayMode::Dof:
        encoder.setTexture(0, m_postProcessChain.dofCocSampler(), dofCoc, BGFX_SAMPLER_UVW_CLAMP);
        break;
    case DebugDisplayMode::Fxaa:
        encoder.setTexture(0, m_postProcessChain.ldrSampler(), color, BGFX_SAMPLER_UVW_CLAMP);
        break;
    case DebugDisplayMode::None:
        encoder.setTexture(0, m_hdrSampler, color);
        break;
    }
    encoder.setVertexBuffer(0, fullscreen);
    encoder.submit(view, presentation);
}

void ForwardRenderer::render(RenderScene& scene, uint16_t width, uint16_t height, float time) {
    if (!m_valid || width == 0 || height == 0) {
        return;
    }
    if (width != m_width || height != m_height) {
        resize(width, height);
    }
    pollClusterStatsReadback();

    syncTaaJitter();
    m_shadowDrawnItems = 0;
    m_shadowCulledItems = 0;
    m_contactShadowWidth = 0;
    m_contactShadowHeight = 0;
    m_gpuLightListActive = false;
    m_gpuLightListPasses = 0;
    m_materialSystem.updateDeformedMeshes(scene);
    updateCamera(scene, width, height);
    ensureLightListTexture(m_viewState.renderWidth(), m_viewState.renderHeight());
    if (m_gpuLightListEnabled) {
        ensureLightBigTileTexture(m_viewState.renderWidth(), m_viewState.renderHeight());
    }
    ensureClusterLightListTextures(m_viewState.renderWidth(), m_viewState.renderHeight());
    if (m_gpuLightListEnabled && m_gpuLightListBufferEnabled) {
        ensureLightListBuffers(m_viewState.renderWidth(), m_viewState.renderHeight());
    }
    ensureLitVariantTileResources(m_viewState.renderWidth(), m_viewState.renderHeight());
    updateLights(scene);
    const GpuLightData& selectedLightData = m_lightBuilder.data();
    m_litLightFlags = 0;
    if (selectedLightData.directionalCount > 0) {
        m_litLightFlags |= kLitLightDirectional;
    }
    if (selectedLightData.punctualCount > 0) {
        m_litLightFlags |= kLitLightPunctual;
    }
    if (selectedLightData.areaCount > 0) {
        m_litLightFlags |= kLitLightArea;
    }
    if (m_shadowsEnabled && shadowsActive() &&
        (m_passUniforms.lightCounts.w > 0.0f || selectedLightData.shadowedCount > 0)) {
        m_litLightFlags |= kLitLightShadows;
    }
    const float deltaTime = m_hasLastRenderTime ? std::max(time - m_lastRenderTime, 0.0f) : 1.0f / 60.0f;
    m_lastRenderTime = time;
    m_hasLastRenderTime = true;
    m_globalUniforms.time = Vec4(time, deltaTime, static_cast<float>(m_frameIndex), 0.0f);
    ensureVfx(scene);
    updateVfx(scene, deltaTime);
    prepareVfxClusterLighting();
    buildDrawLists(scene);
    updateShadowGpuData(scene);
    m_depthPrepassActive =
        m_depthPrepassActive && m_shaderLibrary.program(shader::ProgramKey{shader::PassId::Prepass, 0, 0}).valid();
    if (m_frameIndex == 0) {
        HBG_LOGI("draw lists: opaque={} ({} batches, {} filtered, {} culled), forwardOnly={}, transparent={} "
                 "({} batches, {} filtered, {} culled), prepass={}, gbuffer={}, motion={}, shadow={}, "
                 "wireframe={}",
                 m_opaqueDrawList.size(), m_opaqueDrawList.batches().size(), m_opaqueDrawList.filteredCount(),
                 m_opaqueDrawList.culledCount(), m_forwardOpaqueDrawList.size(), m_transparentDrawList.size(),
                 m_transparentDrawList.batches().size(), m_transparentDrawList.filteredCount(),
                 m_transparentDrawList.culledCount(), m_prepassDrawList.size(), m_gbufferDrawList.size(),
                 m_motionVectorDrawList.size(), m_shadowDrawList.size(), m_wireframeDrawList.size());
    }

    const uint16_t renderWidth = m_viewState.renderWidth();
    const uint16_t renderHeight = m_viewState.renderHeight();
    const bool opaqueObjects = m_frameSettings.isEnabled(FrameSettingsField::OpaqueObjects);
    const bool depthPrepass = opaqueObjects && !m_prepassDrawList.empty();
    const bool motionVectors = motionVectorsActive();

    bool deferred = m_frameSettings.litShaderMode() == LitShaderMode::Deferred;
    if (deferred) {
        const char* reason = deferredUnavailableReason();
        if (reason != nullptr) {
            if (!m_deferredFallbackLogged) {
                HBG_LOGW("deferred unavailable: {}; falling back to forward", reason);
                m_deferredFallbackLogged = true;
            }
            deferred = false;
        } else if (!ensureGBufferTextures(renderWidth, renderHeight)) {
            if (!m_deferredFallbackLogged) {
                HBG_LOGW("deferred unavailable: GBuffer target creation failed; falling back to forward");
                m_deferredFallbackLogged = true;
            }
            deferred = false;
        }
    }
    m_litVariantsActive = false;
    m_usedLitVariantMask = 0;
    if (deferred && m_litVariantsEnabled) {
        bool allVariantsValid = true;
        for (uint32_t variant = 0; variant < kLitVariantCount; ++variant) {
            if (!lightListProgram(shader::PassId::DeferredLightingVariant, variant).valid()) {
                allVariantsValid = false;
                break;
            }
        }
        m_litVariantsActive = allVariantsValid;
        if (!allVariantsValid && m_frameIndex == 0 && !m_litVariantsWarningLogged) {
            HBG_LOGW("29 Lit variants requested but one or more deferred variant programs are unavailable; "
                     "falling back to the monolithic deferred path");
            m_litVariantsWarningLogged = true;
        }
    }
    if (deferred && m_frameIndex == 0) {
        HBG_LOGI("deferred foundation active: {}x{} GBuffer with {} targets (litVariants={})", renderWidth,
                 renderHeight, m_gbufferTextures.size(), m_litVariantsActive ? "true" : "false");
    }

    m_renderGraph.beginFrame(renderWidth, renderHeight, m_frameIndex);

    gfx::TextureDesc colorDesc;
    colorDesc.width = renderWidth;
    colorDesc.height = renderHeight;
    colorDesc.format = (m_litVariantTilesEnabled || m_debugDisplayMode == DebugDisplayMode::Lightmap)
                           ? gfx::TextureFormat::RGBA16F
                           : selectHdrFormat();
    colorDesc.renderTarget = true;
    colorDesc.computeWrite = m_litVariantTilesEnabled;
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
    if (depthPrepass && !deferred) {
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
    RenderGraphTextureHandle clusterLightList;
    if (m_clusterTexture.valid()) {
        clusterLightList = m_renderGraph.importTexture(m_clusterTexture, "Cluster Light List");
    }
    RenderGraphTextureHandle lightScreenBounds;
    if (m_lightScreenBoundsTexture.valid()) {
        lightScreenBounds = m_renderGraph.importTexture(m_lightScreenBoundsTexture, "Light Screen Bounds");
    }
    RenderGraphTextureHandle lightBigTile;
    if (m_lightBigTileTexture.valid()) {
        lightBigTile = m_renderGraph.importTexture(m_lightBigTileTexture, "Light Big Tile List");
    }
    RenderGraphBufferHandle lightScreenBoundsBuffer;
    RenderGraphBufferHandle lightBigTileBuffer;
    RenderGraphBufferHandle lightFptlBuffer;
    RenderGraphBufferHandle lightClusterBuffer;
    RenderGraphBufferHandle lightIndirectArgs;
    if (m_lightScreenBoundsBuffer.valid()) {
        lightScreenBoundsBuffer = m_renderGraph.importBuffer(m_lightScreenBoundsBuffer, "Light Bounds Buffer");
    }
    if (m_lightBigTileBuffer.valid()) {
        lightBigTileBuffer = m_renderGraph.importBuffer(m_lightBigTileBuffer, "Light Big Tile Buffer");
    }
    if (m_lightFptlBuffer.valid()) {
        lightFptlBuffer = m_renderGraph.importBuffer(m_lightFptlBuffer, "Light FPTL Buffer");
    }
    if (m_lightClusterBuffer.valid()) {
        lightClusterBuffer =
            m_renderGraph.importBuffer(m_lightClusterBuffer, "Light Cluster Buffer");
    }
    if (m_lightIndirectArgsBuffer.valid()) {
        lightIndirectArgs = m_renderGraph.importBuffer(m_lightIndirectArgsBuffer, "Light Indirect Args");
    }
    RenderGraphTextureHandle litTileFeatureFlags;
    if (m_litVariantTilesEnabled && m_litTileFeatureFlagsTexture.valid()) {
        litTileFeatureFlags = m_renderGraph.importTexture(m_litTileFeatureFlagsTexture, "Lit Tile Feature Flags");
    }
    RenderGraphBufferHandle litIndirectArgs;
    if (m_litVariantTilesEnabled && m_litIndirectArgsBuffer.valid()) {
        litIndirectArgs = m_renderGraph.importBuffer(m_litIndirectArgsBuffer, "Lit Variant Indirect Args");
    }
    const RenderGraphTextureHandle directionalShadow =
        m_renderGraph.importTexture(m_shadowManager.activeDirectionalTexture(), "Directional Shadow Atlas");
    const RenderGraphTextureHandle cachedDirectionalShadow =
        m_shadowManager.directionalMixedCached()
            ? m_renderGraph.importTexture(m_shadowManager.cachedDirectionalTexture(),
                                          "Cached Directional Shadow Atlas")
            : RenderGraphTextureHandle{};
    const gfx::TextureHandle activePunctualShadow = m_shadowManager.activePunctualTexture();
    const RenderGraphTextureHandle punctualShadow =
        m_renderGraph.importTexture(activePunctualShadow, "Punctual Shadow Atlas");
    const RenderGraphTextureHandle areaShadow =
        m_renderGraph.importTexture(m_shadowManager.activeAreaTexture(), "Area Shadow Atlas");

    const RenderGraphBufferHandle shadowContext =
        m_renderGraph.importBuffer(m_shadowManager.contextBuffer(), "Shadow Context");
    RenderGraphTextureHandle environment;
    RenderGraphTextureHandle skyEnvironment;
    if (m_skyManager.valid()) {
        environment = m_renderGraph.importTexture(m_skyManager.texture(), "Environment IBL");
        skyEnvironment = m_renderGraph.importTexture(m_skyManager.skyTexture(), "Sky Environment");
    }
    const RenderGraphBufferHandle fullscreen =
        m_renderGraph.importBuffer(m_fullscreenVertexBuffer, "Fullscreen Triangle");
    RenderGraphBufferHandle shadowRequestBuffer;
    RenderGraphBufferHandle shadowCasterBuffer;
    RenderGraphBufferHandle shadowBatchBuffer;
    RenderGraphBufferHandle shadowInstanceBuffer;
    RenderGraphBufferHandle shadowIndirectBuffer;
    RenderGraphBufferHandle shadowStatsBuffer;
    if (m_shadowGpuCullingActive) {
        shadowRequestBuffer = m_renderGraph.importBuffer(m_shadowRequestBuffer, "Shadow Requests");
        shadowCasterBuffer = m_renderGraph.importBuffer(m_shadowCasterBuffer, "Shadow Casters");
        shadowBatchBuffer = m_renderGraph.importBuffer(m_shadowBatchBuffer, "Shadow Batches");
        shadowInstanceBuffer = m_renderGraph.importBuffer(m_shadowInstanceBuffer, "Shadow Instances");
        shadowIndirectBuffer = m_renderGraph.importBuffer(m_shadowIndirectBuffer, "Shadow Indirect Args");
        shadowStatsBuffer = m_renderGraph.importBuffer(m_shadowStatsBuffer, "Shadow Cull Stats");
    }

    FramePassResources resources;
    resources.color = color;
    resources.depth = depth;
    resources.normal = normal;
    resources.motionVector = motionVector;
    resources.depthPyramid = depthPyramid;
    resources.lightData = lightData;
    resources.lightList = lightList;
    resources.clusterLightList = clusterLightList;
    resources.lightScreenBounds = lightScreenBounds;
    resources.lightBigTile = lightBigTile;
    resources.lightScreenBoundsBuffer = lightScreenBoundsBuffer;
    resources.lightBigTileBuffer = lightBigTileBuffer;
    resources.lightFptlBuffer = lightFptlBuffer;
    resources.lightClusterBuffer = lightClusterBuffer;
    resources.lightIndirectArgs = lightIndirectArgs;
    resources.litTileFeatureFlags = litTileFeatureFlags;
    resources.litIndirectArgs = litIndirectArgs;
    resources.shadowRequestBuffer = shadowRequestBuffer;
    resources.shadowCasterBuffer = shadowCasterBuffer;
    resources.shadowBatchBuffer = shadowBatchBuffer;
    resources.shadowInstanceBuffer = shadowInstanceBuffer;
    resources.shadowIndirectBuffer = shadowIndirectBuffer;
    resources.shadowStatsBuffer = shadowStatsBuffer;
    resources.directionalShadow = directionalShadow;
    resources.punctualShadow = punctualShadow;
    resources.areaShadow = areaShadow;
    resources.shadowContext = shadowContext;
    resources.environment = environment;
    resources.skyEnvironment = skyEnvironment;
    resources.fullscreen = fullscreen;
    if (deferred) {
        constexpr std::array<const char*, 5> kGBufferNames = {
            "GBuffer Base Color",
            "GBuffer Material",
            "GBuffer Normal",
            "GBuffer Emissive",
            "GBuffer Advanced",
        };
        for (std::size_t index = 0; index < resources.gbuffer.size(); ++index) {
            resources.gbuffer[index] = m_renderGraph.importTexture(m_gbufferTextures[index], kGBufferNames[index]);
        }
    }
    if (m_depthPrepassActive && m_lightmapSet.valid() && !m_bakedGIDrawList.empty() &&
        bakedGIProgramsReady()) {
        gfx::TextureDesc bakedDesc;
        bakedDesc.width = renderWidth;
        bakedDesc.height = renderHeight;
        bakedDesc.format = gfx::TextureFormat::RGBA16F;
        bakedDesc.renderTarget = true;
        bakedDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        bakedDesc.name = "gbuffer.bakedIndirect";
        resources.bakedIndirect = m_renderGraph.createTexture(bakedDesc, "Baked Indirect");
    }

    const bool decalsRequested = deferred && !scene.decals.empty() &&
                                 m_frameSettings.isEnabled(FrameSettingsField::Decals) &&
                                 m_pipelineSettings.supportDecals;
    const bool decalsAvailable =
        decalsRequested && depth.valid() && depthPyramid.valid() && fullscreen.valid() &&
        ensureDecalTextures(renderWidth, renderHeight) &&
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::GBufferDecals, 0, 0}).valid() &&
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::Decal, 0, 0}).valid() &&
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::DecalClear, 0, 0}).valid();
    if (decalsAvailable) {
        constexpr std::array<const char*, 4> kDBufferNames = {
            "DBuffer Albedo",
            "DBuffer Normal",
            "DBuffer Material",
            "DBuffer Blend",
        };
        for (std::size_t index = 0; index < resources.decal.size(); ++index) {
            resources.decal[index] = m_renderGraph.importTexture(m_decalTextures[index], kDBufferNames[index]);
        }
        if (m_frameIndex == 0) {
            HBG_LOGI("decals: {} static box decals, DBuffer {}x{} (4RT)", scene.decals.size(), renderWidth,
                     renderHeight);
        }
    } else if (decalsRequested) {
        if (!m_decalFallbackLogged) {
            HBG_LOGW("decals requested but unavailable (programs or depth resources missing)");
            m_decalFallbackLogged = true;
        }
        releaseDecalTextures();
    }

    const bool ssaoRequested =
        deferred && m_frameSettings.isEnabled(FrameSettingsField::SSAO) && m_pipelineSettings.supportSSAO;
    const bool ssaoAvailable =
        ssaoRequested && depthPyramid.valid() && resources.gbuffer[2].valid() && resources.fullscreen.valid() &&
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSAO, 0, 0}).valid() &&
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSAOBlur, 0, 0}).valid() &&
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSAOUpsample, 0, 0}).valid();
    updateSsaoUniforms(ssaoAvailable);
    const bool ssrRequested = deferred && m_frameSettings.isEnabled(FrameSettingsField::SSR) &&
                              m_pipelineSettings.supportSSR && m_ssrSettings.enabled &&
                              m_ssrSettings.intensity > 0.0f;
    const bool ssrAvailable =
        ssrRequested && color.valid() && depth.valid() && depthPyramid.valid() && resources.gbuffer[2].valid() &&
        resources.gbuffer[0].valid() && resources.gbuffer[1].valid() && fullscreen.valid() &&
        lightListProgram(shader::PassId::DeferredLighting).valid() &&
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSR, 0, 0}).valid() &&
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSRComposite, 0, 0}).valid();
    updateSsrUniforms(ssrAvailable);
    const bool ssgiRequested = deferred && m_frameSettings.isEnabled(FrameSettingsField::SSGI) &&
                               m_pipelineSettings.supportSSGI && m_ssgiSettings.enabled &&
                               m_ssgiSettings.intensity > 0.0f;
    const bool ssgiAvailable =
        ssgiRequested && color.valid() && depth.valid() && depthPyramid.valid() && resources.gbuffer[0].valid() &&
        resources.gbuffer[1].valid() && resources.gbuffer[2].valid() && fullscreen.valid() &&
        lightListProgram(shader::PassId::DeferredLighting).valid() &&
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSGI, 0, 0}).valid() &&
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSGIDenoise, 0, 0}).valid() &&
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSGIComposite, 0, 0}).valid();
    updateSsgiUniforms(ssgiAvailable);
    m_ssgiTemporalActive = false;
    if (ssgiAvailable) {
        const std::array<uint16_t, 2> resolution =
            ssgiResolution(renderWidth, renderHeight, m_ssgiSettings.halfResolution);
        gfx::TextureDesc ssgiDesc;
        ssgiDesc.width = resolution[0];
        ssgiDesc.height = resolution[1];
        ssgiDesc.format = selectHdrFormat();
        ssgiDesc.renderTarget = true;
        ssgiDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        ssgiDesc.name = "ssgi.raw";
        resources.ssgiRaw = m_renderGraph.createTexture(ssgiDesc, "SSGI Trace");
        gfx::TextureDesc denoiseDesc = ssgiDesc;
        denoiseDesc.name = "ssgi.denoise";
        resources.ssgi = m_renderGraph.createTexture(denoiseDesc, "SSGI Denoise");

        if (m_ssgiSettings.temporalAccumulation && resources.motionVector.valid() &&
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSGITemporal, 0, 0}).valid()) {
            m_ssgiTemporalActive = ensureSsgiHistory(resolution[0], resolution[1]);
        }
        if (!m_ssgiTemporalActive) {
            m_ssgiHistoryValid = false;
        } else if (m_frameIndex == 0) {
            HBG_LOGI("ssgi: temporal enabled, history={}x{}", resolution[0], resolution[1]);
        }
        if (m_frameIndex == 0) {
            HBG_LOGI("ssgi: trace {}x{}, rays={}, steps={}, maxDistance={}, falloff={}, thickness={}",
                     resolution[0], resolution[1], m_ssgiSettings.rayCount, m_ssgiSettings.maxSteps,
                     m_ssgiSettings.maxDistance, m_ssgiSettings.falloff, m_ssgiSettings.thickness);
        }
    } else {
        m_ssgiHistoryValid = false;
    }
    m_ssrTemporalActive = false;
    if (ssrAvailable && m_ssrSettings.temporalAccumulation && resources.motionVector.valid() &&
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSRTemporal, 0, 0}).valid()) {
        m_ssrTemporalActive = ensureSsrHistory(renderWidth, renderHeight);
    }
    if (!m_ssrTemporalActive) {
        m_ssrHistoryValid = false;
    } else if (m_frameIndex == 0) {
        HBG_LOGI("ssr: temporal enabled, weight={}, history={}x{}",
                 m_ssrSettings.temporalWeight, m_ssrHistoryWidth, m_ssrHistoryHeight);
    }
    if (ssrAvailable) {
        gfx::TextureDesc ssrDesc;
        ssrDesc.width = renderWidth;
        ssrDesc.height = renderHeight;
        ssrDesc.format = m_ssrTemporalActive ? gfx::TextureFormat::RGBA16F : selectHdrFormat();
        ssrDesc.renderTarget = true;
        ssrDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        ssrDesc.name = "ssr.trace";
        resources.ssr = m_renderGraph.createTexture(ssrDesc, "SSR Trace");
        if (m_frameIndex == 0) {
            HBG_LOGI("ssr: {}x{}, steps={}, maxDistance={}, thickness={}, roughnessFade={}, edgeFade={}, "
                     "format={}",
                     renderWidth, renderHeight, m_ssrSettings.maxSteps, m_ssrSettings.maxDistance,
                     m_ssrSettings.thickness, m_ssrSettings.roughnessFade, m_ssrSettings.edgeFade,
                     gfx::textureFormatName(ssrDesc.format));
        }
    }
    m_ssaoTemporalActive = false;
    if (ssaoAvailable) {
        const std::array<uint16_t, 2> resolution =
            ssaoResolution(renderWidth, renderHeight, m_ssaoSettings.halfResolution);
        const gfx::TextureFormat format = selectSsaoFormat();

        const bool ssaoTemporalRequested =
            m_ssaoSettings.temporalAccumulation && resources.motionVector.valid() &&
            m_shaderLibrary.program(shader::ProgramKey{shader::PassId::SSAOTemporal, 0, 0}).valid();
        if (ssaoTemporalRequested) {
            if (ensureSsaoHistory(resolution[0], resolution[1])) {
                m_ssaoTemporalActive = true;
            } else if (!m_ssaoTemporalFallbackLogged) {
                HBG_LOGW("ssao: temporal history is unavailable; falling back to spatial SSAO");
                m_ssaoTemporalFallbackLogged = true;
            }
        }

        gfx::TextureDesc rawDesc;
        rawDesc.width = resolution[0];
        rawDesc.height = resolution[1];
        rawDesc.format = format;
        rawDesc.renderTarget = true;
        rawDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        rawDesc.name = "ssao.raw";
        resources.ssaoRaw = m_renderGraph.createTexture(rawDesc, "SSAO Raw");

        gfx::TextureDesc blurDesc = rawDesc;
        blurDesc.name = "ssao.blur";
        resources.ssaoBlur = m_renderGraph.createTexture(blurDesc, "SSAO Blur");

        gfx::TextureDesc fullDesc;
        fullDesc.width = renderWidth;
        fullDesc.height = renderHeight;
        fullDesc.format = format;
        fullDesc.renderTarget = true;
        fullDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        fullDesc.name = "ssao.full";
        resources.ssao = m_renderGraph.createTexture(fullDesc, "SSAO");
        if (m_frameIndex == 0) {
            HBG_LOGI("ssao: trace {}x{}, upsample {}x{}, steps={}, radius={}, intensity={}, temporal={}",
                     resolution[0], resolution[1], renderWidth, renderHeight, m_ssaoSettings.stepCount,
                     m_ssaoSettings.radius, m_ssaoSettings.intensity,
                     m_ssaoTemporalActive ? "true" : "false");
        }
    }
    if (!m_ssaoTemporalActive) {
        m_ssaoHistoryValid = false;
    }

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
        if ((!normal.valid() && !(deferred && resources.gbuffer[2].valid())) ||
            !hasProgram(shader::PassId::DebugNormal)) {
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
    case DebugDisplayMode::LightCluster: {
        const bool listAvailable =
            m_gpuLightListBufferActive ? resources.lightFptlBuffer.valid() : lightList.valid();
        const bool clusterAvailable = m_gpuLightListBufferActive ? resources.lightClusterBuffer.valid()
                                                                 : m_clusterTexture.valid();
        if (!depth.valid() || !lightListProgram(shader::PassId::DebugLightCluster).valid() ||
            (m_debugLightClusterSlice == -2
                 ? !listAvailable
                 : (!m_lightClusterEnabled || !clusterAvailable ||
                    m_clusterUniforms.depthParams.w <= 0.5f))) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    }
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
    case DebugDisplayMode::ShadowCascade:
        if (!shadowsActive() || !directionalShadow.valid() || !resources.shadowContext.valid() ||
            m_shadowManager.directionalRequestCount() == 0 ||
            (!normal.valid() && !(deferred && resources.gbuffer[2].valid())) ||
            !hasProgram(shader::PassId::DebugShadowCascade)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::ShadowFilter:
        if (!shadowsActive() || !directionalShadow.valid() || !resources.shadowContext.valid() ||
            m_shadowManager.directionalRequestCount() == 0 ||
            (!normal.valid() && !(deferred && resources.gbuffer[2].valid())) ||
            !hasProgram(shader::PassId::DebugShadowFilter)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::Albedo:
    case DebugDisplayMode::Roughness:
        if (deferred) {
            if (!std::all_of(resources.gbuffer.begin(), resources.gbuffer.end(),
                             [](RenderGraphTextureHandle texture) { return texture.valid(); }) ||
                !resources.depth.valid() || !resources.fullscreen.valid() ||
                !hasProgram(shader::PassId::DebugGBuffer) || !hasProgram(shader::PassId::DebugColor)) {
                debugMode = DebugDisplayMode::None;
            }
        } else if (!m_depthPrepassActive || m_opaqueDrawList.empty() ||
                   !hasProgram(debugMode == DebugDisplayMode::Albedo ? shader::PassId::DebugAlbedo
                                                                     : shader::PassId::DebugRoughness) ||
                   !hasProgram(shader::PassId::DebugColor)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::Metallic:
    case DebugDisplayMode::AmbientOcclusion:
    case DebugDisplayMode::Emissive:
        if (!deferred ||
            !std::all_of(resources.gbuffer.begin(), resources.gbuffer.end(),
                         [](RenderGraphTextureHandle texture) { return texture.valid(); }) ||
            !resources.depth.valid() || !resources.fullscreen.valid() ||
            !hasProgram(shader::PassId::DebugGBuffer) || !hasProgram(shader::PassId::DebugColor)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::SSAO:
        if (!resources.ssao.valid() || !hasProgram(shader::PassId::DebugSSAO)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::SSAORaw:
        if (!resources.ssaoRaw.valid() || !hasProgram(shader::PassId::DebugSSAO)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::SSAOHistory:
        if (!m_ssaoTemporalActive || m_ssaoHistoryFrame == 0 ||
            !hasProgram(shader::PassId::DebugSSAO)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::SSR:
        if (!resources.ssr.valid() || !hasProgram(shader::PassId::DebugSSR)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::SSRRaw:
        if (!resources.ssr.valid() || !hasProgram(shader::PassId::DebugSSR)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::SSRHistory:
        if (!m_ssrTemporalActive || m_ssrHistoryFrame == 0 ||
            !hasProgram(shader::PassId::DebugSSR)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::SSGI:
        if (!resources.ssgi.valid() || !resources.depthPyramid.valid() || !resources.gbuffer[2].valid() ||
            !hasProgram(shader::PassId::DebugSSGI)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::SSGIRaw:
        if (!resources.ssgiRaw.valid() || !hasProgram(shader::PassId::DebugSSGIRaw)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::SSGIHistory:
        if (!m_ssgiTemporalActive || m_ssgiHistoryFrame == 0 ||
            !hasProgram(shader::PassId::DebugSSGIRaw)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::Decals:
        if (!resources.decal[0].valid() || !hasProgram(shader::PassId::DebugDecals)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::Exposure:
        if (!m_postProcessChain.exposureTexture().valid() || !m_postProcessChain.histogramTexture().valid() ||
            !hasProgram(shader::PassId::DebugExposure)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::Bloom:
        if (!hasProgram(shader::PassId::DebugBloom)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::TaaHistory:
        if (!hasProgram(shader::PassId::DebugTaaHistory)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::Dof:
        if (!hasProgram(shader::PassId::DebugDofCoc)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::Fxaa:
        if (!hasProgram(shader::PassId::DebugFxaa)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::Lightmap:
        if (!resources.color.valid() || !resources.depth.valid() ||
            !hasProgram(shader::PassId::DebugLightmap)) {
            debugMode = DebugDisplayMode::None;
        }
        break;
    case DebugDisplayMode::None:
        break;
    }

    m_deferredActive = deferred;
    m_advancedLightingActive = false;
    const bool materialDebug = !deferred && isMaterialDebug(debugMode);
    if (debugMode == DebugDisplayMode::ShadowAtlasDirectional) {
        addShadowAtlasClearPass(m_renderGraph, directionalShadow, m_shadowManager.directionalAtlasSize());
    } else if (debugMode == DebugDisplayMode::ShadowAtlasPunctual) {
        addShadowAtlasClearPass(m_renderGraph, punctualShadow, m_shadowManager.punctualAtlasSize());
    }
    const bool clearShadowAtlases = m_frameIndex == 0;
    addShadowAtlasClearPass(m_renderGraph, directionalShadow, m_shadowManager.directionalAtlasSize(), clearShadowAtlases);
    addShadowAtlasClearPass(m_renderGraph, punctualShadow, m_shadowManager.punctualAtlasSize(), clearShadowAtlases);
    addShadowAtlasClearPass(m_renderGraph, areaShadow, m_shadowManager.areaAtlasSize(), clearShadowAtlases);
    addGpuShadowCullPass(m_renderGraph, resources);
    addShadowPass(m_renderGraph, directionalShadow, cachedDirectionalShadow, punctualShadow, areaShadow, scene);
    addAreaEvsmMomentPasses(m_renderGraph, areaShadow);
    addShadowMomentPasses(m_renderGraph, directionalShadow);
    addClearPass(m_renderGraph, color, depth, materialDebug || (deferred && isGbufferDebug(debugMode)));
    addPrepassPass(m_renderGraph, normal, depth, scene, deferred);
    addDepthPyramidPasses(m_renderGraph, depth, depthPyramid);
    addObjectsMotionVectorsPass(m_renderGraph, motionVector, depth, scene);
    addCameraMotionVectorsPass(m_renderGraph, motionVector, depth, depthPyramid);
    addGpuLightListPasses(m_renderGraph, resources);
    if (deferred) {
        recordDeferredFrame(m_renderGraph, resources, scene, debugMode, materialDebug);
    } else {
        recordForwardFrame(m_renderGraph, resources, scene, debugMode, materialDebug);
    }
    if ((debugMode == DebugDisplayMode::SSAOHistory && !resources.ssaoHistoryPrevious.valid()) ||
        (debugMode == DebugDisplayMode::SSRHistory && !resources.ssrHistoryPrevious.valid()) ||
        (debugMode == DebugDisplayMode::SSGIHistory && !resources.ssgiHistoryPrevious.valid()) ||
        (debugMode == DebugDisplayMode::SSRRaw && !resources.ssrRaw.valid()) ||
        (debugMode == DebugDisplayMode::SSGIRaw && !resources.ssgiRaw.valid())) {
        debugMode = DebugDisplayMode::None;
    }

    RenderGraphTextureHandle fogColor;
    const bool fogRequested =
        m_volumetricFogSettings.active() && m_frameSettings.isEnabled(FrameSettingsField::AtmosphericScattering) &&
        m_frameSettings.isEnabled(FrameSettingsField::Volumetrics) && m_pipelineSettings.supportVolumetrics;
    const bool fogAvailable =
        fogRequested && debugMode == DebugDisplayMode::None && color.valid() && depth.valid() &&
        resources.fullscreen.valid() &&
        lightListProgram(shader::PassId::VolumetricFog).valid() &&
        m_shaderLibrary.program(shader::ProgramKey{shader::PassId::VolumetricFogComposite, 0, 0}).valid();
    m_volumetricFogActive = fogAvailable;
    m_volumetricFogSteps = fogAvailable ? m_volumetricFogSettings.stepCount : 0;
    if (fogAvailable) {
        fogColor = addVolumetricFogPass(m_renderGraph, resources, color, scene);
        m_volumetricFogActive = fogColor.valid();
        m_volumetricFogSteps = fogColor.valid() ? m_volumetricFogSettings.stepCount : 0;
        if (!fogColor.valid() && !m_volumetricFogLogged) {
            HBG_LOGW("volumetric fog: pass was not recorded");
            m_volumetricFogLogged = true;
        }
    } else if (fogRequested && debugMode == DebugDisplayMode::None && !m_volumetricFogLogged) {
        HBG_LOGW("volumetric fog: shader unavailable; skipping fog");
        m_volumetricFogLogged = true;
    }
    if (!fogColor.valid()) {
        m_volumetricFogWidth = 0;
        m_volumetricFogHeight = 0;
        m_volumetricFogTemporalActive = false;
        m_volumetricFogPunctualLights = 0;
        m_volumetricFogLocalVolumes = 0;
        m_volumetricFogLocalMasks = 0;
        m_fogHistoryValid = false;
    }
    const RenderGraphTextureHandle postProcessSource = fogColor.valid() ? fogColor : color;

    const auto postProcessStart = std::chrono::steady_clock::now();
    bool postProcessRecorded = false;
    RenderGraphTextureHandle presentationColor = postProcessSource;
    PostProcessOutput postProcessOutput{};
    if (debugMode == DebugDisplayMode::None || debugMode == DebugDisplayMode::Exposure ||
        debugMode == DebugDisplayMode::ColorGrading || debugMode == DebugDisplayMode::Bloom ||
        debugMode == DebugDisplayMode::TaaHistory || debugMode == DebugDisplayMode::Dof ||
        debugMode == DebugDisplayMode::Fxaa) {
        PostProcessInputs postProcessInputs;
        postProcessInputs.color = postProcessSource;
        postProcessInputs.fullscreen = resources.fullscreen;
        postProcessInputs.depth = depth;
        postProcessInputs.depthPyramid = depthPyramid;
        postProcessInputs.motionVectors = motionVector;
        postProcessInputs.normal = normal;
        postProcessInputs.stencil = depth;
        postProcessInputs.renderWidth = renderWidth;
        postProcessInputs.renderHeight = renderHeight;
        postProcessInputs.targetWidth = width;
        postProcessInputs.targetHeight = height;
        postProcessInputs.renderScale = m_renderScale;
        postProcessInputs.deltaSeconds = deltaTime;
        postProcessInputs.frameIndex = m_frameIndex;
        postProcessInputs.historyValid = m_viewState.historyValid();
        postProcessInputs.forceColorGradingLut = debugMode == DebugDisplayMode::ColorGrading;
        postProcessInputs.forceBloom = debugMode == DebugDisplayMode::Bloom;
        postProcessInputs.forceDof = debugMode == DebugDisplayMode::Dof;
        postProcessInputs.dofCocOnly = debugMode == DebugDisplayMode::Dof;
        postProcessOutput = m_postProcessChain.record(m_renderGraph, postProcessInputs);
        presentationColor = postProcessOutput.color;
        postProcessRecorded = true;
    }
    if (debugMode == DebugDisplayMode::Exposure && !m_postProcessChain.exposureActive()) {
        HBG_LOGW("exposure debug view requires Automatic or Histogram exposure");
        debugMode = DebugDisplayMode::None;
    }
    if (debugMode == DebugDisplayMode::ColorGrading &&
        !m_postProcessChain.colorGradingLutTexture().valid()) {
        HBG_LOGW("color grading debug view requires an active color grading path");
        debugMode = DebugDisplayMode::None;
    }
    if (debugMode == DebugDisplayMode::Bloom && !postProcessOutput.bloomActive) {
        HBG_LOGW("bloom debug view requires bloom support and a renderable frame");
        debugMode = DebugDisplayMode::None;
    }
    if (debugMode == DebugDisplayMode::TaaHistory && !postProcessOutput.taaActive) {
        HBG_LOGW("TAA history debug view requires --aa=taa/--taa and a renderable frame");
        debugMode = DebugDisplayMode::None;
    }
    if (debugMode == DebugDisplayMode::Dof && !postProcessOutput.dofActive) {
        HBG_LOGW("DOF debug view requires a renderable frame with depth");
        debugMode = DebugDisplayMode::None;
    }
    if (debugMode == DebugDisplayMode::Fxaa && !postProcessOutput.fxaaActive) {
        HBG_LOGW("FXAA debug view requires --aa=fxaa/--fxaa and a renderable frame");
        debugMode = DebugDisplayMode::None;
    }

    if (debugMode == DebugDisplayMode::None && postProcessOutput.usedUber) {
        if (!m_postProcessChain.addFinalPass(m_renderGraph, presentationColor, resources.fullscreen, width,
                                             height)) {
            HBG_LOGE("forward renderer: failed to add the Uber final pass");
        }
    } else {
        addPresentationPass(m_renderGraph, resources, presentationColor, debugMode, postProcessOutput.bloom,
                            postProcessOutput.taaHistory, postProcessOutput.dofCoc);
    }

    const bool postProcessWork = postProcessOutput.effectsActive || m_postProcessChain.exposureActive() ||
                                 postProcessOutput.usedUber || postProcessOutput.bloomActive ||
                                 postProcessOutput.taaActive || postProcessOutput.dofActive ||
                                 postProcessOutput.motionBlurActive || postProcessOutput.fxaaActive;
    if (postProcessRecorded && postProcessWork) {
        const auto postProcessEnd = std::chrono::steady_clock::now();
        m_postProcessWallTotalMs +=
            std::chrono::duration<double, std::milli>(postProcessEnd - postProcessStart).count();
        ++m_postProcessWallFrames;
    }

    gfx::Encoder encoder(*m_resources);
    bool graphExecuted = false;
    if (!m_renderGraph.execute(encoder, *m_resources)) {
        HBG_LOGE("forward renderer: render graph execution failed: {}", m_renderGraph.lastError());
    } else {
        graphExecuted = true;
        if (m_frameIndex == 0 || (m_postProcessChain.taaActive() && m_frameIndex == 1)) {
            const RenderGraphDiagnostics& diagnostics = m_renderGraph.diagnostics();
            HBG_LOGI("render graph: {} recorded, {} live, {} culled; transient textures={}, pooled={}, framebuffers={}",
                     diagnostics.recordedPasses, diagnostics.livePasses, diagnostics.culledPasses,
                     diagnostics.transientTextures, diagnostics.pooledTextures, diagnostics.framebuffers);
            HBG_LOGI("gpu light list: requested={} active={} fallback={} passes={} cpu_ms={:.3f} bounds={}x{} "
                     "bigTile={}x{} fptl={}x{}",
                     m_gpuLightListEnabled ? "true" : "false", m_gpuLightListActive ? "true" : "false",
                     m_gpuLightListEnabled && !m_gpuLightListActive ? "true" : "false", m_gpuLightListPasses,
                     m_lightListCpuMs, kMaxGpuLights * 2u, 1,
                     LightListBuilder::bigTileTextureWidthFor(renderWidth, m_lightListTextureCapacity),
                     LightListBuilder::bigTileTextureHeightFor(renderHeight),
                     LightListBuilder::textureWidthFor(renderWidth, m_lightListTextureCapacity),
                     LightListBuilder::textureHeightFor(renderHeight));
            HBG_LOGI("postprocess diagnostics: active={} compute={} history={} passes={} profile='{}' exposure={} "
                     "exposure_active={} exposure_applied={} exposure_history={} taa_active={} taa_history={} "
                     "taa_frames={} dof_active={} dof_coc={}x{} dof_taps={} fxaa_active={} "
                     "ssao_temporal={} ssao_history={} ssao_frames={} ssr_temporal={} ssr_history={} "
                     "ssr_frames={} ssgi_temporal={} ssgi_history={} ssgi_frames={}",
                     (m_postProcessChain.effectsActive() || m_postProcessChain.exposureActive()) ? "true" : "false",
                     m_postProcessChain.computeSupported() ? "true" : "false",
                     m_postProcessChain.historyValid() ? "true" : "false",
                     m_postProcessChain.postProcessPassCount(), m_postProcessChain.profile().name.c_str(),
                     exposureModeName(m_postProcessSettings.exposureMode),
                     m_postProcessChain.exposureActive() ? "true" : "false",
                     m_postProcessChain.exposureApplied() ? "true" : "false",
                     m_postProcessChain.exposureHistoryValid() ? "true" : "false",
                     m_postProcessChain.taaActive() ? "true" : "false",
                     m_postProcessChain.taaHistoryValid() ? "true" : "false",
                     m_postProcessChain.taaHistoryFrameCount(), m_postProcessChain.dofActive() ? "true" : "false",
                      m_postProcessChain.dofCocWidth(), m_postProcessChain.dofCocHeight(),
                      m_postProcessChain.dofTapCount(), m_postProcessChain.fxaaActive() ? "true" : "false",
                      m_ssaoTemporalActive ? "true" : "false",
                      m_ssaoHistoryValid ? "true" : "false", m_ssaoHistoryFrame,
                      m_ssrTemporalActive ? "true" : "false",
                      m_ssrHistoryValid ? "true" : "false", m_ssrHistoryFrame,
                      m_ssgiTemporalActive ? "true" : "false",
                      m_ssgiHistoryValid ? "true" : "false", m_ssgiHistoryFrame);
            if (deferred) {
                const char* separator = "";
                std::string order;
                for (const std::string& name : m_renderGraph.passNames()) {
                    order += separator;
                    order += name;
                    separator = " -> ";
                }
                HBG_LOGI("deferred pass order: {}", order);
                HBG_LOGI("deferred diagnostics: GBuffer MRT=5, advanced={}, depth={}, stencil={}, color={}; SSAO={} SSR={} "
                         "SSGI={} Decals={} Probes={} ({} selected/{} loaded); live passes={}, culled={}",
                         m_advancedLightingActive ? "true" : "false",
                         gfx::textureFormatName(selectDepthFormat()),
                         selectDepthFormat() == gfx::TextureFormat::D24S8 ? "true" : "false",
                         gfx::textureFormatName(selectHdrFormat()),
                         m_frameSettings.isEnabled(FrameSettingsField::SSAO) && m_pipelineSettings.supportSSAO,
                         m_frameSettings.isEnabled(FrameSettingsField::SSR) && m_pipelineSettings.supportSSR,
                         m_frameSettings.isEnabled(FrameSettingsField::SSGI) && m_pipelineSettings.supportSSGI,
                         m_frameSettings.isEnabled(FrameSettingsField::Decals) && m_pipelineSettings.supportDecals,
                         m_frameSettings.isEnabled(FrameSettingsField::ReflectionProbe), m_probeManager.selectedCount(),
                         m_probeManager.loadedCount(), diagnostics.livePasses, diagnostics.culledPasses);
            }
        }
    }

    if (graphExecuted) {
        submitClusterStatsReadback();
    }
    m_postProcessChain.endFrame();
    ++m_frameIndex;
}

void ForwardRenderer::shutdown(gfx::ResourceManager& resources) {
    m_valid = false;
    releaseVfx();
    m_volumetricFogActive = false;
    m_volumetricFogSteps = 0;
    m_volumetricFogWidth = 0;
    m_volumetricFogHeight = 0;
    m_volumetricFogTemporalActive = false;
    m_volumetricFogPunctualLights = 0;
    m_volumetricFogLocalVolumes = 0;
    m_volumetricFogLocalMasks = 0;
    releaseVolumetricFogHistory();
    releaseLocalFogMaskTextures(resources);
    m_renderGraph.shutdown(resources);
    m_postProcessChain.shutdown(resources);
    releaseGBufferTextures();
    releaseDecalTextures();
    releaseLitVariantTileResources();
    releaseShadowGpuBuffers();
    m_lightmapSet.destroy(resources);
    if (m_lightmapSampler.valid()) {
        resources.destroyUniform(m_lightmapSampler);
        m_lightmapSampler = {};
    }
    m_shaderLibrary.shutdown(resources);
    m_materialSystem.shutdown(resources);
    m_shadowManager.shutdown(resources);
    m_skyManager.shutdown(resources);
    m_probeManager.shutdown(resources);

    if (m_environmentFallback.valid()) {
        resources.destroyTexture(m_environmentFallback);
        m_environmentFallback = {};
    }
    if (m_environmentSampler.valid()) {
        resources.destroyUniform(m_environmentSampler);
        m_environmentSampler = {};
    }
    if (m_probeArraySampler.valid()) {
        resources.destroyUniform(m_probeArraySampler);
        m_probeArraySampler = {};
    }
    if (m_cookieAtlasSampler.valid()) {
        resources.destroyUniform(m_cookieAtlasSampler);
        m_cookieAtlasSampler = {};
    }
    if (m_probeArrayFallback.valid()) {
        resources.destroyTexture(m_probeArrayFallback);
        m_probeArrayFallback = {};
    }
    if (m_cookieAtlasTexture.valid()) {
        resources.destroyTexture(m_cookieAtlasTexture);
        m_cookieAtlasTexture = {};
    }
    m_cookieAtlasUploaded = false;
    if (m_cookieAtlasFallback.valid()) {
        resources.destroyTexture(m_cookieAtlasFallback);
        m_cookieAtlasFallback = {};
    }
    releaseSsaoHistory();
    releaseSsrHistory();
    releaseSsgiHistory();
    if (m_ssaoFallback.valid()) {
        resources.destroyTexture(m_ssaoFallback);
        m_ssaoFallback = {};
    }
    if (m_bakedIndirectFallback.valid()) {
        resources.destroyTexture(m_bakedIndirectFallback);
        m_bakedIndirectFallback = {};
    }
    if (m_contactShadowFallback.valid()) {
        resources.destroyTexture(m_contactShadowFallback);
        m_contactShadowFallback = {};
    }
    if (m_ssaoSampler.valid()) {
        resources.destroyUniform(m_ssaoSampler);
        m_ssaoSampler = {};
    }
    if (m_bakedIndirectSampler.valid()) {
        resources.destroyUniform(m_bakedIndirectSampler);
        m_bakedIndirectSampler = {};
    }
    if (m_contactShadowSampler.valid()) {
        resources.destroyUniform(m_contactShadowSampler);
        m_contactShadowSampler = {};
    }
    if (m_ssaoInputSampler.valid()) {
        resources.destroyUniform(m_ssaoInputSampler);
        m_ssaoInputSampler = {};
    }
    if (m_ssaoHistorySampler.valid()) {
        resources.destroyUniform(m_ssaoHistorySampler);
        m_ssaoHistorySampler = {};
    }
    if (m_ssrSampler.valid()) {
        resources.destroyUniform(m_ssrSampler);
        m_ssrSampler = {};
    }
    if (m_ssrHistorySampler.valid()) {
        resources.destroyUniform(m_ssrHistorySampler);
        m_ssrHistorySampler = {};
    }
    if (m_ssgiSampler.valid()) {
        resources.destroyUniform(m_ssgiSampler);
        m_ssgiSampler = {};
    }
    if (m_ssgiRawSampler.valid()) {
        resources.destroyUniform(m_ssgiRawSampler);
        m_ssgiRawSampler = {};
    }
    if (m_ssgiHistorySampler.valid()) {
        resources.destroyUniform(m_ssgiHistorySampler);
        m_ssgiHistorySampler = {};
    }
    if (m_ssgiHistoryDepthSampler.valid()) {
        resources.destroyUniform(m_ssgiHistoryDepthSampler);
        m_ssgiHistoryDepthSampler = {};
    }
    if (m_ssgiHistoryNormalSampler.valid()) {
        resources.destroyUniform(m_ssgiHistoryNormalSampler);
        m_ssgiHistoryNormalSampler = {};
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
    releaseLightBigTileTexture();
    releaseClusterLightListTextures();
    releaseClusterStatsReadbackTexture();
    releaseLightListBuffers();
    m_clusterStatsReadbackPending = false;
    m_clusterStatsReadbackRequested = false;
    m_clusterStatsReadbackFrame = 0;
    m_clusterStatsReferenceValid = false;
    if (m_lightScreenBoundsTexture.valid()) {
        resources.destroyTexture(m_lightScreenBoundsTexture);
        m_lightScreenBoundsTexture = {};
    }
    if (m_lightViewRowsTexture.valid()) {
        resources.destroyTexture(m_lightViewRowsTexture);
        m_lightViewRowsTexture = {};
    }
    if (m_lightListSampler.valid()) {
        resources.destroyUniform(m_lightListSampler);
        m_lightListSampler = {};
    }
    if (m_lightScreenBoundsSampler.valid()) {
        resources.destroyUniform(m_lightScreenBoundsSampler);
        m_lightScreenBoundsSampler = {};
    }
    if (m_lightBigTileSampler.valid()) {
        resources.destroyUniform(m_lightBigTileSampler);
        m_lightBigTileSampler = {};
    }
    if (m_lightViewRowsSampler.valid()) {
        resources.destroyUniform(m_lightViewRowsSampler);
        m_lightViewRowsSampler = {};
    }
    if (m_shadowSampler.valid()) {
        resources.destroyUniform(m_shadowSampler);
        m_shadowSampler = {};
    }
    if (m_shadowPunctualSampler.valid()) {
        resources.destroyUniform(m_shadowPunctualSampler);
        m_shadowPunctualSampler = {};
    }
    if (m_shadowPointSampler.valid()) {
        resources.destroyUniform(m_shadowPointSampler);
        m_shadowPointSampler = {};
    }
    if (m_shadowPointPunctualSampler.valid()) {
        resources.destroyUniform(m_shadowPointPunctualSampler);
        m_shadowPointPunctualSampler = {};
    }
    if (m_shadowCopySampler.valid()) {
        resources.destroyUniform(m_shadowCopySampler);
        m_shadowCopySampler = {};
    }
    if (m_shadowCopyParamsUniform.valid()) {
        resources.destroyUniform(m_shadowCopyParamsUniform);
        m_shadowCopyParamsUniform = {};
    }
    if (m_shadowMomentSlotUniform.valid()) {
        resources.destroyUniform(m_shadowMomentSlotUniform);
        m_shadowMomentSlotUniform = {};
    }
    if (m_shadowMomentParamsUniform.valid()) {
        resources.destroyUniform(m_shadowMomentParamsUniform);
        m_shadowMomentParamsUniform = {};
    }
    if (m_shadowMomentDepthSampler.valid()) {
        resources.destroyUniform(m_shadowMomentDepthSampler);
        m_shadowMomentDepthSampler = {};
    }
    if (m_shadowMomentSampler.valid()) {
        resources.destroyUniform(m_shadowMomentSampler);
        m_shadowMomentSampler = {};
    }
    if (m_shadowMomentSatFallback.valid()) {
        resources.destroyTexture(m_shadowMomentSatFallback);
        m_shadowMomentSatFallback = {};
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
    if (m_fogSourceSampler.valid()) {
        resources.destroyUniform(m_fogSourceSampler);
        m_fogSourceSampler = {};
    }
    if (m_fogHistorySampler.valid()) {
        resources.destroyUniform(m_fogHistorySampler);
        m_fogHistorySampler = {};
    }
    if (m_fogLowResSampler.valid()) {
        resources.destroyUniform(m_fogLowResSampler);
        m_fogLowResSampler = {};
    }
    for (gfx::UniformHandle& sampler : m_fogMaskSamplers) {
        if (sampler.valid()) {
            resources.destroyUniform(sampler);
        }
        sampler = {};
    }
    for (gfx::UniformHandle& sampler : m_gbufferSamplers) {
        if (sampler.valid()) {
            resources.destroyUniform(sampler);
        }
        sampler = {};
    }
    for (gfx::TextureHandle& texture : m_decalFallbackTextures) {
        if (texture.valid()) {
            resources.destroyTexture(texture);
        }
        texture = {};
    }
    for (gfx::UniformHandle& sampler : m_decalSamplers) {
        if (sampler.valid()) {
            resources.destroyUniform(sampler);
        }
        sampler = {};
    }
    if (m_decalAlbedoSampler.valid()) {
        resources.destroyUniform(m_decalAlbedoSampler);
        m_decalAlbedoSampler = {};
    }
    if (m_decalNormalSampler.valid()) {
        resources.destroyUniform(m_decalNormalSampler);
        m_decalNormalSampler = {};
    }
    if (m_decalMaskSampler.valid()) {
        resources.destroyUniform(m_decalMaskSampler);
        m_decalMaskSampler = {};
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
    m_clusterBinder.shutdown(resources);
    m_ssaoBinder.shutdown(resources);
    m_ssrBinder.shutdown(resources);
    m_ssgiBinder.shutdown(resources);
    m_decalBinder.shutdown(resources);
    m_materialBinder.shutdown(resources);
    m_objectBinder.shutdown(resources);
    m_prepassBinder.shutdown(resources);
    m_environmentBinder.shutdown(resources);
    m_probeBinder.shutdown(resources);
    m_debugBinder.shutdown(resources);
    m_fogBinder.shutdown(resources);
    m_contactBinder.shutdown(resources);
    m_opaqueDrawList.clear();
    m_forwardOpaqueDrawList.clear();
    m_bakedGIDrawList.clear();
    m_transparentDrawList.clear();
    m_shadowDrawList.clear();
    m_shadowStaticDrawList.clear();
    m_shadowDynamicDrawList.clear();
    m_prepassDrawList.clear();
    m_motionVectorDrawList.clear();
    m_wireframeDrawList.clear();
    m_resources = nullptr;
}

} // namespace hbg::render
