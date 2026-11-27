#pragma once

#include "gfx/encoder.h"
#include "gfx/resource_manager.h"
#include "render/river/river_particles.h"

#include <cstdint>
#include <filesystem>

namespace hbg::render {

class RiverParticleGpu {
  public:
    bool init(gfx::ResourceManager& resources, const RiverParticleSystem& river, gfx::TextureHandle waveBaseTexture,
              const std::filesystem::path& shaderRoot);
    bool update(gfx::ResourceManager& resources, const RiverParticleSystem& river, const Mat4& invViewProj,
                const Vec3& cameraPosition, uint16_t width, uint16_t height);
    void submit(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                gfx::TextureHandle depthTexture);
    void shutdown(gfx::ResourceManager& resources);

    bool valid() const { return m_valid; }
    bool ready() const { return m_valid && m_ready; }
    gfx::TextureHandle normalTexture() const { return m_normalTexture; }
    uint32_t targetWidth() const { return m_targetWidth; }
    uint32_t targetHeight() const { return m_targetHeight; }

  private:
    bool createProgram(gfx::ResourceManager& resources, const std::filesystem::path& shaderRoot);
    bool createGridBuffer(gfx::ResourceManager& resources, uint32_t vertexCapacity);
    bool ensureGridBuffer(gfx::ResourceManager& resources, uint32_t vertexCount);
    bool createParameterTexture(gfx::ResourceManager& resources);
    bool ensureSpriteTableTexture(gfx::ResourceManager& resources, uint32_t gridWidth, uint32_t gridHeight);
    bool createNormalTarget(gfx::ResourceManager& resources, uint16_t width, uint16_t height);
    bool ensureTarget(gfx::ResourceManager& resources, uint16_t width, uint16_t height);
    bool createUniforms(gfx::ResourceManager& resources);
    bool uploadTables(gfx::ResourceManager& resources, const RiverParticleSystem& river);
    void destroySpriteTableTexture(gfx::ResourceManager& resources);

    gfx::ResourceManager* m_resources = nullptr;
    gfx::ProgramHandle m_program;
    gfx::BufferHandle m_gridBuffer;
    uint32_t m_gridVertexCapacity = 0;
    uint32_t m_gridSpanCount = 0;
    gfx::TextureHandle m_normalTexture;
    gfx::TextureHandle m_waveBaseTexture;
    gfx::TextureHandle m_parameterTexture;
    gfx::TextureHandle m_spriteTableTexture;
    gfx::UniformHandle m_invViewProjUniform;
    gfx::UniformHandle m_cameraUniform;
    gfx::UniformHandle m_paramsUniform;
    gfx::UniformHandle m_gridUniform;
    gfx::UniformHandle m_waveSampler;
    gfx::UniformHandle m_parameterSampler;
    gfx::UniformHandle m_spriteTableSampler;
    gfx::UniformHandle m_depthSampler;
    Mat4 m_invViewProj = Mat4::identity();
    Vec4 m_camera{};
    Vec4 m_params0{};
    Vec4 m_gridParams{};
    bool m_ready = false;
    bool m_gridLogged = false;
    uint32_t m_targetWidth = 0;
    uint32_t m_targetHeight = 0;
    uint32_t m_capacity = 0;
    uint32_t m_vertexCount = 0;
    uint32_t m_spriteGridWidth = 0;
    uint32_t m_spriteGridHeight = 0;
    bool m_valid = false;
};

} // namespace hbg::render
