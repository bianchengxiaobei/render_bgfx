#include "render/sky/environment_processor.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hbg::render {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = kPi * 2.0;

void setError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

float saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

Vec4 readPixel(const asset::ImageAsset& image, uint32_t x, uint32_t y) {
    if (image.format == gfx::TextureFormat::RGBA32F) {
        const auto* pixels = reinterpret_cast<const float*>(image.pixels.data());
        const std::size_t offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
        return Vec4(pixels[offset + 0], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]);
    }
    const std::size_t offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return Vec4(
        static_cast<float>(image.pixels[offset + 0]) / 255.0f, static_cast<float>(image.pixels[offset + 1]) / 255.0f,
        static_cast<float>(image.pixels[offset + 2]) / 255.0f, static_cast<float>(image.pixels[offset + 3]) / 255.0f);
}

Vec3 rotateY(const Vec3& direction, float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return Vec3(cosine * direction.x + sine * direction.z, direction.y, -sine * direction.x + cosine * direction.z);
}

bool isCubemapStrip(const asset::ImageAsset& image) {
    return image.height > 0 && image.width == image.height * 6;
}

Vec3 sampleCubemapStrip(const asset::ImageAsset& image, const Vec3& direction) {
    const Vec3 d = normalize(direction);
    const float ax = std::abs(d.x);
    const float ay = std::abs(d.y);
    const float az = std::abs(d.z);
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
    const auto clampX = [faceWidth](int value) { return std::clamp(value, 0, static_cast<int>(faceWidth) - 1); };
    const auto clampY = [height = static_cast<int>(image.height)](int value) {
        return std::clamp(value, 0, height - 1);
    };
    const uint32_t faceOffset = face * static_cast<uint32_t>(faceWidth);
    const Vec4 c00 =
        readPixel(image, faceOffset + static_cast<uint32_t>(clampX(x0)), static_cast<uint32_t>(clampY(y0)));
    const Vec4 c10 =
        readPixel(image, faceOffset + static_cast<uint32_t>(clampX(x0 + 1)), static_cast<uint32_t>(clampY(y0)));
    const Vec4 c01 =
        readPixel(image, faceOffset + static_cast<uint32_t>(clampX(x0)), static_cast<uint32_t>(clampY(y0 + 1)));
    const Vec4 c11 =
        readPixel(image, faceOffset + static_cast<uint32_t>(clampX(x0 + 1)), static_cast<uint32_t>(clampY(y0 + 1)));
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    return Vec3(lerp(lerp(c00.x, c10.x, fx), lerp(c01.x, c11.x, fx), fy),
                lerp(lerp(c00.y, c10.y, fx), lerp(c01.y, c11.y, fx), fy),
                lerp(lerp(c00.z, c10.z, fx), lerp(c01.z, c11.z, fx), fy));
}

Vec3 sampleEquirect(const asset::ImageAsset& image, const Vec3& direction) {
    const Vec2 uv = EnvironmentProcessor::directionToEquirect(direction);
    const float pixelX = uv.x * static_cast<float>(image.width) - 0.5f;
    const float pixelY = uv.y * static_cast<float>(image.height) - 0.5f;
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

    const Vec4 c00 = readPixel(image, static_cast<uint32_t>(wrapX(x0)), static_cast<uint32_t>(clampY(y0)));
    const Vec4 c10 = readPixel(image, static_cast<uint32_t>(wrapX(x0 + 1)), static_cast<uint32_t>(clampY(y0)));
    const Vec4 c01 = readPixel(image, static_cast<uint32_t>(wrapX(x0)), static_cast<uint32_t>(clampY(y0 + 1)));
    const Vec4 c11 = readPixel(image, static_cast<uint32_t>(wrapX(x0 + 1)), static_cast<uint32_t>(clampY(y0 + 1)));

    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float r = lerp(lerp(c00.x, c10.x, fx), lerp(c01.x, c11.x, fx), fy);
    const float g = lerp(lerp(c00.y, c10.y, fx), lerp(c01.y, c11.y, fx), fy);
    const float b = lerp(lerp(c00.z, c10.z, fx), lerp(c01.z, c11.z, fx), fy);
    return Vec3(r, g, b);
}

Vec3 sampleSource(const asset::ImageAsset& image, const Vec3& direction, float rotationRadians) {
    const Vec3 rotated = rotateY(direction, -rotationRadians);
    return isCubemapStrip(image) ? sampleCubemapStrip(image, rotated) : sampleEquirect(image, rotated);
}

