#pragma once

#include "gfx/framebuffer_cache.h"
#include "gfx/resource_manager.h"
#include "gfx/texture.h"
#include "render/camera/view_state.h"
#include "render/lighting/light_builder.h"
#include "render/scene/render_scene.h"

#include <array>
#include <cstdint>
#include <vector>

namespace hbg::gfx {
class Encoder;
}

namespace hbg::render {

constexpr uint32_t kDirectionalCascadeCount = 4;
constexpr uint32_t kMaxShadowRequests = 32;
constexpr uint32_t kShadowContextTexels = 8;
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

    bool init(gfx::ResourceManager& resources, uint16_t size = 2048);
    void shutdown(gfx::ResourceManager& resources);
    void invalidate();

    bool update(const RenderScene& scene, const ViewConstants& view, const Camera& camera, LightBuilder& lightBuilder);
    bool valid() const { return !m_requests.empty(); }
    const std::vector<ShadowRequest>& requests() const { return m_requests; }
    const GpuShadowContextData& contextData() const { return m_contextData; }
    Vec4 cascadeSplits() const { return m_cascadeSplits; }
    uint32_t directionalRequestCount() const { return m_directionalRequestCount; }
    uint32_t punctualRequestCount() const { return m_punctualRequestCount; }
    uint32_t shadowedLightCount() const { return m_shadowedLightCount; }

    gfx::TextureHandle directionalTexture() const { return m_directionalTexture; }
    gfx::TextureHandle punctualTexture() const { return m_punctualTexture; }
    uint16_t directionalAtlasSize() const { return m_directionalAtlasSize; }
    uint16_t punctualAtlasSize() const { return m_punctualAtlasSize; }
    gfx::TextureHandle contextTexture() const { return m_contextTexture; }

    void beginPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                   const ShadowRequest& request) const;

  private:
    bool buildDirectionalRequests(const RenderScene& scene, const ViewConstants& view, const Camera& camera,
                                  const Light& light, uint32_t lightIndex, LightBuilder& lightBuilder,
                                  bool homogeneousDepth, bool originBottomLeft);
    bool buildPunctualRequest(const Light& light, uint32_t lightIndex, LightBuilder& lightBuilder,
                              bool homogeneousDepth, bool originBottomLeft);
    bool packPunctual(uint16_t width, uint16_t height, ShadowRequest& request);
    void buildContextData();
    void uploadContext();
    void resetPacking();

    gfx::ResourceManager* m_resources = nullptr;
    gfx::TextureHandle m_directionalTexture;
    gfx::TextureHandle m_punctualTexture;
    gfx::TextureHandle m_contextTexture;
    std::vector<ShadowRequest> m_requests;
    GpuShadowContextData m_contextData;
    Vec4 m_cascadeSplits{};
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
