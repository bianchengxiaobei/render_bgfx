#pragma once

#include "render/lighting/gpu_light_data.h"
#include "render/scene/render_scene.h"
#include "render/shadows/shadow_settings.h"

#include <memory>
#include <vector>

namespace hbg::render {

class LightBuilder {
  public:
    void build(const RenderScene& scene, const Vec3& cameraPosition, const Vec3& cameraForward, float cameraFar,
               bool contactShadowsEnabled = false, bool directionalOnly = false,
               const ShadowSettings* shadowSettings = nullptr,
               uint32_t lightCapacity = kLightListDefaultCapacity, bool supportLightLayers = false);
    const GpuLightData& data() const { return *m_data; }
    const Light* shadowCaster() const { return m_shadowCaster; }
    const std::vector<const Light*>& selectedLights() const { return m_selectedLights; }
    void setShadowIndex(uint32_t lightIndex, int32_t shadowIndex);
    void setShadowDimmer(uint32_t lightIndex, float dimmer);
    void setCookieAtlas(uint32_t lightIndex, uint32_t tileIndex, Vec4 scaleOffset);
    void setShadowedCount(uint32_t count) { m_data->shadowedCount = count; }

  private:
    std::unique_ptr<GpuLightData> m_data = std::make_unique<GpuLightData>();
    Light m_fallbackLight;
    const Light* m_shadowCaster = nullptr;
    std::vector<const Light*> m_selectedLights;
};

} // namespace hbg::render
