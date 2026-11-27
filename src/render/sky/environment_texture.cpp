#include "render/sky/environment_texture.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace hbg::render {

bool uploadEnvironmentTexture(gfx::ResourceManager& resources, const EnvironmentImage& environment, const char* name,
                              gfx::TextureHandle& texture, std::string* error) {
    gfx::TextureDesc desc;
    desc.width = static_cast<uint16_t>(environment.width);
    desc.height = static_cast<uint16_t>(environment.height);
    desc.mipCount = static_cast<uint8_t>(environment.mipCount);
    desc.format = gfx::TextureFormat::RGBA32F;
    desc.flags = BGFX_SAMPLER_V_CLAMP;
    desc.name = name;
    texture = resources.createTexture(desc);
    if (!texture.valid()) {
        if (error != nullptr) {
            *error = "failed to create environment texture";
        }
        return false;
    }

    for (uint32_t mip = 0; mip < environment.mipCount; ++mip) {
        const uint32_t mipWidth = std::max(1u, environment.width >> mip);
        const uint32_t mipHeight = std::max(1u, environment.height >> mip);
        const uint16_t pitch = static_cast<uint16_t>(mipWidth * sizeof(float) * 4);
        const uint32_t size = static_cast<uint32_t>(environment.mips[mip].size() * sizeof(float));
        if (!resources.updateTexture(texture, environment.mips[mip].data(), size, static_cast<uint16_t>(mipWidth),
                                     static_cast<uint16_t>(mipHeight), pitch, 0, static_cast<uint8_t>(mip))) {
            if (error != nullptr) {
                *error = "failed to upload environment mip " + std::to_string(mip);
            }
            return false;
        }
    }
    return true;
}

} // namespace hbg::render
