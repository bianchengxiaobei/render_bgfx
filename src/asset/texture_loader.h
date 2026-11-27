#pragma once

#include "gfx/resource_manager.h"
#include "gfx/texture.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hbg::asset {

struct ImageAsset {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    gfx::TextureFormat format = gfx::TextureFormat::RGBA8;
    std::vector<uint8_t> pixels;

    bool valid() const { return width > 0 && height > 0 && !pixels.empty(); }
    bool isFloat() const { return format == gfx::TextureFormat::RGBA16F || format == gfx::TextureFormat::RGBA32F; }
};

class TextureLoader {
  public:
    static bool loadFromMemory(const void* data, std::size_t size, ImageAsset& image, std::string* error = nullptr);
    static bool loadFromFile(const std::filesystem::path& path, ImageAsset& image, std::string* error = nullptr);

    static gfx::TextureHandle createTexture(gfx::ResourceManager& resources, const ImageAsset& image, bool srgb,
                                            const std::string& name = {});
};

} // namespace hbg::asset
