#include "gfx/texture.h"

#include "core/log.h"

#include <utility>

namespace hbg::gfx {

bgfx::TextureFormat::Enum toBgfx(TextureFormat format) {
    switch (format) {
    case TextureFormat::RGBA8:
        return bgfx::TextureFormat::RGBA8;
    case TextureFormat::BGRA8:
        return bgfx::TextureFormat::BGRA8;
    case TextureFormat::RGBA16F:
        return bgfx::TextureFormat::RGBA16F;
    case TextureFormat::RGBA32F:
        return bgfx::TextureFormat::RGBA32F;
    case TextureFormat::RG16F:
        return bgfx::TextureFormat::RG16F;
    case TextureFormat::RG32F:
        return bgfx::TextureFormat::RG32F;
    case TextureFormat::R8:
        return bgfx::TextureFormat::R8;
    case TextureFormat::RG8:
        return bgfx::TextureFormat::RG8;
    case TextureFormat::R32U:
        return bgfx::TextureFormat::R32U;
    case TextureFormat::R32F:
        return bgfx::TextureFormat::R32F;
    case TextureFormat::RG11B10F:
        return bgfx::TextureFormat::RG11B10F;
    case TextureFormat::D16:
        return bgfx::TextureFormat::D16;
    case TextureFormat::D24S8:
        return bgfx::TextureFormat::D24S8;
    case TextureFormat::D32F:
        return bgfx::TextureFormat::D32F;
    case TextureFormat::BC1:
        return bgfx::TextureFormat::BC1;
    case TextureFormat::BC3:
        return bgfx::TextureFormat::BC3;
    case TextureFormat::BC5:
        return bgfx::TextureFormat::BC5;
    case TextureFormat::BC7:
        return bgfx::TextureFormat::BC7;
    case TextureFormat::Count:
        break;
    }
    return bgfx::TextureFormat::Count;
}

bool isDepthFormat(TextureFormat format) {
    switch (format) {
    case TextureFormat::D16:
    case TextureFormat::D24S8:
    case TextureFormat::D32F:
        return true;
    default:
        return false;
    }
}

const char* textureFormatName(TextureFormat format) {
    switch (format) {
    case TextureFormat::RGBA8:
        return "RGBA8";
    case TextureFormat::BGRA8:
        return "BGRA8";
    case TextureFormat::RGBA16F:
        return "RGBA16F";
    case TextureFormat::RGBA32F:
        return "RGBA32F";
    case TextureFormat::RG16F:
        return "RG16F";
    case TextureFormat::RG32F:
        return "RG32F";
    case TextureFormat::R8:
        return "R8";
    case TextureFormat::RG8:
        return "RG8";
    case TextureFormat::R32U:
        return "R32U";
    case TextureFormat::R32F:
        return "R32F";
    case TextureFormat::RG11B10F:
        return "RG11B10F";
    case TextureFormat::D16:
        return "D16";
    case TextureFormat::D24S8:
        return "D24S8";
    case TextureFormat::D32F:
        return "D32F";
    case TextureFormat::BC1:
        return "BC1";
    case TextureFormat::BC3:
        return "BC3";
    case TextureFormat::BC5:
        return "BC5";
    case TextureFormat::BC7:
        return "BC7";
    case TextureFormat::Count:
        return "Count";
    }
    return "Unknown";
}

Texture::~Texture() {
    destroy();
}

Texture::Texture(Texture&& other) noexcept
    : m_native(other.m_native), m_desc(std::move(other.m_desc)), m_info(other.m_info) {
    other.m_native = bgfx::TextureHandle{bgfx::kInvalidHandle};
    other.m_info = {};
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        destroy();
        m_native = other.m_native;
        m_desc = std::move(other.m_desc);
        m_info = other.m_info;
        other.m_native = bgfx::TextureHandle{bgfx::kInvalidHandle};
        other.m_info = {};
    }
    return *this;
}