float radicalInverse(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

Vec3 importanceSampleGGX(float u1, float u2, float roughness, const Vec3& normal) {
    const float a = roughness * roughness;
    const float phi = static_cast<float>(kTwoPi) * u2;
    const float cosTheta = std::sqrt((1.0f - u1) / (1.0f + (a * a - 1.0f) * u1));
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));

    const Vec3 up = std::abs(normal.z) < 0.999f ? Vec3(0.0f, 0.0f, 1.0f) : Vec3(1.0f, 0.0f, 0.0f);
    const Vec3 tangent = normalize(cross(up, normal));
    const Vec3 bitangent = cross(normal, tangent);
    const Vec3 local =
        tangent * (sinTheta * std::cos(phi)) + bitangent * (sinTheta * std::sin(phi)) + normal * cosTheta;
    return normalize(local);
}

Vec3 prefilter(const asset::ImageAsset& image, const Vec3& normal, float roughness, float rotationRadians) {
    Vec3 color(0.0f, 0.0f, 0.0f);
    float weight = 0.0f;
    for (uint32_t sampleIndex = 0; sampleIndex < kEnvironmentPrefilterSamples; ++sampleIndex) {
        const float u1 = (static_cast<float>(sampleIndex) + 0.5f) / static_cast<float>(kEnvironmentPrefilterSamples);
        const float u2 = radicalInverse(sampleIndex);
        const Vec3 halfway = importanceSampleGGX(u1, u2, roughness, normal);
        const Vec3 light = normalize(halfway * (2.0f * dot(normal, halfway)) - normal);
        const float nDotL = std::max(dot(normal, light), 0.0f);
        if (nDotL > 0.0f) {
            color = color + sampleSource(image, light, rotationRadians) * nDotL;
            weight += nDotL;
        }
    }
    if (weight <= 1.0e-8f) {
        return sampleSource(image, normal, rotationRadians);
    }
    return color * (1.0f / weight);
}

float mipToPerceptualRoughness(uint32_t mip, uint32_t maxMip) {
    const float level = maxMip > 0 ? (static_cast<float>(mip) / static_cast<float>(maxMip)) * 6.0f : 0.0f;
    const float x = saturate(level / 6.0f);
    const float root = std::sqrt(std::max(0.0f, 2.89f / 1.96f - (2.8f / 1.96f) * x));
    return saturate(1.7f / 1.4f - root);
}

double shBasis(int index, const Vec3& direction) {
    const double x = direction.x;
    const double y = direction.y;
    const double z = direction.z;
    switch (index) {
    case 0:
        return 0.28209479177387814347;
    case 1:
        return 0.48860251190291992159 * y;
    case 2:
        return 0.48860251190291992159 * z;
    case 3:
        return 0.48860251190291992159 * x;
    case 4:
        return 1.09254843059207907054 * x * y;
    case 5:
        return 1.09254843059207907054 * y * z;
    case 6:
        return 0.31539156525252000603 * (3.0 * z * z - 1.0);
    case 7:
        return 1.09254843059207907054 * x * z;
    case 8:
        return 0.54627421529603953527 * (x * x - y * y);
    default:
        return 0.0;
    }
}

float dot4(const Vec4& lhs, const Vec4& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
}

void packSH(const std::array<std::array<double, 9>, 3>& coefficients, std::array<Vec4, 7>& packed) {
    for (int channel = 0; channel < 3; ++channel) {
        const auto& c = coefficients[static_cast<std::size_t>(channel)];
        packed[static_cast<std::size_t>(channel)] = Vec4(static_cast<float>(c[3]), static_cast<float>(c[1]),
                                                         static_cast<float>(c[2]), static_cast<float>(c[0] - c[6]));
        packed[static_cast<std::size_t>(3 + channel)] = Vec4(static_cast<float>(c[4]), static_cast<float>(c[5]),
                                                             static_cast<float>(c[6] * 3.0), static_cast<float>(c[7]));
    }
    packed[6] = Vec4(static_cast<float>(coefficients[0][8]), static_cast<float>(coefficients[1][8]),
                     static_cast<float>(coefficients[2][8]), 1.0f);
}

} // namespace

