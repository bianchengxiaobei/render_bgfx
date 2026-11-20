#include "render/lightmap/lightmap.h"

#include <bimg/bimg.h>
#include <bx/error.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace hbg::render::lightmap {
namespace {

bool setError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

bool readFile(const std::filesystem::path& path, std::vector<uint8_t>& bytes, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return setError(error, "failed to open lightmap DDS: " + path.string());
    }
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return setError(error, "lightmap DDS is empty: " + path.string());
    }
    if (bytes.size() > UINT32_MAX) {
        return setError(error, "lightmap DDS is too large: " + path.string());
    }
    return true;
}

bool loadChannel(const std::filesystem::path& path, uint32_t size, std::vector<uint16_t>& payload, std::string* error) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes, error)) {
        return false;
    }

    bimg::ImageContainer container{};
    bx::Error parseError;
    if (!bimg::imageParse(container, bytes.data(), static_cast<uint32_t>(bytes.size()), &parseError)) {
        std::string message = "failed to parse lightmap DDS: " + path.string();
        if (!parseError.isOk()) {
            const bx::StringView parseMessage = parseError.getMessage();
            if (parseMessage.getLength() > 0) {
                message += " (" + std::string(parseMessage.getPtr(), parseMessage.getLength()) + ")";
            }
        }
        return setError(error, std::move(message));
    }

    const uint64_t expectedBytes = static_cast<uint64_t>(size) * size * 4u * sizeof(uint16_t);
    if (container.m_format != bimg::TextureFormat::RGBA16F || container.m_width != size || container.m_height != size ||
        container.m_numMips != 1u || container.m_numLayers != 1u || container.m_cubeMap || container.m_srgb) {
        return setError(error, "lightmap DDS must be square RGBA16F without mips/layers/sRGB: " + path.string());
    }

    bimg::ImageMip mip;
    if (!bimg::imageGetRawData(container, 0u, 0u, bytes.data(), static_cast<uint32_t>(bytes.size()), mip) ||
        mip.m_data == nullptr || mip.m_size != expectedBytes || (mip.m_size % sizeof(uint16_t)) != 0u) {
        return setError(error, "lightmap DDS has an unexpected payload: " + path.string());
    }

    payload.resize(static_cast<std::size_t>(mip.m_size / sizeof(uint16_t)));
    std::memcpy(payload.data(), mip.m_data, mip.m_size);
    return true;
}

std::filesystem::path channelPath(const std::filesystem::path& directory, const LightmapAtlasSource& source,
                                  uint32_t channel) {
    return directory / (source.file + "_sh" + std::to_string(channel) + ".dds");
}

} // namespace

bool loadAtlasImage(const std::filesystem::path& directory, const LightmapAtlasSource& source, AtlasImage& image,
                    std::string* error) {
    image = {};
    if (directory.empty()) {
        return setError(error, "lightmap base directory is empty");
    }
    if (source.file.empty()) {
        return setError(error, "lightmap atlas file name is empty");
    }
    if (source.size == 0u) {
        return setError(error, "lightmap atlas '" + source.file + "' has zero size");
    }
    if (source.layers != kShChannelCount) {
        return setError(error, "lightmap atlas '" + source.file + "' must have three SH layers");
    }
    if (source.format != "rgba16f") {
        return setError(error, "lightmap atlas '" + source.file + "' must use rgba16f");
    }

    image.size = source.size;
    for (uint32_t channel = 0; channel < kShChannelCount; ++channel) {
        if (!loadChannel(channelPath(directory, source, channel), source.size, image.sh[channel], error)) {
            image = {};
            return false;
        }
    }
    return true;
}

bool LightmapSet::load(gfx::ResourceManager& resources, const RenderScene& scene, std::string* error) {
    destroy(resources);
    if (scene.lightmapAtlases.empty()) {
        return true;
    }
    if (scene.lightmapBaseDirectory.empty()) {
        return setError(error, "lightmap base directory is empty");
    }

    const uint32_t atlasSize = scene.lightmapAtlases.front().size;
    if (atlasSize == 0u || atlasSize > std::numeric_limits<uint16_t>::max()) {
        return setError(error, "lightmap atlas size is outside the texture range");
    }
    const uint64_t layerCount = static_cast<uint64_t>(scene.lightmapAtlases.size()) * kShChannelCount;
    if (layerCount > std::numeric_limits<uint16_t>::max()) {
        return setError(error, "too many lightmap atlas layers");
    }

    const uint64_t layerBytes = static_cast<uint64_t>(atlasSize) * atlasSize * 4u * sizeof(uint16_t);
    const uint64_t totalBytes = layerBytes * layerCount;
    if (totalBytes > std::numeric_limits<uint32_t>::max()) {
        return setError(error, "lightmap atlas array is too large for a texture upload");
    }

    std::vector<uint8_t> combined(static_cast<std::size_t>(totalBytes), 0u);
    m_atlases.reserve(scene.lightmapAtlases.size());
    for (std::size_t atlasIndex = 0; atlasIndex < scene.lightmapAtlases.size(); ++atlasIndex) {
        const LightmapAtlasSource& source = scene.lightmapAtlases[atlasIndex];
        if (source.size != atlasSize) {
            m_atlases.clear();
            return setError(error, "all lightmap atlases must have the same size");
        }

        AtlasImage image;
        if (!loadAtlasImage(scene.lightmapBaseDirectory, source, image, error)) {
            m_atlases.clear();
            return false;
        }

        for (uint32_t channel = 0; channel < kShChannelCount; ++channel) {
            const uint64_t layerIndex = static_cast<uint64_t>(atlasIndex) * kShChannelCount + channel;
            std::memcpy(combined.data() + static_cast<std::size_t>(layerIndex * layerBytes), image.sh[channel].data(),
                        static_cast<std::size_t>(layerBytes));
        }
        m_atlases.push_back(LightmapAtlas{static_cast<uint32_t>(atlasIndex * kShChannelCount), atlasSize});
    }

    gfx::TextureDesc desc;
    desc.width = static_cast<uint16_t>(atlasSize);
    desc.height = static_cast<uint16_t>(atlasSize);
    desc.layers = static_cast<uint16_t>(layerCount);
    desc.format = gfx::TextureFormat::RGBA16F;
    desc.flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_UVW_CLAMP;
    desc.data = combined.data();
    desc.dataSize = static_cast<uint32_t>(combined.size());
    desc.name = "lightmap.sh";
    m_shArray = resources.createTexture(desc);
    if (!m_shArray.valid()) {
        m_atlases.clear();
        return setError(error, "failed to create the lightmap SH texture array");
    }
    m_layerCount = static_cast<uint32_t>(layerCount);
    return true;
}

void LightmapSet::destroy(gfx::ResourceManager& resources) {
    if (m_shArray.valid()) {
        resources.destroyTexture(m_shArray);
    }
    m_shArray = {};
    m_atlases.clear();
    m_layerCount = 0;
}

} // namespace hbg::render::lightmap
