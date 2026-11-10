#pragma once

#include "gfx/generated/uniforms.h"
#include "gfx/resource_manager.h"
#include "gfx/texture.h"

#include <filesystem>
#include <string>

namespace hbg::render {

class SkyManager {
  public:
    SkyManager() = default;
    ~SkyManager() = default;

    SkyManager(const SkyManager&) = delete;
    SkyManager& operator=(const SkyManager&) = delete;

    bool load(gfx::ResourceManager& resources, const std::filesystem::path& path, float intensity,
              float rotationDegrees, std::string* error = nullptr);
    void shutdown(gfx::ResourceManager& resources);

    bool valid() const { return m_texture.valid() && m_skyTexture.valid(); }
    gfx::TextureHandle texture() const { return m_texture; }
    gfx::TextureHandle skyTexture() const { return m_skyTexture; }
    const gfx::generated::EnvironmentUniforms& uniforms() const { return m_uniforms; }
    float intensity() const { return m_intensity; }
    float maxMip() const { return m_maxMip; }

  private:
    gfx::TextureHandle m_texture;
    gfx::TextureHandle m_skyTexture;
    gfx::generated::EnvironmentUniforms m_uniforms{};
    float m_intensity = 1.0f;
    float m_maxMip = 0.0f;
};

} // namespace hbg::render
