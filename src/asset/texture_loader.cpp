#include "asset/texture_loader.h"

#include "bimg/decode.h"

#include <bx/allocator.h>
#include <bx/error.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>

namespace hbg::asset {
namespace {

void setError(std::string* error, const bx::Error& source) {
    if (error == nullptr) {
        return;
    }
    const bx::StringView message = source.getMessage();
    error->assign(message.getPtr(), message.getLength());
}

void setError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool readFile(const std::filesystem::path& path, std::vector<uint8_t>& data, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        setError(error, "failed to open image: " + path.string());
        return false;
    }

    data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (data.empty()) {
        setError(error, "image is empty: " + path.string());
        return false;
    }
    return true;
}

gfx::TextureFormat toCompressedFormat(bimg::TextureFormat::Enum format) {
    switch (format) {
    case bimg::TextureFormat::BC1:
        return gfx::TextureFormat::BC1;
    case bimg::TextureFormat::BC3:
        return gfx::TextureFormat::BC3;
    case bimg::TextureFormat::BC5:
        return gfx::TextureFormat::BC5;
    case bimg::TextureFormat::BC7:
        return gfx::TextureFormat::BC7;
    default:
        return gfx::TextureFormat::Count;
    }
}

} // namespace

bool TextureLoader::loadFromMemory(const void* data, std::size_t size, ImageAsset& image, std::string* error) {
    if (data == nullptr || size == 0) {
        setError(error, "image data is empty");
        return false;
    }

    bx::DefaultAllocator allocator;
    bx::Error parseError;
    bimg::ImageContainer* parsed =
        bimg::imageParse(&allocator, data, static_cast<uint32_t>(size), bimg::TextureFormat::Count, &parseError);
    if (parsed == nullptr) {
        setError(error, parseError);
        if (error != nullptr && error->empty()) {
            *error = "bimg failed to parse image";
        }
        return false;
    }

    const bool compressed = bimg::isCompressed(parsed->m_format);
    const gfx::TextureFormat compressedFormat = toCompressedFormat(parsed->m_format);
    if (compressed && compressedFormat != gfx::TextureFormat::Count && !parsed->m_cubeMap &&
        parsed->m_numLayers <= 1 && parsed->m_data != nullptr && parsed->m_size > 0) {
        image.width = parsed->m_width;
        image.height = parsed->m_height;
        image.mipCount = static_cast<uint8_t>(std::max<uint32_t>(parsed->m_numMips, 1));
        image.format = compressedFormat;
        image.pixels.assign(static_cast<const uint8_t*>(parsed->m_data),
                            static_cast<const uint8_t*>(parsed->m_data) + parsed->m_size);
        bimg::imageFree(parsed);
        if (!image.valid()) {
            setError(error, "decoded compressed image has no pixels");
            return false;
        }
        return true;
    }

    const bimg::TextureFormat::Enum destination =
        bimg::isFloat(parsed->m_format) ? bimg::TextureFormat::RGBA32F : bimg::TextureFormat::RGBA8;
    bimg::ImageContainer* converted = bimg::imageConvert(&allocator, destination, *parsed, false);
    bimg::imageFree(parsed);
    if (converted == nullptr) {
        setError(error, "bimg failed to convert image to RGBA");
        return false;
    }

    image.width = converted->m_width;
    image.height = converted->m_height;
    image.mipCount = 1;
    image.format = destination == bimg::TextureFormat::RGBA32F ? gfx::TextureFormat::RGBA32F
                                                               : gfx::TextureFormat::RGBA8;
    image.pixels.assign(static_cast<const uint8_t*>(converted->m_data),
                        static_cast<const uint8_t*>(converted->m_data) + converted->m_size);
    bimg::imageFree(converted);

    if (!image.valid()) {
        setError(error, "decoded image has no pixels");
        return false;
    }
    return true;
}

bool TextureLoader::loadFromFile(const std::filesystem::path& path, ImageAsset& image, std::string* error) {
    std::vector<uint8_t> data;
    if (!readFile(path, data, error)) {
        return false;
    }
    if (!loadFromMemory(data.data(), data.size(), image, error)) {
        return false;
    }
    image.name = path.filename().string();
    return true;
}

