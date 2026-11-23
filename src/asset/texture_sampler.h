#pragma once

#include <cstdint>

namespace hbg::asset {

enum class TextureWrap : uint8_t {
    Repeat = 0,
    Clamp = 1,
    Mirror = 2,
};

enum class TextureFilter : uint8_t {
    Linear = 0,
    Nearest = 1,
};

enum class TextureMipFilter : uint8_t {
    Linear = 0,
    Nearest = 1,
};

struct TextureSampler {
    TextureWrap wrapS = TextureWrap::Clamp;
    TextureWrap wrapT = TextureWrap::Clamp;
    TextureFilter minFilter = TextureFilter::Linear;
    TextureFilter magFilter = TextureFilter::Linear;
    TextureMipFilter mipFilter = TextureMipFilter::Linear;

    bool operator==(const TextureSampler&) const = default;
};

inline uint32_t samplerStateKey(const TextureSampler& sampler) {
    return static_cast<uint32_t>(sampler.wrapS) |
           (static_cast<uint32_t>(sampler.wrapT) << 2) |
           (static_cast<uint32_t>(sampler.minFilter) << 4) |
           (static_cast<uint32_t>(sampler.magFilter) << 5) |
           (static_cast<uint32_t>(sampler.mipFilter) << 6);
}

} // namespace hbg::asset
