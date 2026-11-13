#pragma once

#include "core/handle.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>

namespace hbg::gfx {

enum class TextureFormat : uint8_t {
    RGBA8,
    BGRA8,
    RGBA16F,
    RGBA32F,
    RG16F,
    RG32F,
    R8,
    RG8,
    R32U,
    R32F,
    RG11B10F,
    D16,
    D24S8,
    D32F,
    BC1,
    BC3,
    BC5,
    BC7,
    Count,
};

bgfx::TextureFormat::Enum toBgfx(TextureFormat format);
bool isDepthFormat(TextureFormat format);
const char* textureFormatName(TextureFormat format);

struct TextureTag {};
using TextureHandle = Handle<TextureTag>;

struct TextureDesc {
    uint16_t width = 1;
    uint16_t height = 1;
    uint16_t layers = 1;
    uint8_t mipCount = 1;
    uint8_t msaaSamples = 1;
    TextureFormat format = TextureFormat::RGBA8;
    uint64_t flags = BGFX_TEXTURE_NONE;
    bool renderTarget = false;
    bool depthStencil = false;
    bool computeWrite = false;
    bool cubeMap = false;
    const void* data = nullptr;
    uint32_t dataSize = 0;
    std::string name;
};

class Texture {
  public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    bool create(const TextureDesc& desc);
    bool update(const void* data, uint32_t size, uint16_t width, uint16_t height, uint16_t pitch, uint16_t layer = 0,
                uint8_t mip = 0, uint16_t x = 0, uint16_t y = 0);
    void destroy();

    bool valid() const;
    bgfx::TextureHandle native() const { return m_native; }
    bgfx::TextureHandle takeNative();

    const TextureDesc& desc() const { return m_desc; }
    const bgfx::TextureInfo& info() const { return m_info; }
    uint32_t sizeBytes() const { return m_info.storageSize; }
    bool isDepth() const { return m_desc.depthStencil || isDepthFormat(m_desc.format); }

  private:
    bgfx::TextureHandle m_native{bgfx::kInvalidHandle};
    TextureDesc m_desc{};
    bgfx::TextureInfo m_info{};
};

} // namespace hbg::gfx