bool TextureLoader::packOrmMask(const ImageAsset* occlusion, const ImageAsset* metallicRoughness,
                                ImageAsset& packed, std::string* error) {
    if (occlusion == nullptr && metallicRoughness == nullptr) {
        setError(error, "packed mask requires an occlusion or metallic-roughness image");
        return false;
    }

    const auto validate = [error](const ImageAsset* image, const char* label) {
        if (image == nullptr) {
            return true;
        }
        if (image->format != gfx::TextureFormat::RGBA8) {
            setError(error, std::string("packed mask: ") + label + " must be RGBA8, got " +
                                gfx::textureFormatName(image->format));
            return false;
        }
        if (image->width == 0 || image->height == 0) {
            setError(error, std::string("packed mask: ") + label + " has invalid dimensions");
            return false;
        }
        const std::size_t required =
            static_cast<std::size_t>(image->width) * static_cast<std::size_t>(image->height) * 4u;
        if (image->pixels.size() < required) {
            setError(error, std::string("packed mask: ") + label + " pixel data is truncated");
            return false;
        }
        return true;
    };
    if (!validate(occlusion, "occlusion") || !validate(metallicRoughness, "metallic-roughness")) {
        return false;
    }

    const uint32_t width = occlusion == nullptr
                               ? metallicRoughness->width
                               : (metallicRoughness == nullptr
                                      ? occlusion->width
                                      : std::max(occlusion->width, metallicRoughness->width));
    const uint32_t height = occlusion == nullptr
                                ? metallicRoughness->height
                                : (metallicRoughness == nullptr
                                       ? occlusion->height
                                       : std::max(occlusion->height, metallicRoughness->height));
    if (width == 0 || height == 0 || width > 65535u || height > 65535u) {
        setError(error, "packed mask: unsupported output dimensions");
        return false;
    }

    const auto sample = [width, height](const ImageAsset* image, uint32_t x, uint32_t y,
                                        std::size_t channel) -> uint8_t {
        if (image == nullptr) {
            return 255;
        }
        if (image->width == width && image->height == height) {
            return image->pixels[(static_cast<std::size_t>(y) * image->width + x) * 4u + channel];
        }

        const float sourceX =
            (static_cast<float>(x) + 0.5f) * static_cast<float>(image->width) / static_cast<float>(width) - 0.5f;
        const float sourceY =
            (static_cast<float>(y) + 0.5f) * static_cast<float>(image->height) / static_cast<float>(height) - 0.5f;
        const float clampedX = std::clamp(sourceX, 0.0f, static_cast<float>(image->width - 1u));
        const float clampedY = std::clamp(sourceY, 0.0f, static_cast<float>(image->height - 1u));
        const uint32_t x0 = static_cast<uint32_t>(std::floor(clampedX));
        const uint32_t y0 = static_cast<uint32_t>(std::floor(clampedY));
        const uint32_t x1 = std::min(x0 + 1u, image->width - 1u);
        const uint32_t y1 = std::min(y0 + 1u, image->height - 1u);
        const float tx = clampedX - static_cast<float>(x0);
        const float ty = clampedY - static_cast<float>(y0);
        const auto read = [image, channel](uint32_t px, uint32_t py) {
            return static_cast<float>(image->pixels[(static_cast<std::size_t>(py) * image->width + px) * 4u +
                                                    channel]);
        };
        const float low = read(x0, y0) * (1.0f - tx) + read(x1, y0) * tx;
        const float high = read(x0, y1) * (1.0f - tx) + read(x1, y1) * tx;
        return static_cast<uint8_t>(std::clamp(low * (1.0f - ty) + high * ty + 0.5f, 0.0f, 255.0f));
    };

    packed.width = width;
    packed.height = height;
    packed.mipCount = 1;
    packed.format = gfx::TextureFormat::RGBA8;
    packed.name = "OrmMask";
    packed.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4u;
            packed.pixels[offset + 0] = sample(occlusion, x, y, 0);
            packed.pixels[offset + 1] = sample(metallicRoughness, x, y, 1);
            packed.pixels[offset + 2] = sample(metallicRoughness, x, y, 2);
            packed.pixels[offset + 3] = 255;
        }
    }
    return true;
}

uint64_t TextureLoader::samplerFlags(const TextureSampler& sampler) {
    uint64_t flags = BGFX_TEXTURE_NONE;
    switch (sampler.wrapS) {
    case TextureWrap::Clamp:
        flags |= BGFX_SAMPLER_U_CLAMP;
        break;
    case TextureWrap::Mirror:
        flags |= BGFX_SAMPLER_U_MIRROR;
        break;
    case TextureWrap::Repeat:
    default:
        break;
    }
    switch (sampler.wrapT) {
    case TextureWrap::Clamp:
        flags |= BGFX_SAMPLER_V_CLAMP;
        break;
    case TextureWrap::Mirror:
        flags |= BGFX_SAMPLER_V_MIRROR;
        break;
    case TextureWrap::Repeat:
    default:
        break;
    }
    if (sampler.minFilter == TextureFilter::Nearest) {
        flags |= BGFX_SAMPLER_MIN_POINT;
    }
    if (sampler.magFilter == TextureFilter::Nearest) {
        flags |= BGFX_SAMPLER_MAG_POINT;
    }
    if (sampler.mipFilter == TextureMipFilter::Nearest) {
        flags |= BGFX_SAMPLER_MIP_POINT;
    }
    return flags;
}

gfx::TextureHandle TextureLoader::createTexture(gfx::ResourceManager& resources, const ImageAsset& image, bool srgb,
                                                const std::string& name) {
    return createTexture(resources, image, srgb, TextureSampler{}, name);
}

gfx::TextureHandle TextureLoader::createTexture(gfx::ResourceManager& resources, const ImageAsset& image, bool srgb,
                                                const TextureSampler& sampler, const std::string& name) {
    if (!image.valid()) {
        return {};
    }

    gfx::TextureDesc desc;
    desc.width = static_cast<uint16_t>(image.width);
    desc.height = static_cast<uint16_t>(image.height);
    desc.mipCount = image.mipCount;
    desc.format = image.format;
    desc.data = image.pixels.data();
    desc.dataSize = static_cast<uint32_t>(image.pixels.size());
    desc.flags = samplerFlags(sampler) | (srgb ? BGFX_TEXTURE_SRGB : 0);
    desc.name = name.empty() ? image.name : name;
    return resources.createTexture(desc);
}

} // namespace hbg::asset
