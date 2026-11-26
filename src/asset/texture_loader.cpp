#include "asset/texture_loader.h"

#include "bimg/decode.h"

#include <bx/allocator.h>
#include <bx/error.h>

#if defined(HBG_HAS_BASISU) && HBG_HAS_BASISU
#include <basisu_transcoder.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
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

uint32_t readUint32LE(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8u) |
           (static_cast<uint32_t>(data[2]) << 16u) | (static_cast<uint32_t>(data[3]) << 24u);
}

bool isKtx2(const void* data, std::size_t size) {
    static constexpr uint8_t kIdentifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32,
                                                0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    return data != nullptr && size >= 80 && std::memcmp(data, kIdentifier, sizeof(kIdentifier)) == 0;
}

bool needsBasisuTranscode(const void* data, std::size_t size) {
    if (!isKtx2(data, size)) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    const uint32_t vkFormat = readUint32LE(bytes + 12);
    const uint32_t supercompression = readUint32LE(bytes + 44);
    return supercompression == 1 || supercompression == 2 || (supercompression == 0 && vkFormat == 0);
}

#if defined(HBG_HAS_BASISU) && HBG_HAS_BASISU
bool loadBasisuKtx2(const void* data, std::size_t size, ImageAsset& image, std::string* error) {
    static const bool initialized = [] {
        basist::basisu_transcoder_init();
        return true;
    }();
    (void)initialized;

    basist::ktx2_transcoder transcoder;
    if (!transcoder.init(data, static_cast<uint32_t>(size))) {
        setError(error, "BasisU: failed to parse KTX2 container");
        return false;
    }

    const basist::ktx2_header& header = transcoder.get_header();
    if (header.m_pixel_depth > 1 || transcoder.get_faces() != 1 || transcoder.get_layers() > 1) {
        setError(error, "BasisU: only single-layer 2D textures are supported");
        return false;
    }

    const basist::basis_tex_format format = transcoder.get_format();
    if (format != basist::basis_tex_format::cETC1S && format != basist::basis_tex_format::cUASTC4x4) {
        setError(error, "BasisU: UASTC HDR textures are not supported");
        return false;
    }

    const bool uastc = format == basist::basis_tex_format::cUASTC4x4;
    const bool hasAlpha = transcoder.get_has_alpha() != 0;
    const basist::transcoder_texture_format target =
        (uastc || hasAlpha) ? basist::transcoder_texture_format::cTFBC7_RGBA
                            : basist::transcoder_texture_format::cTFBC1_RGB;
    const uint32_t blockBytes = basist::basis_get_bytes_per_block_or_pixel(target);
    if (!basist::basis_is_format_supported(target, format)) {
        setError(error, "BasisU: target BC format is not supported by the transcoder");
        return false;
    }
    if (!transcoder.start_transcoding()) {
        setError(error, "BasisU: failed to initialize transcoding");
        return false;
    }

    const uint32_t levels = transcoder.get_levels();
    if (levels == 0 || levels > 255) {
        setError(error, "BasisU: invalid mip level count");
        return false;
    }

    image.width = transcoder.get_width();
    image.height = transcoder.get_height();
    image.mipCount = static_cast<uint8_t>(levels);
    image.format = target == basist::transcoder_texture_format::cTFBC7_RGBA ? gfx::TextureFormat::BC7
                                                                           : gfx::TextureFormat::BC1;
    image.pixels.clear();

    for (uint32_t level = 0; level < levels; ++level) {
        basist::ktx2_image_level_info info{};
        if (!transcoder.get_image_level_info(info, level, 0, 0)) {
            image.pixels.clear();
            setError(error, "BasisU: failed to read mip level " + std::to_string(level));
            return false;
        }
        const std::size_t levelSize = static_cast<std::size_t>(info.m_total_blocks) * blockBytes;
        if (levelSize == 0) {
            image.pixels.clear();
            setError(error, "BasisU: empty mip level " + std::to_string(level));
            return false;
        }
        const std::size_t offset = image.pixels.size();
        image.pixels.resize(offset + levelSize);
        if (!transcoder.transcode_image_level(level, 0, 0, image.pixels.data() + offset, info.m_total_blocks, target,
                                              0, info.m_num_blocks_x, 0)) {
            image.pixels.clear();
            setError(error, "BasisU: failed to transcode mip level " + std::to_string(level));
            return false;
        }
    }

    if (!image.valid()) {
        setError(error, "BasisU: transcoded image has no pixels");
        return false;
    }
    return true;
}
#endif

} // namespace