bool EnvironmentProcessor::build(const asset::ImageAsset& source, float rotationDegrees, EnvironmentImage& environment,
                                 std::string* error) {
    environment = {};
    if (!source.valid()) {
        setError(error, "environment image is empty");
        return false;
    }
    if (source.format != gfx::TextureFormat::RGBA32F && source.format != gfx::TextureFormat::RGBA8) {
        setError(error, "environment image must be RGBA32F or RGBA8");
        return false;
    }

    uint32_t width = std::min(source.width, kEnvironmentMaxWidth);
    uint32_t height =
        static_cast<uint32_t>(std::max(1.0, std::round(static_cast<double>(source.height) * width / source.width)));
    if (isCubemapStrip(source)) {
        width = std::min(source.height * 4, kEnvironmentMaxWidth);
        height = std::max(1u, width / 2);
    }
    if (height > kEnvironmentMaxWidth) {
        height = kEnvironmentMaxWidth;
    }

    uint32_t mipCount = 1;
    while (mipCount < kEnvironmentMaxMipCount && (width >> mipCount) > 0 && (height >> mipCount) > 0) {
        ++mipCount;
    }

    environment.width = width;
    environment.height = height;
    environment.mipCount = mipCount;
    environment.mips.resize(mipCount);
    environment.mips[0].resize(static_cast<std::size_t>(width) * height * 4);

    const float rotationRadians = rotationDegrees * static_cast<float>(kPi / 180.0);
    float* basePixels = environment.mips[0].data();
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const Vec2 uv((static_cast<float>(x) + 0.5f) / static_cast<float>(width),
                          (static_cast<float>(y) + 0.5f) / static_cast<float>(height));
            const Vec3 direction = equirectToDirection(uv);
            const Vec3 color = sampleSource(source, direction, rotationRadians);
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4;
            basePixels[offset + 0] = color.x;
            basePixels[offset + 1] = color.y;
            basePixels[offset + 2] = color.z;
            basePixels[offset + 3] = 1.0f;
        }
    }

    for (uint32_t mip = 1; mip < mipCount; ++mip) {
        const uint32_t mipWidth = std::max(1u, width >> mip);
        const uint32_t mipHeight = std::max(1u, height >> mip);
        const float perceptualRoughness = mipToPerceptualRoughness(mip, mipCount - 1);
        const float roughness = perceptualRoughness * perceptualRoughness;
        std::vector<float>& mipData = environment.mips[mip];
        mipData.resize(static_cast<std::size_t>(mipWidth) * mipHeight * 4);
        for (uint32_t y = 0; y < mipHeight; ++y) {
            for (uint32_t x = 0; x < mipWidth; ++x) {
                const Vec2 uv((static_cast<float>(x) + 0.5f) / static_cast<float>(mipWidth),
                              (static_cast<float>(y) + 0.5f) / static_cast<float>(mipHeight));
                const Vec3 direction = equirectToDirection(uv);
                const Vec3 color = prefilter(source, direction, roughness, rotationRadians);
                const std::size_t offset = (static_cast<std::size_t>(y) * mipWidth + x) * 4;
                mipData[offset + 0] = color.x;
                mipData[offset + 1] = color.y;
                mipData[offset + 2] = color.z;
                mipData[offset + 3] = 1.0f;
            }
        }
    }

    std::array<std::array<double, 9>, 3> coefficients{};
    const std::vector<float>& baseMip = environment.mips[0];
    for (uint32_t y = 0; y < environment.height; ++y) {
        const double theta = (static_cast<double>(y) + 0.5) / environment.height * kPi;
        const double sinTheta = std::sin(theta);
        const double dOmega = (kTwoPi / environment.width) * (kPi / environment.height) * sinTheta;
        for (uint32_t x = 0; x < environment.width; ++x) {
            const Vec2 uv((static_cast<float>(x) + 0.5f) / static_cast<float>(environment.width),
                          (static_cast<float>(y) + 0.5f) / static_cast<float>(environment.height));
            const Vec3 direction = equirectToDirection(uv);
            const std::size_t offset = (static_cast<std::size_t>(y) * environment.width + x) * 4;
            const Vec3 color(baseMip[offset + 0], baseMip[offset + 1], baseMip[offset + 2]);
            for (int index = 0; index < 9; ++index) {
                const double basis = shBasis(index, direction) * dOmega;
                coefficients[0][index] += static_cast<double>(color.x) * basis;
                coefficients[1][index] += static_cast<double>(color.y) * basis;
                coefficients[2][index] += static_cast<double>(color.z) * basis;
            }
        }
    }

    constexpr std::array<double, 9> kBasisScale = {
        0.28209479177387814347, 0.48860251190291992159, 0.48860251190291992159,
        0.48860251190291992159, 1.09254843059207907054, 1.09254843059207907054,
        0.31539156525252000603, 1.09254843059207907054, 0.54627421529603953527,
    };
    for (std::array<double, 9>& channel : coefficients) {
        const double invPi = 1.0 / kPi;
        for (int index = 0; index < 9; ++index) {
            const double band = index == 0 ? 1.0 : (index < 4 ? 2.0 / 3.0 : 1.0 / 4.0);
            channel[static_cast<std::size_t>(index)] *= band * invPi * kBasisScale[static_cast<std::size_t>(index)];
        }
    }
    packSH(coefficients, environment.sh);
    return true;
}

