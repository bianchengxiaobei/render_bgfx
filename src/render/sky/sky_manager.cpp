#include "render/sky/sky_manager.h"

#include "asset/texture_loader.h"
#include "core/log.h"
#include "render/sky/environment_processor.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace hbg::render {
namespace {

bool uploadEnvironment(gfx::ResourceManager& resources, const EnvironmentImage& environment, const char* name,
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

} // namespace

bool SkyManager::load(gfx::ResourceManager& resources, const std::filesystem::path& path, float intensity,
                      float rotationDegrees, std::string* error) {
    shutdown(resources);
    m_intensity = std::max(intensity, 0.0f);

    asset::ImageAsset source;
    if (!asset::TextureLoader::loadFromFile(path, source, error)) {
        return false;
    }

    EnvironmentImage environment;
    if (!EnvironmentProcessor::build(source, rotationDegrees, environment, error)) {
        return false;
    }
    EnvironmentImage skyEnvironment;
    if (!EnvironmentProcessor::buildSky(source, rotationDegrees, skyEnvironment, error)) {
        return false;
    }
    if (!uploadEnvironment(resources, environment, "sky.environmentIbl", m_texture, error) ||
        !uploadEnvironment(resources, skyEnvironment, "sky.environmentSky", m_skyTexture, error)) {
        shutdown(resources);
        return false;
    }

    m_maxMip = static_cast<float>(environment.mipCount - 1);
    m_uniforms.skyParams = Vec4(m_intensity, m_maxMip, 1.0f, 1.0f);
    m_uniforms.shAr = environment.sh[0];
    m_uniforms.shAg = environment.sh[1];
    m_uniforms.shAb = environment.sh[2];
    m_uniforms.shBr = environment.sh[3];
    m_uniforms.shBg = environment.sh[4];
    m_uniforms.shBb = environment.sh[5];
    m_uniforms.shCr = environment.sh[6];

    HBG_LOGI("sky: '{}' IBL {}x{} ({} mips), sky {}x{}, intensity={}, rotation={} deg", path.filename().string(),
             environment.width, environment.height, environment.mipCount, skyEnvironment.width, skyEnvironment.height,
             m_intensity, rotationDegrees);
    return true;
}

void SkyManager::shutdown(gfx::ResourceManager& resources) {
    if (m_texture.valid()) {
        resources.destroyTexture(m_texture);
        m_texture = {};
    }
    if (m_skyTexture.valid()) {
        resources.destroyTexture(m_skyTexture);
        m_skyTexture = {};
    }
    m_uniforms = {};
    m_intensity = 1.0f;
    m_maxMip = 0.0f;
}

} // namespace hbg::render
