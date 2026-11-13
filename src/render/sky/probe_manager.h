#pragma once

#include "asset/texture_loader.h"
#include "gfx/generated/uniforms.h"
#include "gfx/resource_manager.h"
#include "gfx/texture.h"
#include "render/scene/reflection_probe.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace hbg::render {

class ProbeManager {
  public:
    ProbeManager() = default;
    ~ProbeManager() = default;

    ProbeManager(const ProbeManager&) = delete;
    ProbeManager& operator=(const ProbeManager&) = delete;

    bool load(gfx::ResourceManager& resources, const std::vector<ReflectionProbe>& probes,
              const std::vector<asset::ImageAsset>& images, std::string* error = nullptr);
    void shutdown(gfx::ResourceManager& resources);

    void select(const Vec3& cameraPosition, bool enabled, bool skyFallback);

    bool valid() const { return !m_probes.empty(); }
    std::size_t loadedCount() const { return m_probes.size(); }
    std::size_t selectedCount() const;
    gfx::TextureHandle texture(std::size_t index) const;
    const gfx::generated::ProbeUniforms& uniforms() const { return m_uniforms; }

  private:
    struct GpuProbe {
        ReflectionProbe descriptor;
        gfx::TextureHandle texture;
        float maxMip = 0.0f;
    };

    std::vector<GpuProbe> m_probes;
    std::array<int, kMaxActiveReflectionProbes> m_selected{-1, -1};
    std::array<gfx::TextureHandle, kMaxActiveReflectionProbes> m_textures{};
    gfx::generated::ProbeUniforms m_uniforms{};
};

} // namespace hbg::render
