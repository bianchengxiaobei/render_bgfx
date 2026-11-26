#pragma once

#include "asset/texture_sampler.h"
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
    uint16_t layers = 1;
    uint16_t depth = 1;
    uint8_t mipCount = 1;
    bool cubeMap = false;
    bool is3D = false;
    gfx::TextureFormat format = gfx::TextureFormat::RGBA8;
    std::vector<uint8_t> pixels;

    bool valid() const { return width > 0 && height > 0 && !pixels.empty(); }
    bool isFloat() const { return format == gfx::TextureFormat::RGBA16F || format == gfx::TextureFormat::RGBA32F; }
    bool isCompressed() const {
        return format == gfx::TextureFormat::BC1 || format == gfx::TextureFormat::BC3 ||
               format == gfx::TextureFormat::BC5 || format == gfx::TextureFormat::BC7;
    }
};

enum class LayeredTextureLayout : uint8_t { Texture2DArray, Texture3D, Cubemap };

class TextureLoader {
  public:
    static bool loadFromMemory(const void* data, std::size_t size, ImageAsset& image, std::string* error = nullptr);
    static bool loadFromFile(const std::filesystem::path& path, ImageAsset& image, std::string* error = nullptr);
    static bool composeLayered(const std::vector<const ImageAsset*>& layers, LayeredTextureLayout layout,
                               ImageAsset& image, std::string* error = nullptr);
    static bool packOrmMask(const ImageAsset* occlusion, const ImageAsset* metallicRoughness, ImageAsset& packed,
                            std::string* error = nullptr);

    static uint64_t samplerFlags(const TextureSampler& sampler);
    static gfx::TextureHandle createTexture(gfx::ResourceManager& resources, const ImageAsset& image, bool srgb,
                                            const std::string& name = {});
    static gfx::TextureHandle createTexture(gfx::ResourceManager& resources, const ImageAsset& image, bool srgb,
                                            const TextureSampler& sampler, const std::string& name = {});
};

} // namespace hbg::asset
