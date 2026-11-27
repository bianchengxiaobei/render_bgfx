#pragma once

#include "gfx/encoder.h"
#include "gfx/resource_manager.h"
#include "render/water/ocean_fft.h"

#include <array>
#include <cstdint>
#include <filesystem>

namespace hbg::render {

class OceanFftGpu {
  public:
    bool init(gfx::ResourceManager& resources, const OceanFFT& ocean, const std::filesystem::path& shaderRoot);
    void shutdown(gfx::ResourceManager& resources);

    bool valid() const { return m_valid; }
    gfx::TextureHandle texture() const { return m_finalTexture; }
    uint32_t updateCount() const { return m_updateCount; }
    void invalidateDisplacement() { m_hasView = false; }

    bool update(gfx::ResourceManager& resources, const OceanFFT& ocean, gfx::BufferHandle vertexBuffer,
                uint32_t vertexCount, int gridWidth, int gridHeight, float baseHeight, const Mat4& invViewProj,
                const Vec3& cameraPosition, const Vec2& uvScale, const Vec2& uvOrigin, float timeSeconds,
                float sphereRadius = 0.0f);

  private:
    enum class Program : uint8_t {
        Prepare,
        Horizontal,
        Vertical,
        Finalize,
        Mip,
        Displace,
        Count,
    };

    bool createPrograms(gfx::ResourceManager& resources, const std::filesystem::path& shaderRoot);
    bool createTextures(gfx::ResourceManager& resources, const OceanFFT& ocean);
    bool createUniforms(gfx::ResourceManager& resources);
    bool uploadSpectrum(gfx::ResourceManager& resources, const OceanFFT& ocean);
    void simulate(float timeSeconds, const OceanFFT& ocean);
    void displace(gfx::ResourceManager& resources, const OceanFFT& ocean, gfx::BufferHandle vertexBuffer,
                  uint32_t vertexCount, int gridWidth, int gridHeight, float baseHeight, const Mat4& invViewProj,
                  const Vec3& cameraPosition, const Vec2& uvScale, const Vec2& uvOrigin, float sphereRadius);
    void destroyTextures(gfx::ResourceManager& resources);
    void destroyUniforms(gfx::ResourceManager& resources);
    void destroyPrograms(gfx::ResourceManager& resources);

    gfx::ResourceManager* m_resources = nullptr;
    std::array<gfx::ProgramHandle, static_cast<std::size_t>(Program::Count)> m_programs{};
    gfx::TextureHandle m_spectrumTexture;
    gfx::TextureHandle m_pingTexture;
    gfx::TextureHandle m_pongTexture;
    gfx::TextureHandle m_sourceTexture;
    gfx::TextureHandle m_finalTexture;
    gfx::UniformHandle m_timeUniform;
    gfx::UniformHandle m_gridSizesUniform;
    gfx::UniformHandle m_mipUniform;
    gfx::UniformHandle m_invViewProjUniform;
    gfx::UniformHandle m_cameraUniform;
    gfx::UniformHandle m_gridUniform;
    gfx::UniformHandle m_displaceUniform;
    gfx::UniformHandle m_uvUniform;
    gfx::UniformHandle m_sphereUniform;
    gfx::UniformHandle m_inputSampler;
    gfx::UniformHandle m_spectrumSampler;
    gfx::UniformHandle m_finalSampler;
    uint32_t m_resolution = 0;
    uint32_t m_mipCount = 0;
    uint32_t m_updateCount = 0;
    Mat4 m_lastInvViewProj{};
    Vec3 m_lastCamera{};
    float m_lastTime = -1.0f;
    int m_lastGridWidth = 0;
    int m_lastGridHeight = 0;
    uint32_t m_lastVertexCount = 0;
    float m_lastSphereRadius = 0.0f;
    bool m_valid = false;
    bool m_hasTime = false;
    bool m_hasView = false;
};

} // namespace hbg::render
