#include "hbg/dawn/dawn_export.h"

#include "asset/texture_loader.h"
#include "core/bounds.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace hbg::dawn {
namespace {

Matrix4x4 toMatrix4x4(const Mat4& matrix) {
    Matrix4x4 result;
    for (size_t i = 0; i < 16; ++i) {
        result.mm[i] = matrix.values[i];
    }
    return result;
}

Mat4 transpose(const Mat4& matrix) {
    Mat4 result;
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            result.values[column * 4 + row] = matrix.values[row * 4 + column];
        }
    }
    return result;
}

Float2 toFloat2(const Vec2& value) {
    return Float2{value.x, value.y};
}

Float3 toFloat3(const Vec3& value) {
    return Float3{value.x, value.y, value.z};
}

struct UvBounds {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 1.0f;
    float maxY = 1.0f;
};

UvBounds calculateUvBounds(const asset::Mesh& mesh, bool useUv2) {
    if (mesh.vertices.empty()) {
        return {};
    }

    UvBounds bounds;
    bounds.minX = std::numeric_limits<float>::max();
    bounds.minY = std::numeric_limits<float>::max();
    bounds.maxX = -std::numeric_limits<float>::max();
    bounds.maxY = -std::numeric_limits<float>::max();

    for (const asset::MeshVertex& vertex : mesh.vertices) {
        const Vec2 uv = useUv2 ? vertex.texcoord1 : vertex.texcoord0;
        bounds.minX = std::min(bounds.minX, uv.x);
        bounds.minY = std::min(bounds.minY, uv.y);
        bounds.maxX = std::max(bounds.maxX, uv.x);
        bounds.maxY = std::max(bounds.maxY, uv.y);
    }

    if (bounds.maxX - bounds.minX > 1.0f) {
        bounds.minX = 0.0f;
        bounds.maxX = 1.0f;
    }
    if (bounds.maxY - bounds.minY > 1.0f) {
        bounds.minY = 0.0f;
        bounds.maxY = 1.0f;
    }
    return bounds;
}

