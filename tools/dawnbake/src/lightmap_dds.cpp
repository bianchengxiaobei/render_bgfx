#include "hbg/dawn/lightmap_dds.h"

#include "hbg/dawn/dawn_package.h"
#include "hbg/dawn/dawn_serialize.h"

#include <bimg/bimg.h>
#include <bx/error.h>
#include <bx/file.h>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace hbg::dawn {
namespace {

constexpr uint32_t kShChannelCount = 3u;
constexpr uint64_t kDdsMagicAndHeaderBytes = 128u;
constexpr uint64_t kDdsDx10HeaderBytes = 20u;
constexpr uint64_t kDdsHeaderBytes = kDdsMagicAndHeaderBytes + kDdsDx10HeaderBytes;
constexpr uint64_t kBytesPerTexel = 4u * sizeof(uint16_t);

void setError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

bool validateImages(const LightmapAtlasImageSet& images, std::string* error) {
    if (images.atlases.empty()) {
        setError(error, "no lightmap atlases to write");
        return false;
    }

    for (std::size_t atlasIndex = 0; atlasIndex < images.atlases.size(); ++atlasIndex) {
        const LightmapAtlasImage& image = images.atlases[atlasIndex];
        if (image.size == 0u) {
            setError(error, "lightmap atlas " + std::to_string(atlasIndex) + " has zero size");
            return false;
        }

        const uint64_t pixels = static_cast<uint64_t>(image.size) * static_cast<uint64_t>(image.size);
        if (pixels > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) / kBytesPerTexel) {
            setError(error, "lightmap atlas " + std::to_string(atlasIndex) + " is too large for DDS");
            return false;
        }
        const uint64_t expectedHalfs = pixels * 4u;
        if (expectedHalfs > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
            setError(error, "lightmap atlas " + std::to_string(atlasIndex) + " is too large for memory");
            return false;
        }

        for (uint32_t channel = 0; channel < kShChannelCount; ++channel) {
            if (image.sh[channel].size() != static_cast<std::size_t>(expectedHalfs)) {
                setError(error, "lightmap atlas " + std::to_string(atlasIndex) + " channel " + std::to_string(channel) +
                                    " has " + std::to_string(image.sh[channel].size()) + " values, expected " +
                                    std::to_string(expectedHalfs));
                return false;
            }
        }
    }
    return true;
}

bool writeChannel(const LightmapAtlasImage& image, uint32_t channel, const std::filesystem::path& path,
                  std::string* error) {
    const uint64_t pixels = static_cast<uint64_t>(image.size) * static_cast<uint64_t>(image.size);
    const uint64_t payloadBytes = pixels * kBytesPerTexel;
    if (payloadBytes > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) - kDdsHeaderBytes) {
        setError(error, "lightmap atlas image is too large for the DDS writer: " + path.string());
        return false;
    }

    bimg::ImageContainer container{};
    container.m_allocator = nullptr;
    container.m_data = const_cast<uint16_t*>(image.sh[channel].data());
    container.m_format = bimg::TextureFormat::RGBA16F;
    container.m_orientation = bimg::Orientation::R0;
    container.m_parser = bimg::ImageParser::Count;
    container.m_size = static_cast<uint32_t>(payloadBytes);
    container.m_offset = std::numeric_limits<uint32_t>::max();
    container.m_width = image.size;
    container.m_height = image.size;
    container.m_depth = 0;
    container.m_numLayers = 1;
    container.m_numMips = 1;
    container.m_hasAlpha = true;
    container.m_cubeMap = false;
    container.m_ktx = false;
    container.m_ktx2 = false;
    container.m_pvr3 = false;
    container.m_srgb = false;

    bx::FileWriter writer;
    bx::Error bxError;
    if (!writer.open(bx::FilePath(path.string().c_str()), false, &bxError)) {
        setError(error, "failed to open lightmap DDS file: " + path.string());
        return false;
    }

    const int32_t written = bimg::imageWriteDds(&writer, container, container.m_data, container.m_size, &bxError);
    writer.close();

    const uint64_t expectedBytes = kDdsHeaderBytes + payloadBytes;
    if (written <= 0 || static_cast<uint64_t>(written) != expectedBytes || !bxError.isOk()) {
        setError(error, "failed to write lightmap DDS file: " + path.string() + " (wrote " + std::to_string(written) +
                            " bytes, expected " + std::to_string(expectedBytes) + ")");
        return false;
    }
    return true;
}

} // namespace

bool writeLightmapAtlasDds(const LightmapAtlasImageSet& images, const std::filesystem::path& directory,
                           std::string* error) {
    if (directory.empty()) {
        setError(error, "lightmap DDS output directory is empty");
        return false;
    }
    if (!validateImages(images, error)) {
        return false;
    }

    std::error_code createError;
    std::filesystem::create_directories(directory, createError);
    if (createError) {
        setError(error, "failed to create lightmap DDS directory " + directory.string() + ": " + createError.message());
        return false;
    }

    std::vector<std::filesystem::path> writtenPaths;
    const auto cleanup = [&writtenPaths]() {
        for (const std::filesystem::path& path : writtenPaths) {
            std::error_code removeError;
            std::filesystem::remove(path, removeError);
        }
    };

    for (std::size_t atlasIndex = 0; atlasIndex < images.atlases.size(); ++atlasIndex) {
        const LightmapAtlasImage& image = images.atlases[atlasIndex];
        for (uint32_t channel = 0; channel < kShChannelCount; ++channel) {
            const std::filesystem::path path =
                directory / lightmapAtlasDdsFileName(static_cast<uint32_t>(atlasIndex), channel);
            writtenPaths.push_back(path);
            if (!writeChannel(image, channel, path, error)) {
                cleanup();
                return false;
            }
        }
    }
    return true;
}

