#include "render/sky/probe_manager.h"

#include "core/log.h"
#include "render/sky/environment_processor.h"
#include "render/sky/environment_texture.h"

#include <algorithm>
#include <string>
#include <utility>

namespace hbg::render {

bool ProbeManager::load(gfx::ResourceManager& resources, const std::vector<ReflectionProbe>& probes,
                        const std::vector<asset::ImageAsset>& images, std::string* error) {
    shutdown(resources);
    if (probes.size() > kMaxReflectionProbes) {
        HBG_LOGW("probes: {} declared, only the first {} static probes are supported", probes.size(),
                 kMaxReflectionProbes);
    }

    for (const ReflectionProbe& probe : probes) {
        if (m_probes.size() >= kMaxReflectionProbes) {
            break;
        }
        if (!probe.enabled) {
            continue;
        }
        if (probe.image < 0 || static_cast<std::size_t>(probe.image) >= images.size()) {
            HBG_LOGW("probes: '{}' has an invalid image index", probe.name);
            continue;
        }

        EnvironmentImage environment;
        std::string buildError;
        if (!EnvironmentProcessor::build(images[static_cast<std::size_t>(probe.image)], 0.0f, environment,
                                         &buildError)) {
            HBG_LOGW("probes: '{}' environment build failed: {}", probe.name, buildError);
            continue;
        }

        GpuProbe gpuProbe;
        gpuProbe.descriptor = probe;
        gpuProbe.maxMip = static_cast<float>(std::max(environment.mipCount, 1u) - 1u);
        const std::string textureName = "probe." + std::to_string(m_probes.size()) + "." + probe.name;
        if (!uploadEnvironmentTexture(resources, environment, textureName.c_str(), gpuProbe.texture, error)) {
            shutdown(resources);
            return false;
        }
        m_probes.push_back(std::move(gpuProbe));
    }

    if (m_probes.empty()) {
        HBG_LOGI("probes: no active static reflection probes");
    } else {
        HBG_LOGI("probes: {} static reflection probes loaded", m_probes.size());
    }
    select(Vec3(0.0f, 0.0f, 0.0f), false, false);
    return true;
}

void ProbeManager::shutdown(gfx::ResourceManager& resources) {
    for (GpuProbe& probe : m_probes) {
        if (probe.texture.valid()) {
            resources.destroyTexture(probe.texture);
            probe.texture = {};
        }
    }
    m_probes.clear();
    m_selected = {-1, -1};
    m_textures = {};
    m_uniforms = {};
}

void ProbeManager::select(const Vec3& cameraPosition, bool enabled, bool skyFallback) {
    m_selected = {-1, -1};
    m_textures = {};
    m_uniforms = {};
    m_uniforms.worldToProbe0 = Mat4::identity();
    m_uniforms.worldToProbe1 = Mat4::identity();

    if (enabled && !m_probes.empty()) {
        std::vector<ReflectionProbe> descriptors;
        descriptors.reserve(m_probes.size());
        for (const GpuProbe& probe : m_probes) {
            descriptors.push_back(probe.descriptor);
        }
        m_selected = selectReflectionProbes(descriptors, cameraPosition);
    }

    const auto fillProbe = [this](int index, Mat4& worldToProbe, Vec4& extents, Vec4& params,
                                  gfx::TextureHandle& texture) {
        if (index < 0 || static_cast<std::size_t>(index) >= m_probes.size()) {
            return false;
        }
        const GpuProbe& probe = m_probes[static_cast<std::size_t>(index)];
        worldToProbe = probeWorldToProbe(probe.descriptor);
        extents = Vec4(std::abs(probe.descriptor.boxExtents.x), std::abs(probe.descriptor.boxExtents.y),
                       std::abs(probe.descriptor.boxExtents.z), std::max(probe.descriptor.blendDistance, 0.0f));
        params = Vec4(1.0f, std::max(probe.descriptor.intensity, 0.0f), probe.maxMip, 0.0f);
        texture = probe.texture;
        return true;
    };

    fillProbe(m_selected[0], m_uniforms.worldToProbe0, m_uniforms.extents0, m_uniforms.params0, m_textures[0]);
    fillProbe(m_selected[1], m_uniforms.worldToProbe1, m_uniforms.extents1, m_uniforms.params1, m_textures[1]);
    m_uniforms.global =
        Vec4(static_cast<float>(selectedCount()), static_cast<float>(m_probes.size()), skyFallback ? 1.0f : 0.0f,
             0.0f);
}

std::size_t ProbeManager::selectedCount() const {
    std::size_t count = 0;
    for (int index : m_selected) {
        if (index >= 0) {
            ++count;
        }
    }
    return count;
}

gfx::TextureHandle ProbeManager::texture(std::size_t index) const {
    return index < m_textures.size() ? m_textures[index] : gfx::TextureHandle{};
}

} // namespace hbg::render