bool Texture::create(const TextureDesc& desc) {
    if (valid()) {
        HBG_LOGE("Texture::create called on an existing texture '{}'", m_desc.name);
        return false;
    }
    if (desc.width == 0 || desc.height == 0 || desc.layers == 0) {
        HBG_LOGE("Texture::create requires non-zero dimensions");
        return false;
    }
    if (desc.cubeMap && desc.width != desc.height) {
        HBG_LOGE("Texture::create cube map requires width == height ({}x{})", desc.width, desc.height);
        return false;
    }
    if (desc.data != nullptr && desc.dataSize == 0) {
        HBG_LOGE("Texture::create received data without a size");
        return false;
    }

    const bgfx::TextureFormat::Enum format = toBgfx(desc.format);
    if (format == bgfx::TextureFormat::Count) {
        HBG_LOGE("Texture::create received an unsupported format");
        return false;
    }

    const bool hasMips = desc.mipCount != 1;
    uint64_t flags = desc.flags;
    if (desc.mipCount > 1) {
        flags |= BGFX_TEXTURE_MIP_COUNT(desc.mipCount);
    }
    if (desc.renderTarget || desc.depthStencil) {
        flags &= ~BGFX_TEXTURE_RT_MASK;
        if (desc.msaaSamples >= 8) {
            flags |= BGFX_TEXTURE_RT_MSAA_X8;
        } else if (desc.msaaSamples >= 4) {
            flags |= BGFX_TEXTURE_RT_MSAA_X4;
        } else if (desc.msaaSamples >= 2) {
            flags |= BGFX_TEXTURE_RT_MSAA_X2;
        } else {
            flags |= BGFX_TEXTURE_RT;
        }
    }

    const bgfx::Memory* memory = nullptr;
    if (desc.data != nullptr) {
        memory = bgfx::copy(desc.data, desc.dataSize);
    }

    if (desc.cubeMap) {
        m_native = bgfx::createTextureCube(desc.width, hasMips, desc.layers, format, flags, memory);
    } else {
        m_native = bgfx::createTexture2D(desc.width, desc.height, hasMips, desc.layers, format, flags, memory);
    }

    if (!valid()) {
        HBG_LOGE("failed to create texture '{}' ({}x{}, {}, mips={}, layers={})", desc.name, desc.width, desc.height,
                 textureFormatName(desc.format), desc.mipCount, desc.layers);
        return false;
    }

    m_desc = desc;
    m_desc.flags = flags;
    m_desc.data = nullptr;
    m_desc.dataSize = 0;
    bgfx::calcTextureSize(m_info, desc.width, desc.height, 1, desc.cubeMap, hasMips, desc.layers, format);
    if (!desc.name.empty()) {
        bgfx::setName(m_native, desc.name.c_str());
    }

    HBG_LOGD("created texture '{}' ({}x{}, {}, {} bytes)", desc.name, desc.width, desc.height,
             textureFormatName(desc.format), m_info.storageSize);
    return true;
}

bool Texture::update(const void* data, uint32_t size, uint16_t width, uint16_t height, uint16_t pitch, uint16_t layer,
                     uint8_t mip, uint16_t x, uint16_t y) {
    if (!valid() || data == nullptr || width == 0 || height == 0 || pitch == 0) {
        HBG_LOGE("Texture::update requires a valid texture and non-zero data");
        return false;
    }
    if (layer >= m_desc.layers || mip >= m_desc.mipCount) {
        HBG_LOGE("Texture::update received an invalid layer/mip");
        return false;
    }
    if (x + width > m_desc.width || y + height > m_desc.height) {
        HBG_LOGE("Texture::update region is outside texture '{}'", m_desc.name);
        return false;
    }
    if (static_cast<uint64_t>(pitch) * height > size) {
        HBG_LOGE("Texture::update data is too small for {}x{} (pitch={})", width, height, pitch);
        return false;
    }

    const bgfx::Memory* memory = bgfx::copy(data, size);
    bgfx::updateTexture2D(m_native, layer, mip, x, y, width, height, memory, pitch);
    return true;
}

void Texture::destroy() {
    if (valid()) {
        bgfx::destroy(m_native);
        m_native = bgfx::TextureHandle{bgfx::kInvalidHandle};
        m_info = {};
    }
}

bool Texture::valid() const {
    return bgfx::isValid(m_native);
}

bgfx::TextureHandle Texture::takeNative() {
    const bgfx::TextureHandle native = m_native;
    m_native = bgfx::TextureHandle{bgfx::kInvalidHandle};
    return native;
}

} // namespace hbg::gfx
