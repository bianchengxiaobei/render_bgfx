#include "render/sky/sky_manager.h"

#include "asset/texture_loader.h"
#include "core/log.h"
#include "render/sky/environment_processor.h"
#include "render/sky/environment_texture.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace hbg::render {

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
    if (!uploadEnvironmentTexture(resources, environment, "sky.environmentIbl", m_texture, error) ||
        !uploadEnvironmentTexture(resources, skyEnvironment, "sky.environmentSky", m_skyTexture, error)) {
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