struct ColorRGBA {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct ColorBuffer {
    uint32_t width = 1;
    uint32_t height = 1;
    std::vector<ColorRGBA> pixels;
};

float srgbToLinear(float value) {
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

uint8_t toByte(float value) {
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

ColorRGBA readPixel(const asset::ImageAsset& image, uint32_t x, uint32_t y, bool flipY) {
    const uint32_t row = flipY ? (image.height - 1u - y) : y;
    const std::size_t index = (static_cast<std::size_t>(row) * image.width + x) * 4u;
    ColorRGBA color;
    if (image.format == gfx::TextureFormat::RGBA32F) {
        float values[4] = {};
        std::memcpy(values, image.pixels.data() + index * sizeof(float), sizeof(values));
        color = ColorRGBA{values[0], values[1], values[2], values[3]};
    } else {
        color = ColorRGBA{image.pixels[index + 0] / 255.0f, image.pixels[index + 1] / 255.0f,
                          image.pixels[index + 2] / 255.0f, image.pixels[index + 3] / 255.0f};
    }
    return color;
}

const asset::ImageAsset* findImage(const render::RenderScene& scene, const asset::TextureRef& reference) {
    if (reference.imageIndex < 0 || static_cast<std::size_t>(reference.imageIndex) >= scene.images.size()) {
        return nullptr;
    }
    const asset::ImageAsset& image = scene.images[static_cast<std::size_t>(reference.imageIndex)];
    return image.valid() ? &image : nullptr;
}

ColorBuffer buildColorBuffer(const asset::ImageAsset* image, const Vec4& factor, bool srgb,
                             bool flipY) {
    ColorBuffer buffer;
    buffer.width = image != nullptr ? image->width : 1u;
    buffer.height = image != nullptr ? image->height : 1u;
    buffer.pixels.resize(static_cast<std::size_t>(buffer.width) * buffer.height);

    for (uint32_t y = 0; y < buffer.height; ++y) {
        for (uint32_t x = 0; x < buffer.width; ++x) {
            ColorRGBA color{1.0f, 1.0f, 1.0f, 1.0f};
            if (image != nullptr) {
                color = readPixel(*image, x, y, flipY);
            }
            if (srgb && (image == nullptr || image->format == gfx::TextureFormat::RGBA8)) {
                color.r = srgbToLinear(color.r);
                color.g = srgbToLinear(color.g);
                color.b = srgbToLinear(color.b);
            }
            color.r *= factor.x;
            color.g *= factor.y;
            color.b *= factor.z;
            color.a *= factor.w;
            buffer.pixels[static_cast<std::size_t>(y) * buffer.width + x] = color;
        }
    }
    return buffer;
}

Texture2D<MaterialColor> makeTexture(const ColorBuffer& buffer) {
    Texture2D<MaterialColor> texture;
    texture.header.sizeX = buffer.width;
    texture.header.sizeY = buffer.height;
    texture.header.wrapMode = 0;
    texture.header.format = kTextureFormatUByte4;
    texture.colors.resize(buffer.pixels.size());
    for (std::size_t i = 0; i < buffer.pixels.size(); ++i) {
        const ColorRGBA& color = buffer.pixels[i];
        texture.colors[i] = MaterialColor{toByte(color.r), toByte(color.g), toByte(color.b), toByte(color.a)};
    }
    return texture;
}

Texture2D<MaterialColor> makeSolidTexture(const ColorRGBA& color) {
    ColorBuffer buffer;
    buffer.pixels.push_back(color);
    return makeTexture(buffer);
}

ShaHash hashMaterial(const asset::Material& material) {
    uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    };

    mix(material.name.data(), material.name.size());
    mix(&material.type, sizeof(material.type));
    mix(&material.alphaMode, sizeof(material.alphaMode));
    mix(&material.doubleSided, sizeof(material.doubleSided));
    mix(&material.baseColorFactor, sizeof(material.baseColorFactor));
    mix(&material.metallic, sizeof(material.metallic));
    mix(&material.roughness, sizeof(material.roughness));
    mix(&material.emissiveFactor, sizeof(material.emissiveFactor));
    mix(&material.emissiveStrength, sizeof(material.emissiveStrength));
    mix(&material.normalScale, sizeof(material.normalScale));
    mix(&material.alphaCutoff, sizeof(material.alphaCutoff));
    mix(&material.transmission, sizeof(material.transmission));
    mix(&material.ior, sizeof(material.ior));

    ShaHash result;
    for (std::size_t i = 0; i < sizeof(result.hash); ++i) {
        result.hash[i] = static_cast<uint8_t>((hash >> ((i % 8) * 8)) & 0xFFu);
        if (i % 8 == 7) {
            hash ^= i;
            hash *= 1099511628211ull;
        }
    }
    return result;
}

constexpr float kPi = 3.14159265358979323846f;

float degreesToRadians(float degrees) {
    return degrees * (kPi / 180.0f);
}

float radiansToDegrees(float radians) {
    return radians * (180.0f / kPi);
}

uint32_t hashString32(const std::string& text) {
    uint32_t hash = 2166136261u;
    for (unsigned char value : text) {
        hash ^= value;
        hash *= 16777619u;
    }
    return hash;
}

Vec3 normalizeOrDefault(const Vec3& value, const Vec3& fallback) {
    const float magnitude = length(value);
    return magnitude > 1.0e-8f ? value * (1.0f / magnitude) : fallback;
}

Vec3 convertLightColor(const Vec3& color, bool convertSrgb) {
    if (!convertSrgb) {
        return color;
    }
    return Vec3(srgbToLinear(color.x), srgbToLinear(color.y), srgbToLinear(color.z));
}

Vec3 correlatedColorTemperatureToRgb(float kelvin) {
    const float temp = std::max(kelvin, 1.0f) / 100.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    if (temp <= 66.0f) {
        red = 255.0f;
        green = 99.4708025861f * std::log(temp) - 161.1195681661f;
        if (temp <= 19.0f) {
            blue = 0.0f;
        } else {
            blue = 138.5177312231f * std::log(temp - 10.0f) - 305.0447927307f;
        }
    } else {
        red = 329.698727446f * std::pow(temp - 60.0f, -0.1332047592f);
        green = 288.1221695283f * std::pow(temp - 60.0f, -0.0755148492f);
        blue = 255.0f;
    }
    return Vec3(std::clamp(red / 255.0f, 0.0f, 1.0f),
                std::clamp(green / 255.0f, 0.0f, 1.0f),
                std::clamp(blue / 255.0f, 0.0f, 1.0f));
}

float luminance(const Vec3& color) {
    return 0.299f * color.x + 0.587f * color.y + 0.114f * color.z;
}

uint32_t makeLightId(const render::Light& light, uint32_t lightIndex) {
    if (light.instanceId != 0u) {
        return light.instanceId;
    }
    const uint32_t id = hashString32(light.name);
    return id != 0u ? id : (0x9E3779B9u ^ (lightIndex + 1u));
}

Mat4 makeEulerRotation(const Vec3& degrees) {
    const float cx = std::cos(degreesToRadians(degrees.x));
    const float sx = std::sin(degreesToRadians(degrees.x));
    const float cy = std::cos(degreesToRadians(degrees.y));
    const float sy = std::sin(degreesToRadians(degrees.y));
    const float cz = std::cos(degreesToRadians(degrees.z));
    const float sz = std::sin(degreesToRadians(degrees.z));

    Mat4 rotationX = Mat4::identity();
    rotationX[5] = cx;
    rotationX[6] = sx;
    rotationX[9] = -sx;
    rotationX[10] = cx;

    Mat4 rotationY = Mat4::identity();
    rotationY[0] = cy;
    rotationY[2] = -sy;
    rotationY[8] = sy;
    rotationY[10] = cy;

    Mat4 rotationZ = Mat4::identity();
    rotationZ[0] = cz;
    rotationZ[1] = sz;
    rotationZ[4] = -sz;
    rotationZ[5] = cz;

    return rotationY * rotationX * rotationZ;
}

uint32_t skyMipCount(uint32_t cubeSize) {
    uint32_t mipCount = 1;
    for (uint32_t size = cubeSize; size > 1u; size = (size + 1u) / 2u) {
        ++mipCount;
    }
    return mipCount;
}

Vec3 cubeFaceDirection(uint32_t face, uint32_t x, uint32_t y, uint32_t cubeSize) {
    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(cubeSize) * 2.0f - 1.0f;
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(cubeSize) * 2.0f - 1.0f;
    switch (face) {
    case 0:
        return Vec3(1.0f, -v, -u);
    case 1:
        return Vec3(-1.0f, -v, u);
    case 2:
        return Vec3(u, 1.0f, v);
    case 3:
        return Vec3(u, -1.0f, -v);
    case 4:
        return Vec3(u, -v, 1.0f);
    default:
        return Vec3(-u, -v, -1.0f);
    }
}

bool isCubemapStrip(const asset::ImageAsset& image) {
    return image.height > 0u && image.width == image.height * 6u;
}

ColorRGBA sampleEquirect(const asset::ImageAsset& image, const Vec3& direction) {
    const Vec3 d = normalize(direction);
    const float u = std::atan2(d.x, d.z) / (2.0f * kPi) + 0.5f;
    const float v = std::acos(std::clamp(d.y, -1.0f, 1.0f)) / kPi;
    const float pixelX = u * static_cast<float>(image.width) - 0.5f;
    const float pixelY = v * static_cast<float>(image.height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(pixelX));
    const int y0 = static_cast<int>(std::floor(pixelY));
    const float fx = pixelX - static_cast<float>(x0);
    const float fy = pixelY - static_cast<float>(y0);
    const auto wrapX = [width = static_cast<int>(image.width)](int value) {
        const int wrapped = value % width;
        return wrapped < 0 ? wrapped + width : wrapped;
    };
    const auto clampY = [height = static_cast<int>(image.height)](int value) {
        return std::clamp(value, 0, height - 1);
    };

    const ColorRGBA c00 = readPixel(image, static_cast<uint32_t>(wrapX(x0)), static_cast<uint32_t>(clampY(y0)), false);
    const ColorRGBA c10 = readPixel(image, static_cast<uint32_t>(wrapX(x0 + 1)), static_cast<uint32_t>(clampY(y0)), false);
    const ColorRGBA c01 = readPixel(image, static_cast<uint32_t>(wrapX(x0)), static_cast<uint32_t>(clampY(y0 + 1)), false);
    const ColorRGBA c11 = readPixel(image, static_cast<uint32_t>(wrapX(x0 + 1)), static_cast<uint32_t>(clampY(y0 + 1)), false);
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    return ColorRGBA{lerp(lerp(c00.r, c10.r, fx), lerp(c01.r, c11.r, fx), fy),
                     lerp(lerp(c00.g, c10.g, fx), lerp(c01.g, c11.g, fx), fy),
                     lerp(lerp(c00.b, c10.b, fx), lerp(c01.b, c11.b, fx), fy), 1.0f};
}

ColorRGBA sampleCubemapStrip(const asset::ImageAsset& image, const Vec3& direction) {
    const Vec3 d = normalize(direction);
    const float ax = std::fabs(d.x);
    const float ay = std::fabs(d.y);
    const float az = std::fabs(d.z);
    uint32_t face = 0;
    float u = 0.0f;
    float v = 0.0f;
    if (ax >= ay && ax >= az) {
        face = d.x >= 0.0f ? 0u : 1u;
        u = d.x >= 0.0f ? -d.z / ax : d.z / ax;
        v = -d.y / ax;
    } else if (ay >= az) {
        face = d.y >= 0.0f ? 2u : 3u;
        u = d.x / ay;
        v = d.y >= 0.0f ? d.z / ay : -d.z / ay;
    } else {
        face = d.z >= 0.0f ? 4u : 5u;
        u = d.z >= 0.0f ? d.x / az : -d.x / az;
        v = -d.y / az;
    }

    const float faceWidth = static_cast<float>(image.width) / 6.0f;
    const float pixelX = (u * 0.5f + 0.5f) * faceWidth - 0.5f;
    const float pixelY = (0.5f - v * 0.5f) * static_cast<float>(image.height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(pixelX));
    const int y0 = static_cast<int>(std::floor(pixelY));
    const float fx = pixelX - static_cast<float>(x0);
    const float fy = pixelY - static_cast<float>(y0);
    const auto clampX = [faceWidth](int value) {
        return std::clamp(value, 0, static_cast<int>(faceWidth) - 1);
    };
    const auto clampY = [height = static_cast<int>(image.height)](int value) {
        return std::clamp(value, 0, height - 1);
    };
    const uint32_t faceOffset = face * static_cast<uint32_t>(faceWidth);
    const ColorRGBA c00 = readPixel(image, faceOffset + static_cast<uint32_t>(clampX(x0)),
                                    static_cast<uint32_t>(clampY(y0)), false);
    const ColorRGBA c10 = readPixel(image, faceOffset + static_cast<uint32_t>(clampX(x0 + 1)),
                                    static_cast<uint32_t>(clampY(y0)), false);
    const ColorRGBA c01 = readPixel(image, faceOffset + static_cast<uint32_t>(clampX(x0)),
                                    static_cast<uint32_t>(clampY(y0 + 1)), false);
    const ColorRGBA c11 = readPixel(image, faceOffset + static_cast<uint32_t>(clampX(x0 + 1)),
                                    static_cast<uint32_t>(clampY(y0 + 1)), false);
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    return ColorRGBA{lerp(lerp(c00.r, c10.r, fx), lerp(c01.r, c11.r, fx), fy),
                     lerp(lerp(c00.g, c10.g, fx), lerp(c01.g, c11.g, fx), fy),
                     lerp(lerp(c00.b, c10.b, fx), lerp(c01.b, c11.b, fx), fy), 1.0f};
}

ColorRGBA sampleSkySource(const asset::ImageAsset& image, const Vec3& direction, const Mat4& rotation,
                          bool convertSrgb) {
    const Vec3 rotated = transformDirection(rotation, normalize(direction));
    ColorRGBA color = isCubemapStrip(image) ? sampleCubemapStrip(image, rotated)
                                            : sampleEquirect(image, rotated);
    if (convertSrgb && image.format == gfx::TextureFormat::RGBA8) {
        color.r = srgbToLinear(color.r);
        color.g = srgbToLinear(color.g);
        color.b = srgbToLinear(color.b);
    }
    return color;
}

}  // namespace

Guid makeMeshGuid(uint32_t meshIndex) {
    return Guid{0x4D455348u, 0u, 0u, meshIndex + 1u};
}

Guid makeInstanceGuid(uint32_t instanceIndex) {
    return Guid{0x494E5354u, 0u, 0u, instanceIndex + 1u};
}

MeshInfo exportMesh(const asset::Mesh& mesh, uint32_t lodIndex, const Guid& guid,
                    const MeshExportOptions& options) {
    if (mesh.vertices.empty()) {
        throw std::runtime_error("mesh has no vertices");
    }
    if (mesh.indices.empty()) {
        throw std::runtime_error("mesh has no indices");
    }
    if (!mesh.hasNormals) {
        throw std::runtime_error("mesh has no normals (call generateNormals first)");
    }
    if (!mesh.hasTangents) {
        throw std::runtime_error("mesh has no tangents (call generateTangents first)");
    }
    if (!mesh.hasUv1 && !mesh.hasUv0) {
        throw std::runtime_error("mesh has neither UV0 nor UV1");
    }

    MeshInfo result;
    result.header.guid = guid;
    result.header.lodIndex = lodIndex;
    result.header.lightmapUVIndex = 1;
    result.header.lightmapUVDensity = 1.0f;
    result.header.lightmapTexelDensity = 1.0f;
    result.header.numElements =
        mesh.submeshes.empty() ? 1u : static_cast<uint32_t>(mesh.submeshes.size());
    result.header.numVertices = static_cast<uint32_t>(mesh.vertices.size());
    result.header.numTriangles = static_cast<uint32_t>(mesh.indices.size() / 3);

    const bool hasUv2 = mesh.hasUv1;
    const UvBounds uvBounds = calculateUvBounds(mesh, hasUv2);
    const float uvWidth = std::max(uvBounds.maxX - uvBounds.minX, 1.0e-8f);
    const float uvHeight = std::max(uvBounds.maxY - uvBounds.minY, 1.0e-8f);

    std::vector<Float2> uv0;
    std::vector<Float2> uv2Normalized;
    std::vector<Float2> uv2Original;
    uv0.reserve(mesh.vertices.size());
    uv2Normalized.reserve(mesh.vertices.size());
    uv2Original.reserve(mesh.vertices.size());

    for (const asset::MeshVertex& vertex : mesh.vertices) {
        const Vec2 uv0Value = mesh.hasUv0 ? vertex.texcoord0 : Vec2{0.0f, 0.0f};
        const Vec2 uv2Value = hasUv2 ? vertex.texcoord1 : uv0Value;

        const Float3 normal = toFloat3(vertex.normal);
        const Vec3 bitangent{vertex.tangent.x, vertex.tangent.y, vertex.tangent.z};
        const Vec3 tangent = hbg::cross(bitangent, vertex.normal);

        result.vertices.push_back(Float4{vertex.position.x, vertex.position.y, vertex.position.z, 1.0f});
        result.normals.push_back(Float4{normal.x, normal.y, normal.z, 0.0f});
        result.tangents.push_back(Float4{tangent.x, tangent.y, tangent.z, 0.0f});
        result.bitangents.push_back(Float4{bitangent.x, bitangent.y, bitangent.z, 0.0f});

        uv0.push_back(toFloat2(uv0Value));
        if (options.normalizeUv2) {
            uv2Normalized.push_back(Float2{(uv2Value.x - uvBounds.minX) / uvWidth,
                                           (uv2Value.y - uvBounds.minY) / uvHeight});
        } else {
            uv2Normalized.push_back(toFloat2(uv2Value));
        }
        uv2Original.push_back(toFloat2(uv2Value));
    }

    if (options.useObjectMeshForMetaPass) {
        result.texcoords.push_back(uv2Original);
    } else {
        result.texcoords.push_back(uv0);
    }
    result.texcoords.push_back(uv2Normalized);
    result.texcoords.push_back(uv2Original);

    const bool use16Bit = mesh.vertices.size() <= 0xFFFFu && result.header.numTriangles * 3u < 0xFFFFu;
    uint32_t startIndex = 0;

    if (mesh.submeshes.empty()) {
        const uint32_t primitiveCount = static_cast<uint32_t>(mesh.indices.size() / 3);
        for (uint32_t triangle = 0; triangle < primitiveCount; ++triangle) {
            const uint32_t v0 = mesh.indices[triangle * 3 + 0];
            const uint32_t v1 = mesh.indices[triangle * 3 + 2];
            const uint32_t v2 = mesh.indices[triangle * 3 + 1];
            if (use16Bit) {
                result.index16.push_back(static_cast<uint16_t>(v0));
                result.index16.push_back(static_cast<uint16_t>(v1));
                result.index16.push_back(static_cast<uint16_t>(v2));
            } else {
                result.index32.push_back(v0);
                result.index32.push_back(v1);
                result.index32.push_back(v2);
            }
        }
        result.elements.push_back(MeshElementInfo{0, primitiveCount, 0xFFFFu, 0u});
        return result;
    }

    for (const asset::SubMesh& submesh : mesh.submeshes) {
        if (submesh.indexOffset > mesh.indices.size()) {
            throw std::runtime_error("submesh indexOffset out of range");
        }
        const uint32_t available = static_cast<uint32_t>(mesh.indices.size() - submesh.indexOffset);
        const uint32_t indexCount = std::min(submesh.indexCount, available);
        const uint32_t primitiveCount = indexCount / 3;

        for (uint32_t triangle = 0; triangle < primitiveCount; ++triangle) {
            const uint32_t base = submesh.indexOffset + triangle * 3;
            const uint32_t v0 = mesh.indices[base + 0];
            const uint32_t v1 = mesh.indices[base + 2];
            const uint32_t v2 = mesh.indices[base + 1];
            if (use16Bit) {
                result.index16.push_back(static_cast<uint16_t>(v0));
                result.index16.push_back(static_cast<uint16_t>(v1));
                result.index16.push_back(static_cast<uint16_t>(v2));
            } else {
                result.index32.push_back(v0);
                result.index32.push_back(v1);
                result.index32.push_back(v2);
            }
        }

        result.elements.push_back(MeshElementInfo{startIndex, primitiveCount, 0xFFFFu, 0u});
        startIndex += primitiveCount;
    }

    return result;
}

MeshInstanceInfo exportMeshInstance(const asset::Mesh& mesh, const render::RenderObject& object,
                                    uint32_t meshIndex, uint32_t instanceIndex,
                                    const MeshExportOptions& options) {
    MeshInstanceInfo result;
    result.header.guid = makeInstanceGuid(instanceIndex);
    result.header.levelGuid = Guid{};
    result.header.diffuseBoost = options.albedoBoost;
    result.header.emissiveBoost = 1.0f;
    result.header.rayOffset = options.rayOffset;
    result.header.meshIndex = meshIndex;
    result.header.lightmapId = meshIndex;
    result.header.texcoordIndex = 0;
    result.header.fullyOccludedSamplesFraction = 1.0f;
    result.header.flags = options.castShadows ? kMeshInstanceFlagShadowCast : 0u;
    if (options.useEmissionLighting) {
        result.header.flags |= kMeshInstanceFlagUseEmissionLighting;
    }
    result.header.meshInstanceVisibilityMask = options.meshInstanceVisibilityMask;
    result.header.meshInstanceVisibilityMaskInHlodScene = options.meshInstanceVisibilityMask;
    result.header.lodLevelInHlodScene = -1;
    result.header.uv2MapSize = options.uv2MapSize;
    result.header.uv2Offset = options.uv2Offset;

    Bounds worldBounds = object.worldBounds;
    if (!worldBounds.valid()) {
        worldBounds = transformBounds(object.world, mesh.bounds);
    }
    result.header.boundingBox = SceneBoxBounds{toFloat3(worldBounds.min), toFloat3(worldBounds.max)};

    result.header.transform = toMatrix4x4(object.world);
    const Mat4 inverseWorld = inverse(object.world);
    result.header.inverseTransform = toMatrix4x4(inverseWorld);
    result.header.inverseTransposeTransform = toMatrix4x4(transpose(inverseWorld));

    result.materialOverrides.reserve(mesh.submeshes.empty() ? 1 : mesh.submeshes.size());
    if (mesh.submeshes.empty()) {
        result.materialOverrides.push_back(0);
    } else {
        for (const asset::SubMesh& submesh : mesh.submeshes) {
            result.materialOverrides.push_back(submesh.materialIndex);
        }
    }

    return result;
}

MaterialInfo exportMaterial(const asset::Material& material, const render::RenderScene& scene,
                            const MaterialExportOptions& options) {
    MaterialInfo result;
    result.header.hash = hashMaterial(material);
    result.header.shadingModelId = 0;
    result.header.blendMode = material.alphaMode == asset::AlphaMode::Mask
                                  ? 1u
                                  : (material.alphaMode == asset::AlphaMode::Blend ? 2u : 0u);
    result.header.bTwoSided = material.doubleSided ? 1u : 0u;
    result.header.flags = material.doubleSided ? kMaterialFlagTwoSide : 0u;
    result.header.opacityMaskClipValue =
        material.alphaMode == asset::AlphaMode::Mask ? material.alphaCutoff : 0.0f;
    result.header.diffuseBoost = 1.0f;
    result.header.emissiveBoost = 1.0f;

    const asset::ImageAsset* diffuseImage = findImage(scene, material.baseColorTexture);
    const ColorBuffer diffuse =
        buildColorBuffer(diffuseImage, material.baseColorFactor, options.srgbToLinear, false);
    result.diffuseTexture = makeTexture(diffuse);

    const bool needsTransmission =
        material.alphaMode != asset::AlphaMode::Opaque || material.transmission > 0.0f;
    if (needsTransmission) {
        result.header.flags |= kMaterialFlagSurfaceDomain;
        if (material.alphaMode == asset::AlphaMode::Mask) {
            ColorBuffer transmission = diffuse;
            for (ColorRGBA& color : transmission.pixels) {
                color = ColorRGBA{color.a, color.a, color.a, color.a};
            }
            result.transmissionTexture = makeTexture(transmission);
        } else if (material.alphaMode == asset::AlphaMode::Blend) {
            ColorBuffer transmission = diffuse;
            for (ColorRGBA& color : transmission.pixels) {
                const float alpha = color.a;
                color.r *= (1.0f - alpha);
                color.g *= (1.0f - alpha);
                color.b *= (1.0f - alpha);
                color.a = 1.0f - alpha;
            }
            result.transmissionTexture = makeTexture(transmission);
        } else {
            const float transmission = std::clamp(material.transmission, 0.0f, 1.0f);
            result.transmissionTexture =
                makeSolidTexture(ColorRGBA{transmission, transmission, transmission, transmission});
        }
    }

    const bool hasEmissive = material.emissiveTexture.valid() || material.emissiveFactor.x > 0.0f ||
                             material.emissiveFactor.y > 0.0f || material.emissiveFactor.z > 0.0f;
    if (hasEmissive) {
        const asset::ImageAsset* emissiveImage = findImage(scene, material.emissiveTexture);
        const Vec4 emissiveFactor{material.emissiveFactor.x * material.emissiveStrength,
                                  material.emissiveFactor.y * material.emissiveStrength,
                                  material.emissiveFactor.z * material.emissiveStrength, 1.0f};
        ColorBuffer emissive =
            buildColorBuffer(emissiveImage, emissiveFactor, options.srgbToLinear, false);
        constexpr float kInvPi = 1.0f / 3.14159265358979323846f;
        float maxValue = 0.0f;
        for (ColorRGBA& color : emissive.pixels) {
            color.r *= kInvPi;
            color.g *= kInvPi;
            color.b *= kInvPi;
            maxValue = std::max(maxValue, std::max(color.r, std::max(color.g, color.b)));
        }
        if (maxValue <= 1.0e-8f) {
            maxValue = 1.0f;
        }
        const float scale = 1.0f / maxValue;
        for (ColorRGBA& color : emissive.pixels) {
            color.r *= scale;
            color.g *= scale;
            color.b *= scale;
        }
        result.emissiveTexture = makeTexture(emissive);
        result.header.emissiveBoost = maxValue;
    }

    if (const asset::ImageAsset* normalImage = findImage(scene, material.normalTexture)) {
        ColorBuffer normal =
            buildColorBuffer(normalImage, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, false, false);
        if (options.flipNormalY) {
            for (ColorRGBA& color : normal.pixels) {
                color.g = 1.0f - color.g;
            }
        }
        result.normalTexture = makeTexture(normal);
    }

    ColorBuffer specular;
    if (const asset::ImageAsset* metallicRoughness = findImage(scene, material.metallicRoughnessTexture)) {
        specular.width = metallicRoughness->width;
        specular.height = metallicRoughness->height;
        specular.pixels.resize(static_cast<std::size_t>(specular.width) * specular.height);
        for (uint32_t y = 0; y < specular.height; ++y) {
            for (uint32_t x = 0; x < specular.width; ++x) {
                const ColorRGBA source = readPixel(*metallicRoughness, x, y, false);
                const float roughness = std::clamp(source.g * material.roughness, 0.0f, 1.0f);
                const float metallic = std::clamp(source.b * material.metallic, 0.0f, 1.0f);
                specular.pixels[static_cast<std::size_t>(y) * specular.width + x] =
                    ColorRGBA{options.specular, roughness, metallic, options.ior};
            }
        }
    } else {
        specular.pixels.push_back(
            ColorRGBA{options.specular, material.roughness, material.metallic, options.ior});
    }
    result.specularTexture = makeTexture(specular);

    return result;
}

LightFullInfo exportLight(const render::Light& light, const LightExportOptions& options,
                          uint32_t lightIndex,
                          std::vector<Texture2D<Float4>>* cookieTextures) {
    LightFullInfo result;

    const Mat4& world = light.world;
    const Vec3 position = transformPoint(world, Vec3(0.0f, 0.0f, 0.0f));
    const Vec3 right =
        normalizeOrDefault(transformDirection(world, Vec3(1.0f, 0.0f, 0.0f)), Vec3(1.0f, 0.0f, 0.0f));
    const Vec3 up =
        normalizeOrDefault(transformDirection(world, Vec3(0.0f, 1.0f, 0.0f)), Vec3(0.0f, 1.0f, 0.0f));
    const Vec3 engineForward =
        normalizeOrDefault(transformDirection(world, Vec3(0.0f, 0.0f, -1.0f)), Vec3(0.0f, 0.0f, -1.0f));

    const bool directional = light.type == render::LightType::Directional;
    const Vec3 normal = directional ? engineForward * -1.0f : engineForward;

    Vec3 sourceColor = light.color;
    if (light.useColorTemperature) {
        const Vec3 temperatureColor =
            correlatedColorTemperatureToRgb(light.colorTemperature);
        sourceColor = Vec3(sourceColor.x * temperatureColor.x,
                           sourceColor.y * temperatureColor.y,
                           sourceColor.z * temperatureColor.z);
    }
    const Vec3 color = convertLightColor(sourceColor, options.srgbToLinear);
    const Vec3 directColor = color * light.intensity;
    const Vec3 indirectColor =
        color * (light.intensity * std::max(light.indirectMultiplier, 0.0f) *
                 options.indirectMultiplier * options.indirectIntensity);

    result.lightData.position = toFloat3(position);
    result.lightData.normal = toFloat3(normal);
    result.lightData.dPdu = toFloat3(right);
    result.lightData.dPdv = toFloat3(up);
    result.lightData.color = toFloat3(directColor);
    result.lightData.indirectColor = toFloat3(indirectColor);
    result.lightData.attenuation = 1.0f;
    result.lightData.falloffExponent = 0.0f;
    result.lightData.iesTextureSlice = -1;
    const bool hasCookie = light.cookieType != render::LightCookieType::None &&
                           light.cookieMode != render::LightCookieMode::None;
    result.lightData.lightTextureMode =
        (hasCookie || light.hasIes) ? 1 : 0;
    if (cookieTextures != nullptr && (light.hasIes || hasCookie)) {
        constexpr uint32_t kLegacyCookieSize = 8;
        uint32_t width = light.cookieNativeWidth;
        uint32_t height = light.cookieNativeHeight;
        const bool hasNative =
            width > 0 && height > 0 &&
            light.cookieNative.size() == static_cast<std::size_t>(width) * height;
        if (!hasNative) {
            width = kLegacyCookieSize;
            height = kLegacyCookieSize;
        }
        Texture2D<Float4> texture;
        texture.header.sizeX = width;
        texture.header.sizeY = height;
        texture.header.wrapMode = 0;
        texture.header.format = 0;
        texture.colors.resize(static_cast<std::size_t>(width) * height);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const float value =
                    hasNative
                        ? light.cookieNative[static_cast<std::size_t>(y) * width + x]
                        : light.cookieSamples[static_cast<std::size_t>(y) * kLegacyCookieSize + x];
                texture.colors[static_cast<std::size_t>(y) * width + x] =
                    Float4{value, value, value, 1.0f};
            }
        }
        result.lightData.iesTextureSlice = static_cast<int32_t>(cookieTextures->size());
        cookieTextures->push_back(std::move(texture));
    }
    result.lightData.shadowTranslucency = std::clamp(light.shadowTranslucency, 0.0f, 1.0f);
    result.lightData.bEnablePenumbra = light.enablePenumbra ? 1u : 0u;
    result.lightData.penumbraTint = Float4{0.0f, 0.0f, 0.0f, 0.0f};
    result.lightData.penumbraLuma = 0.0f;
    if (light.enablePenumbra) {
        result.lightData.penumbraTint =
            Float4{light.penumbraTint.x, light.penumbraTint.y, light.penumbraTint.z, light.penumbraTint.w};
        result.lightData.penumbraLuma = light.penumbraLuma;
    }

    uint32_t typeFlag = kLightFlagPoint;
    if (directional) {
        typeFlag = kLightFlagDirectional;
    } else if (light.type == render::LightType::Spot) {
        typeFlag = kLightFlagSpot;
    } else if (light.type == render::LightType::Area) {
        typeFlag = light.areaShape == render::LightAreaShape::Disc ? kLightFlagDisc : kLightFlagRect;
    }

    result.lightData.flags = typeFlag | kLightFlagHasStaticLighting | kLightFlagIndirectLighting |
                             kLightFlagIndirectLightingProbe;
    if (options.directLighting && light.bakeMode == render::LightBakeMode::Baked) {
        result.lightData.flags |= kLightFlagDirectLighting | kLightFlagDirectLightingProbe;
    }
    if (light.bakeMode == render::LightBakeMode::Mixed) {
        result.lightData.flags |= kLightFlagStationary;
        if (light.mixedMode == render::LightMixedMode::ShadowMask) {
            result.lightData.flags |= kLightFlagDistanceFieldShadow;
            result.lightData.flags &= ~kLightFlagLightingChannelMask;
            result.lightData.flags |= static_cast<uint32_t>(light.lightingChannel & 0x7u);
        }
    }
    if (light.castShadows && light.shadowType != render::LightShadowType::None) {
        result.lightData.flags |= kLightFlagCastShadow;
    }
    const bool area = light.type == render::LightType::Area;
    const bool inverseSquared =
        area || (!directional && light.falloffMode == render::LightFalloffMode::InverseSquared);
    if (!inverseSquared) {
        result.lightData.flags |= kLightFlagNonInverseSquareFalloff;
    }
    if (!directional) {
        if (light.falloffMode == render::LightFalloffMode::BakeryPhysical) {
            result.lightData.flags |= kLightFlagBakeryFalloff;
        } else if (light.falloffMode == render::LightFalloffMode::BakeryFake) {
            result.lightData.flags |= kLightFlagBakeryFakeFalloff;
        }
    }

    const float range = std::max(light.range, 0.0f);
    const bool softShadows = light.shadowType == render::LightShadowType::Soft;
    const float sourceRadius =
        softShadows ? std::max(light.sourceRadius, std::max(options.sourceRadius, 0.0f)) : 0.0f;
    const float sourceLength =
        softShadows ? std::max(light.sourceLength, std::max(options.sourceLength, 0.0f)) : 0.0f;
    const float directionalSourceAngle =
        softShadows ? std::max(light.lightSourceAngle,
                               std::max(options.directionalSourceAngleDegrees, 0.0f))
                    : 0.0f;
    const float barnDoorAngle =
        light.barnDoorAngle > 0.0f ? light.barnDoorAngle : options.areaBarnDoorAngleDegrees;
    const float barnDoorLength =
        light.barnDoorLength > 0.0f ? light.barnDoorLength : options.areaBarnDoorLength;
    Vec3 boundMin{0.0f, 0.0f, 0.0f};
    Vec3 boundMax{0.0f, 0.0f, 0.0f};

    if (directional) {
        const float halfAngle = degreesToRadians(directionalSourceAngle * 0.5f);
        result.lightData.dimensions = Float3{std::sin(halfAngle), 0.0f, 0.0f};
        result.lightData.attenuation = 1.0f;
    } else if (light.type == render::LightType::Point) {
        result.lightData.dimensions = Float3{sourceRadius, sourceRadius, sourceLength};
        result.lightData.attenuation = range > 1.0e-6f ? 1.0f / range : 0.0f;
        result.lightData.falloffExponent = 1.0f;
        boundMin = position - Vec3(range, range, range);
        boundMax = position + Vec3(range, range, range);
    } else if (light.type == render::LightType::Spot) {
        result.lightData.dimensions = Float3{sourceRadius, sourceRadius, sourceLength};

        const float innerDegrees =
            std::clamp(radiansToDegrees(light.innerConeAngle), 0.0f, 89.0f);
        const float outerDegrees =
            std::clamp(radiansToDegrees(light.outerConeAngle), innerDegrees, 89.0f);
        const float cosOuter = std::cos(degreesToRadians(outerDegrees));
        const float cosInner = std::cos(degreesToRadians(innerDegrees));
        const float cosDifference = cosInner - cosOuter;
        result.lightData.shaping =
            Float2{cosOuter, std::fabs(cosDifference) > 1.0e-6f ? 1.0f / cosDifference : 0.0f};
        result.lightData.attenuation = range > 1.0e-6f ? 1.0f / range : 0.0f;
        result.lightData.falloffExponent = 8.0f;

        const Vec3 disc{std::sqrt(std::clamp(1.0f - normal.x * normal.x, 0.0f, 1.0f)),
                        std::sqrt(std::clamp(1.0f - normal.y * normal.y, 0.0f, 1.0f)),
                        std::sqrt(std::clamp(1.0f - normal.z * normal.z, 0.0f, 1.0f))};
        const Vec3 tip = position + normal * range;
        boundMin = Vec3(std::min(position.x, tip.x), std::min(position.y, tip.y),
                        std::min(position.z, tip.z));
        boundMax = Vec3(std::max(position.x, tip.x), std::max(position.y, tip.y),
                        std::max(position.z, tip.z));
        const float sinOuter = std::sqrt(std::max(1.0f - cosOuter * cosOuter, 0.0f));
        boundMin.x = std::min(boundMin.x, position.x + range * (normal.x * cosOuter - disc.x * sinOuter));
        boundMin.y = std::min(boundMin.y, position.y + range * (normal.y * cosOuter - disc.y * sinOuter));
        boundMin.z = std::min(boundMin.z, position.z + range * (normal.z * cosOuter - disc.z * sinOuter));
        boundMax.x = std::max(boundMax.x, position.x + range * (normal.x * cosOuter + disc.x * sinOuter));
        boundMax.y = std::max(boundMax.y, position.y + range * (normal.y * cosOuter + disc.y * sinOuter));
        boundMax.z = std::max(boundMax.z, position.z + range * (normal.z * cosOuter + disc.z * sinOuter));
    } else if (light.type == render::LightType::Area) {
        const bool disc = light.areaShape == render::LightAreaShape::Disc;
        if (disc) {
            const float radius = std::max(light.areaWidth, 0.0f);
            result.lightData.dimensions = Float3{radius, radius, range * 2.0f};
        } else {
            const float width = std::max(light.areaWidth, 0.0f);
            const float height = std::max(light.areaHeight, 0.0f);
            result.lightData.dimensions = Float3{width, height, width * 0.5f};
        }

        const float effectiveBarnDoorAngle =
            disc ? std::clamp(barnDoorAngle, 1.0f, 89.0f) : barnDoorAngle;
        result.lightData.shaping =
            Float2{std::cos(degreesToRadians(effectiveBarnDoorAngle)), barnDoorLength};
        result.lightData.attenuation = range > 1.0e-6f ? 1.0f / range : 0.0f;

        const Vec3 discVector{std::sqrt(std::clamp(1.0f - normal.x * normal.x, 0.0f, 1.0f)),
                              std::sqrt(std::clamp(1.0f - normal.y * normal.y, 0.0f, 1.0f)),
                              std::sqrt(std::clamp(1.0f - normal.z * normal.z, 0.0f, 1.0f))};
        const Vec3 tip = position + normal * range;
        boundMin = Vec3(std::min(tip.x, position.x - range * discVector.x),
                        std::min(tip.y, position.y - range * discVector.y),
                        std::min(tip.z, position.z - range * discVector.z));
        boundMax = Vec3(std::max(tip.x, position.x + range * discVector.x),
                        std::max(tip.y, position.y + range * discVector.y),
                        std::max(tip.z, position.z + range * discVector.z));
    }

    result.lightData.boundMin = toFloat3(boundMin);
    result.lightData.boundMax = toFloat3(boundMax);
    result.lightGuid = Guid{0u, 0u, 0u, makeLightId(light, lightIndex)};
    result.lightPower = luminance(indirectColor);
    return result;
}

