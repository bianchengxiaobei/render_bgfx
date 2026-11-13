#pragma once

#include "gfx/framebuffer_cache.h"
#include "gfx/resource_manager.h"
#include "gfx/texture.h"
#include "render/camera/view_state.h"
#include "render/lighting/light_builder.h"
#include "render/scene/render_scene.h"
#include "render/shadows/shadow_settings.h"

#include <array>
#include <cstdint>
#include <vector>

namespace hbg::gfx {
class Encoder;
}

namespace hbg::render {

constexpr uint32_t kDirectionalCascadeCount = kMaxDirectionalCascades;
constexpr uint32_t kShadowContextTexels = 10;
constexpr uint32_t kPointShadowResolution = 256;
constexpr uint32_t kSpotShadowResolution = 512;

enum class ShadowRequestType : uint32_t {
    Directional = 0,
    Spot = 1,
    PointFace = 2,
};

enum class ShadowAtlasType : uint8_t {
    Directional,
    Punctual,
};

struct ShadowRequest {
    bool enabled = false;
    ShadowRequestType type = ShadowRequestType::Directional;
    ShadowAtlasType atlas = ShadowAtlasType::Directional;
    uint32_t requestIndex = 0;
    uint32_t lightIndex = 0;
    uint32_t faceIndex = 0;
    uint16_t atlasSize = 2048;
    uint16_t viewportX = 0;
    uint16_t viewportY = 0;
    uint16_t viewportWidth = 0;
    uint16_t viewportHeight = 0;
    Mat4 view = Mat4::identity();
    Mat4 projection = Mat4::identity();
    Mat4 viewProj = Mat4::identity();
    Mat4 worldToShadow = Mat4::identity();
    Vec3 lightPosition{0.0f, 0.0f, 0.0f};
    Vec3 lightDirection{0.0f, 1.0f, 0.0f};
    float depthBias = 0.0015f;
    float normalBias = 0.01f;
    float softness = 0.0f;
    float nearPlane = 0.0f;
    float farPlane = 1.0f;
    float worldTexelSize = 0.0f;
    float slopeBias = 0.0f;
    float fadeScale = 0.0f;
    float fadeBias = 0.0f;
    uint32_t blockerSamples = 16;
    uint32_t filterSamples = 24;
    float minFilterSize = 0.01f;
    ShadowFilter filter = ShadowFilter::Pcf5x5;
    bool isCached = false;
    uint32_t cascadeIndex = 0;
    std::array<Vec4, 6> frustumPlanes{};
};

struct GpuShadowContextData {
    std::array<Vec4, kMaxShadowRequests * kShadowContextTexels> texels{};
    uint32_t requestCount = 0;
};

class ShadowManager {
  public:
    ShadowManager() = default;
    ~ShadowManager() = default;

    ShadowManager(const ShadowManager&) = delete;
    ShadowManager& operator=(const ShadowManager&) = delete;

    bool init(gfx::ResourceManager& resources, const ShadowSettings& settings = {});
    bool setSettings(const ShadowSettings& settings);
    void shutdown(gfx::ResourceManager& resources);
    void invalidate();

    bool update(const RenderScene& scene, const ViewConstants& view, const Camera& camera, LightBuilder& lightBuilder);
    bool valid() const { return !m_requests.empty(); }
    const std::vector<ShadowRequest>& requests() const { return m_requests; }
    const GpuShadowContextData& contextData() const { return m_contextData; }
    const ShadowSettings& settings() const { return m_settings; }
    Vec4 cascadeSplits() const { return m_cascadeSplits; }
    uint32_t directionalRequestCount() const { return m_directionalRequestCount; }
    uint32_t punctualRequestCount() const { return m_punctualRequestCount; }
    uint32_t shadowedLightCount() const { return m_shadowedLightCount; }

    gfx::TextureHandle directionalTexture() const { return m_directionalTexture; }
    gfx::TextureHandle punctualTexture() const { return m_punctualTexture; }
    gfx::TextureHandle cachedPunctualTexture() const { return m_cachedPunctualTexture; }
    gfx::TextureHandle activePunctualTexture() const {
        return m_cacheEligible && m_cachedPunctualTexture.valid() ? m_cachedPunctualTexture : m_punctualTexture;
    }
    uint16_t directionalAtlasSize() const { return m_directionalAtlasSize; }
    uint16_t punctualAtlasSize() const { return m_punctualAtlasSize; }
    gfx::TextureHandle contextTexture() const { return m_contextTexture; }
    bool cacheActive() const { return m_cacheEligible; }
    uint32_t cachedRequestCount() const { return m_cachedRequestCount; }
    void setCacheSuppressed(bool suppressed) {
        if (m_cacheSuppressed != suppressed) {
            m_cacheSuppressed = suppressed;
            m_cacheValid = false;
            m_cacheHash = 0;
        }
    }

    void beginPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                   const ShadowRequest& request) const;
    void beginCachedPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer) const;

  private:
    bool buildDirectionalRequests(const RenderScene& scene, const ViewConstants& view, const Camera& camera,
                                  const Light& light, const LightShadowSettings& shadow, uint32_t lightIndex,
                                  LightBuilder& lightBuilder, bool homogeneousDepth, bool originBottomLeft);
    bool buildPunctualRequest(const Light& light, const LightShadowSettings& shadow, uint32_t lightIndex,
                              LightBuilder& lightBuilder, bool homogeneousDepth, bool originBottomLeft,
                              float distanceToCamera);
    bool packPunctual(uint16_t width, uint16_t height, ShadowRequest& request);
    bool createTextures();
    void destroyTextures();
    void buildContextData();
    void uploadContext();
    void resetPacking();
    void resetFrameRequests();
    uint64_t buildCacheHash(const RenderScene& scene, const LightBuilder& lightBuilder) const;

    gfx::ResourceManager* m_resources = nullptr;
    gfx::TextureHandle m_directionalTexture;
    gfx::TextureHandle m_punctualTexture;
    gfx::TextureHandle m_cachedPunctualTexture;
    gfx::TextureHandle m_contextTexture;
    std::vector<ShadowRequest> m_requests;
    GpuShadowContextData m_contextData;
    ShadowSettings m_settings;
    Vec4 m_cascadeSplits{};
    uint64_t m_cacheHash = 0;
    uint32_t m_cachedRequestCount = 0;
    bool m_cacheEligible = false;
    bool m_cacheValid = false;
    bool m_cacheSuppressed = false;
    uint16_t m_directionalAtlasSize = 2048;
    uint16_t m_punctualAtlasSize = 2048;
    uint16_t m_punctualCursorX = 0;
    uint16_t m_punctualCursorY = 0;
    uint16_t m_punctualRowHeight = 0;
    uint32_t m_directionalRequestCount = 0;
    uint32_t m_punctualRequestCount = 0;
    uint32_t m_shadowedLightCount = 0;
};

} // namespace hbg::render