bool TextureLoader::loadFromMemory(const void* data, std::size_t size, ImageAsset& image, std::string* error) {
    image = ImageAsset{};
    if (data == nullptr || size == 0) {
        setError(error, "image data is empty");
        return false;
    }

    if (needsBasisuTranscode(data, size)) {
#if defined(HBG_HAS_BASISU) && HBG_HAS_BASISU
        return loadBasisuKtx2(data, size, image, error);
#else
        setError(error, "BasisU KTX2 supercompression support is disabled");
        return false;
#endif
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
    bimg::ImageContainer* converted = bimg::imageConvert(&allocator, destination, *parsed, true);
    bimg::imageFree(parsed);
    if (converted == nullptr) {
        setError(error, "bimg failed to convert image to RGBA");
        return false;
    }

    image.width = converted->m_width;
    image.height = converted->m_height;
    image.mipCount = static_cast<uint8_t>(std::max<uint32_t>(converted->m_numMips, 1));
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

bool TextureLoader::composeLayered(const std::vector<const ImageAsset*>& layers, LayeredTextureLayout layout,
                                   ImageAsset& image, std::string* error) {
    if (layers.empty()) {
        setError(error, "layered texture requires at least one layer");
        return false;
    }
    if (layout == LayeredTextureLayout::Cubemap && layers.size() != 6) {
        setError(error, "cubemap texture requires exactly six faces");
        return false;
    }
    if (layers.size() > 65535u) {
        setError(error, "layered texture has too many images");
        return false;
    }

    const ImageAsset* first = layers.front();
    if (first == nullptr || !first->valid()) {
        setError(error, "layered texture contains an invalid image");
        return false;
    }
    if (first->mipCount != 1) {
        setError(error, "layered texture sources must have a single mip");
        return false;
    }
    if (layout == LayeredTextureLayout::Cubemap && first->width != first->height) {
        setError(error, "cubemap faces must be square");
        return false;
    }

    std::size_t totalBytes = 0;
    for (const ImageAsset* layer : layers) {
        if (layer == nullptr || !layer->valid()) {
            setError(error, "layered texture contains an invalid image");
            return false;
        }
        if (layer->width != first->width || layer->height != first->height ||
            layer->format != first->format) {
            setError(error, "layered texture images must share size and format");
            return false;
        }
        if (layer->mipCount != 1) {
            setError(error, "layered texture sources must have a single mip");
            return false;
        }
        if (layer->pixels.size() != first->pixels.size()) {
            setError(error, "layered texture images must share the same pixel data size");
            return false;
        }
        if (totalBytes > std::numeric_limits<std::size_t>::max() - layer->pixels.size()) {
            setError(error, "layered texture is too large");
            return false;
        }
        totalBytes += layer->pixels.size();
    }

    image = ImageAsset{};
    image.name = "LayeredTexture";
    image.width = first->width;
    image.height = first->height;
    image.mipCount = 1;
    image.format = first->format;
    image.pixels.reserve(totalBytes);
    for (const ImageAsset* layer : layers) {
        image.pixels.insert(image.pixels.end(), layer->pixels.begin(), layer->pixels.end());
    }
    switch (layout) {
    case LayeredTextureLayout::Texture2DArray:
        image.layers = static_cast<uint16_t>(layers.size());
        break;
    case LayeredTextureLayout::Texture3D:
        image.depth = static_cast<uint16_t>(layers.size());
        image.is3D = true;
        break;
    case LayeredTextureLayout::Cubemap:
        image.cubeMap = true;
        break;
    }
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

    packed = ImageAsset{};
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
    desc.layers = image.layers;
    desc.depth = image.depth;
    desc.mipCount = image.mipCount;
    desc.format = image.format;
    desc.cubeMap = image.cubeMap;
    desc.is3D = image.is3D;
    desc.data = image.pixels.data();
    desc.dataSize = static_cast<uint32_t>(image.pixels.size());
    desc.flags = samplerFlags(sampler) | (srgb ? BGFX_TEXTURE_SRGB : 0);
    desc.name = name.empty() ? image.name : name;
    return resources.createTexture(desc);
}

} // namespace hbg::asset