bool verifyLightmapAtlasDds(const Lightmap2DOutput& output, uint32_t atlasIndex, uint32_t targetX, uint32_t targetY,
                            uint32_t atlasSize, const std::filesystem::path& directory, std::string* error) {
    if (directory.empty()) {
        setError(error, "lightmap DDS input directory is empty");
        return false;
    }
    if (output.header.size.x <= 0 || output.header.size.y <= 0) {
        setError(error, "lightmap DDS result has an invalid size");
        return false;
    }
    const uint32_t width = static_cast<uint32_t>(output.header.size.x);
    const uint32_t height = static_cast<uint32_t>(output.header.size.y);
    const uint64_t sampleCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    if (sampleCount != output.lightmapData.size()) {
        setError(error, "lightmap DDS result sample count does not match its size");
        return false;
    }
    if (atlasSize == 0u || static_cast<uint64_t>(targetX) + width > atlasSize ||
        static_cast<uint64_t>(targetY) + height > atlasSize) {
        setError(error, "lightmap DDS target rect is outside the atlas");
        return false;
    }

    const uint64_t payloadBytes = static_cast<uint64_t>(atlasSize) * static_cast<uint64_t>(atlasSize) * kBytesPerTexel;
    if (payloadBytes > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        setError(error, "lightmap DDS atlas is too large");
        return false;
    }

    std::size_t checkedTexels = 0u;
    for (uint32_t channel = 0; channel < kShChannelCount; ++channel) {
        const std::filesystem::path path = directory / lightmapAtlasDdsFileName(atlasIndex, channel);
        std::vector<uint8_t> bytes;
        try {
            bytes = readFile(path);
        } catch (const std::exception& exception) {
            setError(error, "failed to read lightmap DDS file " + path.string() + ": " + exception.what());
            return false;
        }

        bimg::ImageContainer container{};
        bx::Error parseError;
        if (!bimg::imageParse(container, bytes.data(), static_cast<uint32_t>(bytes.size()), &parseError)) {
            setError(error, "failed to parse lightmap DDS file " + path.string());
            return false;
        }
        if (container.m_format != bimg::TextureFormat::RGBA16F || container.m_width != atlasSize ||
            container.m_height != atlasSize) {
            setError(error, "lightmap DDS file has an unexpected format or size: " + path.string());
            return false;
        }
        if (container.m_numMips != 1u || container.m_numLayers != 1u || container.m_cubeMap || container.m_srgb) {
            setError(error, "lightmap DDS file has unexpected mip/layer/color-space metadata: " + path.string());
            return false;
        }

        bimg::ImageMip mip;
        if (!bimg::imageGetRawData(container, 0u, 0u, bytes.data(), static_cast<uint32_t>(bytes.size()), mip) ||
            mip.m_size != payloadBytes || mip.m_data == nullptr) {
            setError(error, "lightmap DDS file has unexpected payload data: " + path.string());
            return false;
        }

        const uint16_t* decoded = reinterpret_cast<const uint16_t*>(mip.m_data);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const std::size_t sourceIndex = static_cast<std::size_t>(y) * width + x;
                const Lightmap2DSample& sample = output.lightmapData[sourceIndex];
                if (sample.bIsMapped == 0u) {
                    continue;
                }
                if (channel == 0u) {
                    ++checkedTexels;
                }
                const uint64_t targetIndex =
                    (static_cast<uint64_t>(targetY) + y) * atlasSize + static_cast<uint64_t>(targetX) + x;
                const uint64_t targetTexel = targetIndex * 4u;
                const Float4 value = lightmapAtlasTexel(sample, channel);
                const float components[4] = {value.x, value.y, value.z, value.w};
                for (uint32_t component = 0; component < 4u; ++component) {
                    const uint16_t expected = bx::halfFromFloat(components[component]);
                    const uint16_t actual = decoded[static_cast<std::size_t>(targetTexel) + component];
                    if (expected != actual) {
                        setError(error, "lightmap DDS texel mismatch job " + formatGuid(output.header.jobId) +
                                            " atlas " + std::to_string(atlasIndex) + " channel " +
                                            std::to_string(channel) + " texel " + std::to_string(x) + "," +
                                            std::to_string(y) + " component " + std::to_string(component) +
                                            " expected " + std::to_string(expected) + " got " + std::to_string(actual));
                        return false;
                    }
                }
            }
        }
    }

    if (checkedTexels == 0u) {
        setError(error, "lightmap DDS verification found no mapped texels");
        return false;
    }
    return true;
}

} // namespace hbg::dawn
