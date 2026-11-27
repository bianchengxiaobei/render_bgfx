#include "render/terrain/gpu_tile_storage.h"

#include "core/log.h"

#include <algorithm>
#include <limits>

namespace hbg::render {

bool GpuTileStorage::init(gfx::ResourceManager& resources, uint32_t tileSize, uint32_t capacity,
                          gfx::TextureFormat format, const std::string& name, uint64_t flags,
                          uint8_t mipCount, bool renderTarget) {
    shutdown(resources);
    if (tileSize == 0 || capacity == 0 || capacity > std::numeric_limits<uint16_t>::max() ||
        tileSize > std::numeric_limits<uint16_t>::max()) {
        HBG_LOGE("gpu tile storage '{}' received invalid dimensions", name);
        return false;
    }

    gfx::TextureDesc desc;
    desc.width = static_cast<uint16_t>(tileSize);
    desc.height = static_cast<uint16_t>(tileSize);
    desc.layers = static_cast<uint16_t>(capacity);
    desc.mipCount = std::max<uint8_t>(1, mipCount);
    desc.format = format;
    desc.flags = flags;
    desc.renderTarget = renderTarget;
    desc.name = name;
    m_texture = resources.createTexture(desc);
    if (!m_texture.valid()) {
        HBG_LOGE("gpu tile storage '{}' failed to create {}x{}x{} texture", name, tileSize, tileSize, capacity);
        return false;
    }

    m_resources = &resources;
    m_tileSize = tileSize;
    m_capacity = capacity;
    m_mipCount = desc.mipCount;
    HBG_LOGI("gpu tile storage: '{}' size={} layers={} mips={} bytes={}", name, m_tileSize, m_capacity,
             m_mipCount, gpuBytes());
    return true;
}

void GpuTileStorage::shutdown(gfx::ResourceManager& resources) {
    if (m_texture.valid()) {
        resources.destroyTexture(m_texture);
        m_texture = {};
    }
    m_resources = nullptr;
    m_tileSize = 0;
    m_capacity = 0;
    m_mipCount = 1;
}

bool GpuTileStorage::upload(uint32_t layer, const void* data, uint32_t size, uint32_t pitch) {
    if (!m_texture.valid() || m_resources == nullptr || layer >= m_capacity || data == nullptr || pitch == 0 ||
        size < pitch * m_tileSize) {
        return false;
    }
    return m_resources->updateTexture(m_texture, data, size, static_cast<uint16_t>(m_tileSize),
                                      static_cast<uint16_t>(m_tileSize), static_cast<uint16_t>(pitch),
                                      static_cast<uint16_t>(layer));
}

bool GpuTileStorage::upload(uint32_t layer, uint8_t mip, const void* data, uint32_t size, uint32_t pitch,
                            uint32_t width, uint32_t height) {
    if (!m_texture.valid() || m_resources == nullptr || layer >= m_capacity || mip >= m_mipCount ||
        data == nullptr || pitch == 0 || width == 0 || height == 0 || width > m_tileSize ||
        height > m_tileSize || size < pitch * height) {
        return false;
    }
    return m_resources->updateTexture(m_texture, data, size, static_cast<uint16_t>(width),
                                      static_cast<uint16_t>(height), static_cast<uint16_t>(pitch),
                                      static_cast<uint16_t>(layer), mip);
}

uint64_t GpuTileStorage::gpuBytes() const {
    if (!m_texture.valid() || m_resources == nullptr) {
        return 0;
    }
    const gfx::Texture* texture = m_resources->texture(m_texture);
    return texture != nullptr ? texture->sizeBytes() : 0;
}

} // namespace hbg::render