bool EnvironmentProcessor::buildSky(const asset::ImageAsset& source, float rotationDegrees,
                                    EnvironmentImage& environment, std::string* error) {
    environment = {};
    if (!source.valid()) {
        setError(error, "environment image is empty");
        return false;
    }
    if (source.format != gfx::TextureFormat::RGBA32F && source.format != gfx::TextureFormat::RGBA8) {
        setError(error, "environment image must be RGBA32F or RGBA8");
        return false;
    }

    uint32_t width = std::min(source.width, kEnvironmentSkyMaxWidth);
    uint32_t height =
        static_cast<uint32_t>(std::max(1.0, std::round(static_cast<double>(source.height) * width / source.width)));
    if (isCubemapStrip(source)) {
        width = std::min(source.height * 4, kEnvironmentSkyMaxWidth);
        height = std::max(1u, width / 2);
    }
    if (height > kEnvironmentSkyMaxWidth) {
        height = kEnvironmentSkyMaxWidth;
    }

    environment.width = width;
    environment.height = height;
    environment.mipCount = 1;
    environment.mips.resize(1);
    environment.mips[0].resize(static_cast<std::size_t>(width) * height * 4);

    const float rotationRadians = rotationDegrees * static_cast<float>(kPi / 180.0);
    float* pixels = environment.mips[0].data();
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const Vec2 uv((static_cast<float>(x) + 0.5f) / static_cast<float>(width),
                          (static_cast<float>(y) + 0.5f) / static_cast<float>(height));
            const Vec3 direction = equirectToDirection(uv);
            const Vec3 color = sampleSource(source, direction, rotationRadians);
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4;
            pixels[offset + 0] = color.x;
            pixels[offset + 1] = color.y;
            pixels[offset + 2] = color.z;
            pixels[offset + 3] = 1.0f;
        }
    }
    return true;
}

Vec3 EnvironmentProcessor::evaluateSH(const std::array<Vec4, 7>& coefficients, const Vec3& normal) {
    const Vec3 n = normalize(normal);
    const Vec4 linear(n.x, n.y, n.z, 1.0f);
    const Vec3 constant(dot4(coefficients[0], linear), dot4(coefficients[1], linear), dot4(coefficients[2], linear));
    const Vec4 quadratic(n.x * n.y, n.y * n.z, n.z * n.z, n.z * n.x);
    const Vec3 higher(dot4(coefficients[3], quadratic), dot4(coefficients[4], quadratic),
                      dot4(coefficients[5], quadratic));
    const float last = n.x * n.x - n.y * n.y;
    return constant + higher + Vec3(coefficients[6].x, coefficients[6].y, coefficients[6].z) * last;
}

Vec2 EnvironmentProcessor::directionToEquirect(const Vec3& direction) {
    const Vec3 d = normalize(direction);
    const float u = std::atan2(d.x, d.z) * static_cast<float>(0.5 / kPi) + 0.5f;
    const float v = std::acos(std::clamp(d.y, -1.0f, 1.0f)) * static_cast<float>(1.0 / kPi);
    return Vec2(u, v);
}

Vec3 EnvironmentProcessor::equirectToDirection(const Vec2& uv) {
    const float phi = (uv.x - 0.5f) * static_cast<float>(kTwoPi);
    const float theta = uv.y * static_cast<float>(kPi);
    const float sinTheta = std::sin(theta);
    return Vec3(sinTheta * std::sin(phi), std::cos(theta), sinTheta * std::cos(phi));
}

} // namespace hbg::render
