#pragma once

#include "render/lighting/gpu_light_data.h"
#include "render/scene/render_scene.h"

#include <vector>

namespace hbg::render {

class LightBuilder {
  public:
    void build(const RenderScene& scene, const Vec3& cameraPosition, const Vec3& cameraForward, float cameraFar);
    const GpuLightData& data() const { return m_data; }
    const Light* shadowCaster() const { return m_shadowCaster; }
    const std::vector<const Light*>& selectedLights() const { return m_selectedLights; }
    void setShadowIndex(uint32_t lightIndex, int32_t shadowIndex);
    void setShadowedCount(uint32_t count) { m_data.shadowedCount = count; }

  private:
    GpuLightData m_data;
    Light m_fallbackLight;
    const Light* m_shadowCaster = nullptr;
    std::vector<const Light*> m_selectedLights;
};

} // namespace hbg::render