SkyLightInfo exportSkyLight(const asset::ImageAsset* source, const SkyLightExportOptions& options) {
    SkyLightInfo result;
    const bool hasSource = source != nullptr && source->valid();
    const uint32_t cubeSize = hasSource ? std::clamp(options.cubeSize, 1u, 512u) : 1u;
    const uint32_t mipCount = skyMipCount(cubeSize);
    const Mat4 sourceRotation =
        makeEulerRotation(options.splitRotation ? options.sourceRotationEulerDegrees : options.rotationEulerDegrees);
    const Mat4 transformRotation =
        makeEulerRotation(options.splitRotation ? options.transformRotationEulerDegrees : options.rotationEulerDegrees);

    result.skyData.color = toFloat3(options.color * options.intensity);
    result.skyData.mipDimensions =
        Int3{static_cast<int32_t>(mipCount), static_cast<int32_t>(cubeSize), 0};
    result.skyData.envColor = toFloat3(options.envColor);
    result.skyData.transform = toMatrix4x4(hasSource ? transpose(transformRotation) : Mat4::identity());
    result.skyData.indirectLightingIntensity = options.indirectMultiplier * options.indirectIntensity;
    result.skyData.shadowTranslucency = options.shadowTranslucency;
    result.skyData.flags = 0;

    const std::size_t pixelCount = static_cast<std::size_t>(cubeSize) * cubeSize * 6u;
    result.envCube.resize(pixelCount);
    if (!hasSource) {
        for (Float4& pixel : result.envCube) {
            pixel = Float4{0.0f, 0.0f, 0.0f, 0.0f};
        }
        return result;
    }

    const float invPi = 1.0f / kPi;
    std::size_t pixelIndex = 0;
    for (uint32_t face = 0; face < 6u; ++face) {
        for (uint32_t y = 0; y < cubeSize; ++y) {
            for (uint32_t x = 0; x < cubeSize; ++x) {
                const Vec3 direction = cubeFaceDirection(face, x, y, cubeSize);
                const ColorRGBA color =
                    sampleSkySource(*source, direction, sourceRotation, options.srgbToLinear);
                result.envCube[pixelIndex++] =
                    Float4{color.r * invPi, color.g * invPi, color.b * invPi, color.a * invPi};
            }
        }
    }
    return result;
}

