#include "render/sky/probe_manager.h"

#include "core/log.h"
#include "render/sky/environment_processor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hbg::render {
namespace {

std::vector<float> resampleEnvironmentMip(const std::vector<float>& source, uint32_t sourceWidth,
                                          uint32_t sourceHeight, uint32_t targetWidth, uint32_t targetHeight) {
    std::vector<float> result(static_cast<std::size_t>(targetWidth) * targetHeight * 4u, 0.0f);
    if (sourceWidth == targetWidth && sourceHeight == targetHeight && source.size() == result.size()) {
        return source;
    }
    for (uint32_t y = 0; y < targetHeight; ++y) {
        const float sourceY = (static_cast<float>(y) + 0.5f) * static_cast<float>(sourceHeight) /
                                  static_cast<float>(targetHeight) -
                              0.5f;
        const uint32_t y0 = static_cast<uint32_t>(std::clamp(std::floor(sourceY), 0.0f,
                                                             static_cast<float>(sourceHeight - 1u)));
        const uint32_t y1 = std::min(y0 + 1u, sourceHeight - 1u);
        const float ty = std::clamp(sourceY - static_cast<float>(y0), 0.0f, 1.0f);
        for (uint32_t x = 0; x < targetWidth; ++x) {
            const float sourceX = (static_cast<float>(x) + 0.5f) * static_cast<float>(sourceWidth) /
                                      static_cast<float>(targetWidth) -
                                  0.5f;
            const uint32_t x0 = static_cast<uint32_t>(std::clamp(std::floor(sourceX), 0.0f,
                                                                 static_cast<float>(sourceWidth - 1u)));
            const uint32_t x1 = std::min(x0 + 1u, sourceWidth - 1u);
            const float tx = std::clamp(sourceX - static_cast<float>(x0), 0.0f, 1.0f);
            const auto sample = [&source, sourceWidth](uint32_t sx, uint32_t sy, uint32_t channel) {
                return source[(static_cast<std::size_t>(sy) * sourceWidth + sx) * 4u + channel];
            };
            const std::size_t targetOffset = (static_cast<std::size_t>(y) * targetWidth + x) * 4u;
            for (uint32_t channel = 0; channel < 4; ++channel) {
                const float low = sample(x0, y0, channel) * (1.0f - tx) + sample(x1, y0, channel) * tx;
                const float high = sample(x0, y1, channel) * (1.0f - tx) + sample(x1, y1, channel) * tx;
                result[targetOffset + channel] = low * (1.0f - ty) + high * ty;
            }
        }
    }
    return result;
}

bool uploadProbeArray(gfx::ResourceManager& resources, const std::vector<EnvironmentImage>& environments,
                      gfx::TextureHandle& texture, std::string* error) {
    if (environments.empty()) {
        texture = {};
        return true;
    }
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t mipCount = 1;
    for (const EnvironmentImage& environment : environments) {
        width = std::max(width, environment.width);
        height = std::max(height, environment.height);
        mipCount = std::max(mipCount, environment.mipCount);
    }

    gfx::TextureDesc desc;
    desc.width = static_cast<uint16_t>(width);
    desc.height = static_cast<uint16_t>(height);
    desc.layers = static_cast<uint16_t>(environments.size());
    desc.mipCount = static_cast<uint8_t>(mipCount);
    desc.format = gfx::TextureFormat::RGBA32F;
    desc.flags = BGFX_SAMPLER_V_CLAMP;
    desc.name = "probe.array";
    texture = resources.createTexture(desc);
    if (!texture.valid()) {
        if (error != nullptr) {
            *error = "failed to create probe texture array";
        }
        return false;
    }

    for (uint32_t layer = 0; layer < environments.size(); ++layer) {
        const EnvironmentImage& environment = environments[layer];
        for (uint32_t mip = 0; mip < environment.mipCount; ++mip) {
            const uint32_t sourceWidth = std::max(1u, environment.width >> mip);
            const uint32_t sourceHeight = std::max(1u, environment.height >> mip);
            const uint32_t targetWidth = std::max(1u, width >> mip);
            const uint32_t targetHeight = std::max(1u, height >> mip);
            const std::vector<float> resampled = resampleEnvironmentMip(
                environment.mips[mip], sourceWidth, sourceHeight, targetWidth, targetHeight);
            const uint32_t size = static_cast<uint32_t>(resampled.size() * sizeof(float));
            const uint16_t pitch = static_cast<uint16_t>(targetWidth * sizeof(float) * 4u);
            if (!resources.updateTexture(texture, resampled.data(), size, static_cast<uint16_t>(targetWidth),
                                         static_cast<uint16_t>(targetHeight), pitch, static_cast<uint16_t>(layer),
                                         static_cast<uint8_t>(mip))) {
                if (error != nullptr) {
                    *error = "failed to upload probe texture array mip " + std::to_string(mip);
                }
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool ProbeManager::load(gfx::ResourceManager& resources, const std::vector<ReflectionProbe>& probes,
                        const std::vector<asset::ImageAsset>& images, std::string* error) {
    shutdown(resources);
    if (probes.size() > kMaxReflectionProbes) {
        HBG_LOGW("probes: {} declared, only the first {} static probes are supported", probes.size(),
                 kMaxReflectionProbes);
    }

    std::vector<EnvironmentImage> environments;
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
        m_probes.push_back(std::move(gpuProbe));
        environments.push_back(std::move(environment));
    }

    if (!uploadProbeArray(resources, environments, m_textureArray, error)) {
        if (m_textureArray.valid()) {
            resources.destroyTexture(m_textureArray);
            m_textureArray = {};
        }
        m_probes.clear();
        return false;
    }

    if (m_probes.empty()) {
        HBG_LOGI("probes: no active static reflection probes");
    } else {
        HBG_LOGI("probes: {} static reflection probes loaded ({} layer array)", m_probes.size(),
                 m_probes.size());
    }
    select(Vec3(0.0f, 0.0f, 0.0f), false, false);
    return true;
}

void ProbeManager::shutdown(gfx::ResourceManager& resources) {
    if (m_textureArray.valid()) {
        resources.destroyTexture(m_textureArray);
        m_textureArray = {};
    }
    m_probes.clear();
    m_selected = {-1, -1};
    m_uniforms = {};
}

void ProbeManager::select(const Vec3& cameraPosition, bool enabled, bool skyFallback) {
    m_selected = {-1, -1};
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

    const auto fillProbe = [this](int index, Mat4& worldToProbe, Vec4& extents, Vec4& params) {
        if (index < 0 || static_cast<std::size_t>(index) >= m_probes.size()) {
            return false;
        }
        const GpuProbe& probe = m_probes[static_cast<std::size_t>(index)];
        worldToProbe = probeWorldToProbe(probe.descriptor);
        extents = Vec4(std::abs(probe.descriptor.boxExtents.x), std::abs(probe.descriptor.boxExtents.y),
                       std::abs(probe.descriptor.boxExtents.z), std::max(probe.descriptor.blendDistance, 0.0f));
        params = Vec4(1.0f, std::max(probe.descriptor.intensity, 0.0f), probe.maxMip, 0.0f);
        return true;
    };

    fillProbe(m_selected[0], m_uniforms.worldToProbe0, m_uniforms.extents0, m_uniforms.params0);
    fillProbe(m_selected[1], m_uniforms.worldToProbe1, m_uniforms.extents1, m_uniforms.params1);
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

} // namespace hbg::render
