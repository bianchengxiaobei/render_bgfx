#include "hbg/dawn/dawn_uv_dump.h"

#include <bimg/bimg.h>
#include <bx/error.h>
#include <bx/file.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hbg::dawn {
namespace {

struct Rgba {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

class Canvas {
public:
    Canvas(uint32_t width, uint32_t height) : m_width(width), m_height(height), m_pixels(width * height * 4u) {}

    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    const std::vector<uint8_t>& pixels() const { return m_pixels; }

    void clear(Rgba color) {
        for (uint32_t y = 0; y < m_height; ++y) {
            for (uint32_t x = 0; x < m_width; ++x) {
                setPixel(static_cast<int32_t>(x), static_cast<int32_t>(y), color);
            }
        }
    }

    void blendPixel(int32_t x, int32_t y, Rgba color) {
        if (x < 0 || y < 0 || x >= static_cast<int32_t>(m_width) || y >= static_cast<int32_t>(m_height) ||
            color.a == 0u) {
            return;
        }
        const std::size_t offset =
            (static_cast<std::size_t>(y) * m_width + static_cast<std::size_t>(x)) * 4u;
        if (color.a == 255u) {
            m_pixels[offset + 0u] = color.r;
            m_pixels[offset + 1u] = color.g;
            m_pixels[offset + 2u] = color.b;
            m_pixels[offset + 3u] = 255u;
            return;
        }
        const uint32_t alpha = color.a;
        const uint32_t inverse = 255u - alpha;
        m_pixels[offset + 0u] =
            static_cast<uint8_t>((static_cast<uint32_t>(color.r) * alpha + m_pixels[offset + 0u] * inverse) / 255u);
        m_pixels[offset + 1u] =
            static_cast<uint8_t>((static_cast<uint32_t>(color.g) * alpha + m_pixels[offset + 1u] * inverse) / 255u);
        m_pixels[offset + 2u] =
            static_cast<uint8_t>((static_cast<uint32_t>(color.b) * alpha + m_pixels[offset + 2u] * inverse) / 255u);
        m_pixels[offset + 3u] = 255u;
    }

    void fillRect(int32_t x, int32_t y, int32_t width, int32_t height, Rgba color) {
        const int32_t x0 = std::max(x, 0);
        const int32_t y0 = std::max(y, 0);
        const int32_t x1 = std::min(x + width, static_cast<int32_t>(m_width));
        const int32_t y1 = std::min(y + height, static_cast<int32_t>(m_height));
        for (int32_t py = y0; py < y1; ++py) {
            for (int32_t px = x0; px < x1; ++px) {
                blendPixel(px, py, color);
            }
        }
    }

    void strokeRect(int32_t x, int32_t y, int32_t width, int32_t height, int32_t thickness, Rgba color) {
        if (width <= 0 || height <= 0 || thickness <= 0) {
            return;
        }
        fillRect(x, y, width, thickness, color);
        fillRect(x, y + height - thickness, width, thickness, color);
        fillRect(x, y, thickness, height, color);
        fillRect(x + width - thickness, y, thickness, height, color);
    }

    void line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, Rgba color) {
        int32_t dx = std::abs(x1 - x0);
        int32_t sx = x0 < x1 ? 1 : -1;
        int32_t dy = -std::abs(y1 - y0);
        int32_t sy = y0 < y1 ? 1 : -1;
        int32_t error = dx + dy;
        while (true) {
            blendPixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) {
                break;
            }
            const int32_t twice = 2 * error;
            if (twice >= dy) {
                error += dy;
                x0 += sx;
            }
            if (twice <= dx) {
                error += dx;
                y0 += sy;
            }
        }
    }

private:
    void setPixel(int32_t x, int32_t y, Rgba color) {
        if (x < 0 || y < 0 || x >= static_cast<int32_t>(m_width) || y >= static_cast<int32_t>(m_height)) {
            return;
        }
        const std::size_t offset =
            (static_cast<std::size_t>(y) * m_width + static_cast<std::size_t>(x)) * 4u;
        m_pixels[offset + 0u] = color.r;
        m_pixels[offset + 1u] = color.g;
        m_pixels[offset + 2u] = color.b;
        m_pixels[offset + 3u] = color.a;
    }

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    std::vector<uint8_t> m_pixels;
};