SkyLightInfo exportGradientSkyLight(const Vec3& skyColor, const Vec3& equatorColor, const Vec3& groundColor,
                                    float lerpExponent, const SkyLightExportOptions& options) {
    SkyLightInfo result;
    const uint32_t cubeSize = std::clamp(options.cubeSize, 1u, 512u);
    const Mat4 transformRotation =
        makeEulerRotation(options.splitRotation ? options.transformRotationEulerDegrees
                                                : options.rotationEulerDegrees);

    result.skyData.color = toFloat3(options.color * options.intensity);
    result.skyData.mipDimensions =
        Int3{static_cast<int32_t>(skyMipCount(cubeSize)), static_cast<int32_t>(cubeSize), 0};
    result.skyData.envColor = toFloat3(options.envColor);
    result.skyData.transform = toMatrix4x4(transpose(transformRotation));
    result.skyData.indirectLightingIntensity = options.indirectMultiplier * options.indirectIntensity;
    result.skyData.shadowTranslucency = options.shadowTranslucency;
    result.skyData.flags = 0;

    const float invPi = 1.0f / kPi;
    const float exponent = std::max(lerpExponent, 0.0f);
    result.envCube.resize(static_cast<std::size_t>(cubeSize) * cubeSize * 6u);
    std::size_t pixelIndex = 0;
    for (uint32_t face = 0; face < 6u; ++face) {
        for (uint32_t y = 0; y < cubeSize; ++y) {
            for (uint32_t x = 0; x < cubeSize; ++x) {
                const Vec3 direction = normalize(cubeFaceDirection(face, x, y, cubeSize));
                const Vec3 end = direction.y < 0.0f ? groundColor : skyColor;
                const float blend = std::pow(std::fabs(direction.y), exponent);
                const Vec3 color = equatorColor + (end - equatorColor) * blend;
                result.envCube[pixelIndex++] = Float4{color.x * invPi, color.y * invPi, color.z * invPi, invPi};
            }
        }
    }
    return result;
}

SkyLightInfo exportConstantSkyLight(const Vec4& radiance, const SkyLightExportOptions& options) {
    SkyLightInfo result;
    const uint32_t cubeSize = std::clamp(options.cubeSize, 1u, 512u);
    const float invPi = 1.0f / kPi;

    result.skyData.color = toFloat3(options.color * options.intensity);
    result.skyData.mipDimensions =
        Int3{static_cast<int32_t>(skyMipCount(cubeSize)), static_cast<int32_t>(cubeSize), 0};
    result.skyData.envColor = toFloat3(options.envColor);
    result.skyData.transform = toMatrix4x4(Mat4::identity());
    result.skyData.indirectLightingIntensity = options.indirectMultiplier * options.indirectIntensity;
    result.skyData.shadowTranslucency = options.shadowTranslucency;
    result.skyData.flags = 0;

    result.envCube.assign(static_cast<std::size_t>(cubeSize) * cubeSize * 6u,
                          Float4{radiance.x * invPi, radiance.y * invPi, radiance.z * invPi, radiance.w * invPi});
    return result;
}

}  // namespace hbg::dawn
