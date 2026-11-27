#pragma once

#include "gfx/resource_manager.h"
#include "gfx/texture.h"

#include <cstdint>
#include <string>

namespace hbg::render {

class GpuTileStorage {
  public:
    bool init(gfx::ResourceManager& resources, uint32_t tileSize, uint32_t capacity, gfx::TextureFormat format,
              const std::string& name, uint64_t flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT,
              uint8_t mipCount = 1, bool renderTarget = false);
    void shutdown(gfx::ResourceManager& resources);

    bool upload(uint32_t layer, const void* data, uint32_t size, uint32_t pitch);
    bool upload(uint32_t layer, uint8_t mip, const void* data, uint32_t size, uint32_t pitch, uint32_t width,
                uint32_t height);

    bool valid() const { return m_texture.valid(); }
    gfx::TextureHandle texture() const { return m_texture; }
    uint32_t tileSize() const { return m_tileSize; }
    uint32_t capacity() const { return m_capacity; }
    uint8_t mipCount() const { return m_mipCount; }
    uint64_t gpuBytes() const;

  private:
    gfx::ResourceManager* m_resources = nullptr;
    gfx::TextureHandle m_texture;
    uint32_t m_tileSize = 0;
    uint32_t m_capacity = 0;
    uint8_t m_mipCount = 1;
};

} // namespace hbg::render
