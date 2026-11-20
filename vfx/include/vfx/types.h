#pragma once

#include <array>
#include <cstdint>

namespace vfx {

using Index = std::uint32_t;
using ParticleIndex = std::uint32_t;

inline constexpr Index kInvalidIndex = 0xffffffffu;
inline constexpr ParticleIndex kInvalidParticle = 0xffffffffu;
inline constexpr std::uint32_t kDefaultParticleCapacity = 1024u;
inline constexpr std::uint32_t kMaxParticleCapacity = 1u << 20;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

using Color = Vec4;

struct Mat4 {
    std::array<float, 16> m{};

    static constexpr Mat4 identity() {
        Mat4 result;
        result.m[0] = 1.0f;
        result.m[5] = 1.0f;
        result.m[10] = 1.0f;
        result.m[15] = 1.0f;
        return result;
    }

    float* data() { return m.data(); }
    const float* data() const { return m.data(); }
};

struct Bounds {
    Vec3 min;
    Vec3 max;
    bool valid = false;
};

enum class TextureFormat : std::uint8_t {
    RGBA8,
    BGRA8,
    RGBA16F,
    RGBA32F,
    RG16F,
    RG32F,
    R8,
    RG8,
    R32U,
    R32F,
    RG11B10F,
    D16,
    D24S8,
    D32F,
    BC1,
    BC3,
    BC5,
    BC7,
    Count,
};

enum class UniformType : std::uint8_t { Sampler, Vec4, Mat3, Mat4 };

enum class BlendMode : std::uint8_t { Alpha, Additive, Premultiply, Opaque };

enum class CompareFunc : std::uint8_t { Less, LessEqual, Equal, Greater, Always };

struct Limits {
    std::uint32_t maxTextureSamplers = 16;
    std::uint32_t maxComputeBindings = 16;
};

} // namespace vfx
