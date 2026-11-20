#include "render/lightmap/lightmap.h"

#include <bimg/bimg.h>
#include <doctest/doctest.h>

#include <bx/file.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace hbg;

namespace {

bool writeDds(const std::filesystem::path& path, uint32_t size, uint16_t firstValue) {
    std::vector<uint16_t> payload(static_cast<std::size_t>(size) * size * 4u, 0u);
    for (std::size_t index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<uint16_t>(firstValue + index);
    }

    bimg::ImageContainer container{};
    container.m_allocator = nullptr;
    container.m_data = payload.data();
    container.m_format = bimg::TextureFormat::RGBA16F;
    container.m_orientation = bimg::Orientation::R0;
    container.m_parser = bimg::ImageParser::Count;
    container.m_size = static_cast<uint32_t>(payload.size() * sizeof(uint16_t));
    container.m_offset = std::numeric_limits<uint32_t>::max();
    container.m_width = size;
    container.m_height = size;
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
    bx::Error error;
    if (!writer.open(bx::FilePath(path.string().c_str()), false, &error)) {
        return false;
    }
    const int32_t written = bimg::imageWriteDds(&writer, container, container.m_data, container.m_size, &error);
    writer.close();
    return written > 0 && error.isOk();
}

} // namespace

TEST_CASE("lightmap atlas DDS loader preserves RGBA16F layers") {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("hbg_lightmap_loader_" + std::to_string(nonce));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    constexpr uint32_t kSize = 8u;
    render::LightmapAtlasSource source;
    source.file = "lm0";
    source.size = kSize;
    for (uint32_t channel = 0; channel < render::lightmap::kShChannelCount; ++channel) {
        REQUIRE(writeDds(directory / ("lm0_sh" + std::to_string(channel) + ".dds"), kSize,
                         static_cast<uint16_t>(0x3c00u + channel * 0x20u)));
    }

    render::lightmap::AtlasImage image;
    std::string error;
    REQUIRE_MESSAGE(render::lightmap::loadAtlasImage(directory, source, image, &error), error);
    CHECK(image.size == kSize);
    for (uint32_t channel = 0; channel < render::lightmap::kShChannelCount; ++channel) {
        CHECK(image.sh[channel].size() == static_cast<std::size_t>(kSize) * kSize * 4u);
        CHECK(image.sh[channel][0] == static_cast<uint16_t>(0x3c00u + channel * 0x20u));
    }

    render::LightmapAtlasSource wrongSize = source;
    wrongSize.size = kSize * 2u;
    CHECK_FALSE(render::lightmap::loadAtlasImage(directory, wrongSize, image, &error));
    CHECK(error.find("must be square") != std::string::npos);

    render::LightmapAtlasSource missing = source;
    missing.file = "missing";
    CHECK_FALSE(render::lightmap::loadAtlasImage(directory, missing, image, &error));
    CHECK(error.find("failed to open") != std::string::npos);

    std::filesystem::remove_all(directory);
}
