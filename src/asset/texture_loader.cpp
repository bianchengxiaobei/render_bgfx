#include "asset/texture_loader.h"

#include "bimg/decode.h"

#include <bx/allocator.h>
#include <bx/error.h>

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

gfx::TextureHandle TextureLoader::createTexture(gfx::ResourceManager& resources, const ImageAsset& image, bool srgb,
                                                const std::string& name) {
    if (!image.valid()) {
        return {};
    }

    gfx::TextureDesc desc;
    desc.width = static_cast<uint16_t>(image.width);
    desc.height = static_cast<uint16_t>(image.height);
    desc.format = image.format;
    desc.data = image.pixels.data();
    desc.dataSize = static_cast<uint32_t>(image.pixels.size());
    desc.flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_UVW_CLAMP | (srgb ? BGFX_TEXTURE_SRGB : 0);
    desc.name = name.empty() ? image.name : name;
    return resources.createTexture(desc);
}

} // namespace hbg::asset
