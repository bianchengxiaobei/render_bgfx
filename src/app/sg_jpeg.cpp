#include "app/sg_jpeg.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb/stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace hbg {
namespace {

void appendBytes(void* context, void* data, int size) {
    if (context == nullptr || data == nullptr || size <= 0) {
        return;
    }
    auto* output = static_cast<std::vector<std::uint8_t>*>(context);
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    output->insert(output->end(), bytes, bytes + size);
}

bool isJpegPath(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string extension = path.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == "jpg" || extension == "jpeg";
}

} // namespace

bool encodeJpeg(const std::uint8_t* rgba, int width, int height, int quality,
                std::vector<std::uint8_t>& output) {
    output.clear();
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    output.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) / 4);
    const int written = stbi_write_jpg_to_func(appendBytes, &output, width, height, 4, rgba,
                                               std::clamp(quality, 1, 100));
    return written != 0 && !output.empty();
}

bool encodePng(const std::uint8_t* rgba, int width, int height,
               std::vector<std::uint8_t>& output) {
    output.clear();
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    output.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    const int written = stbi_write_png_to_func(appendBytes, &output, width, height, 4, rgba,
                                               width * 4);
    return written != 0 && !output.empty();
}

bool writeImageFile(const std::string& path, const std::uint8_t* rgba, int width, int height,
                    int quality) {
    if (path.empty() || rgba == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    std::vector<std::uint8_t> encoded;
    if (isJpegPath(path)) {
        if (!encodeJpeg(rgba, width, height, quality, encoded)) {
            return false;
        }
    } else {
        encoded.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        const int written = stbi_write_png_to_func(appendBytes, &encoded, width, height, 4, rgba,
                                                   width * 4);
        if (written == 0 || encoded.empty()) {
            return false;
        }
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(encoded.data()),
               static_cast<std::streamsize>(encoded.size()));
    return static_cast<bool>(file);
}

} // namespace hbg