Rgba objectColor(const std::string& name) {
    static constexpr Rgba kPalette[] = {
        {72, 160, 220, 255}, {220, 120, 72, 255}, {112, 196, 128, 255}, {200, 96, 168, 255},
        {228, 196, 80, 255}, {108, 128, 220, 255}, {220, 92, 92, 255}, {92, 204, 204, 255},
    };
    uint32_t hash = 2166136261u;
    for (char value : name) {
        hash ^= static_cast<unsigned char>(value);
        hash *= 16777619u;
    }
    return kPalette[hash % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

Rgba withAlpha(Rgba color, uint8_t alpha) {
    color.a = alpha;
    return color;
}

bool writePng(const std::filesystem::path& path, const Canvas& canvas) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const std::string pathText = path.string();
    bx::FileWriter writer;
    bx::Error error;
    if (!writer.open(bx::FilePath(pathText.c_str()), false, &error)) {
        return false;
    }
    const int32_t written = bimg::imageWritePng(&writer, canvas.width(), canvas.height(), canvas.width() * 4u,
                                                canvas.pixels().data(), bimg::TextureFormat::RGBA8, false, &error);
    writer.close();
    return written > 0 && error.isOk();
}

} // namespace

bool writeLightmapUvDump(const std::filesystem::path& path,
                         const std::vector<LightmapUvDumpObject>& objects,
                         const std::vector<LightmapAtlasPlan>& atlases, uint32_t imageSize) {
    if (path.empty() || atlases.empty() || imageSize < 16u || imageSize > 4096u) {
        return false;
    }

    const uint64_t canvasWidth64 = static_cast<uint64_t>(imageSize) * atlases.size();
    if (canvasWidth64 > 16384u) {
        return false;
    }
    const uint32_t canvasWidth = static_cast<uint32_t>(canvasWidth64);

    Canvas canvas(canvasWidth, imageSize);
    canvas.clear(Rgba{28, 28, 32, 255});

    for (std::size_t atlasIndex = 0; atlasIndex < atlases.size(); ++atlasIndex) {
        const LightmapAtlasPlan& atlas = atlases[atlasIndex];
        if (atlas.size == 0u) {
            return false;
        }
        const int32_t panelX = static_cast<int32_t>(atlasIndex * imageSize);
        canvas.strokeRect(panelX, 0, static_cast<int32_t>(imageSize), static_cast<int32_t>(imageSize), 2,
                          Rgba{92, 92, 100, 255});
    }

    for (const LightmapUvDumpObject& object : objects) {
        if (object.atlasIndex >= atlases.size()) {
            return false;
        }
        const LightmapAtlasPlan& atlas = atlases[object.atlasIndex];
        if (object.rect.size == 0u || object.rect.x + object.rect.size > atlas.size ||
            object.rect.y + object.rect.size > atlas.size) {
            return false;
        }

        const double scale = static_cast<double>(imageSize) / static_cast<double>(atlas.size);
        const int32_t panelX = static_cast<int32_t>(object.atlasIndex * imageSize);
        const int32_t rectX = panelX + static_cast<int32_t>(std::lround(object.rect.x * scale));
        const int32_t rectY = static_cast<int32_t>(std::lround(object.rect.y * scale));
        const int32_t rectSize = std::max(1, static_cast<int32_t>(std::lround(object.rect.size * scale)));
        const Rgba color = objectColor(object.name);

        canvas.fillRect(rectX, rectY, rectSize, rectSize, withAlpha(color, 72));
        canvas.strokeRect(rectX, rectY, rectSize, rectSize, 2, withAlpha(color, 230));

        if (object.mesh == nullptr || object.mesh->indices.size() < 3u ||
            object.mesh->vertices.empty()) {
            continue;
        }
        const Rgba uvColor{255, 232, 96, 220};
        for (std::size_t triangle = 0; triangle + 2u < object.mesh->indices.size(); triangle += 3u) {
            Vec2 points[3];
            bool valid = true;
            for (std::size_t corner = 0; corner < 3u; ++corner) {
                const uint32_t vertexIndex = object.mesh->indices[triangle + corner];
                if (vertexIndex >= object.mesh->vertices.size()) {
                    valid = false;
                    break;
                }
                const Vec2 uv = object.mesh->vertices[vertexIndex].texcoord1;
                points[corner] = Vec2(object.rect.x + uv.x * object.rect.size,
                                      object.rect.y + (1.0f - uv.y) * object.rect.size);
            }
            if (!valid) {
                continue;
            }
            for (std::size_t edge = 0; edge < 3u; ++edge) {
                const Vec2& a = points[edge];
                const Vec2& b = points[(edge + 1u) % 3u];
                canvas.line(panelX + static_cast<int32_t>(std::lround(a.x * scale)),
                            static_cast<int32_t>(std::lround(a.y * scale)),
                            panelX + static_cast<int32_t>(std::lround(b.x * scale)),
                            static_cast<int32_t>(std::lround(b.y * scale)), uvColor);
            }
        }
    }

    return writePng(path, canvas);
}

} // namespace hbg::dawn
