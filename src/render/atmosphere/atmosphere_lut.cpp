#include "render/atmosphere/atmosphere.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <thread>
#include <utility>

namespace hbg::render {
namespace {

constexpr uint32_t kTransmittanceSamples = 500;
constexpr uint32_t kInscatterSamples = 50;
constexpr float kLutTopPadding = 1000.0f;
constexpr float kPi = 3.14159265358979323846f;

constexpr uint32_t kMultipleScatteringRadiusSlices = 16;
constexpr uint32_t kMultipleScatteringMuSlices = 32;
constexpr uint32_t kMultipleScatteringMuSunSlices = 16;
constexpr uint32_t kMultipleScatteringNuSlices = 4;
constexpr uint32_t kMultipleScatteringSkyWidth = 16;
constexpr uint32_t kMultipleScatteringSkyHeight = 8;
constexpr uint32_t kMultipleScatteringThetaSamples = 8;
constexpr uint32_t kMultipleScatteringPhiSamples = 16;
constexpr uint32_t kMultipleScatteringIrradianceThetaSamples = 16;
constexpr uint32_t kMultipleScatteringIrradiancePhiSamples = 16;
constexpr uint32_t kMultipleScatteringRaySamples = 32;

struct LayerParameters {
    float radius = 0.0f;
    float dmin = 0.0f;
    float dmax = 0.0f;
    float dminp = 0.0f;
    float dmaxp = 0.0f;
};

Vec3 multiply(const Vec3& lhs, const Vec3& rhs) {
    return Vec3(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z);
}


float limitForLut(const AtmosphereSettings& settings, float radius, float mu) {
    const float groundRadius = settings.planetRadius;
    const float topRadius = settings.atmosphereRadius + kLutTopPadding;
    const float mu2 = mu * mu;
    float result = -radius * mu +
                   std::sqrt(std::max(radius * radius * (mu2 - 1.0f) + topRadius * topRadius, 0.0f));
    const float delta = radius * radius * (mu2 - 1.0f) + groundRadius * groundRadius;
    if (delta >= 0.0f) {
        const float inner = -radius * mu - std::sqrt(delta);
        if (inner >= 0.0f) {
            result = std::min(result, inner);
        }
    }
    return result;
}

float opticalDepth(const AtmosphereSettings& settings, float scaleHeight, float radius, float mu) {
    const float groundRadius = settings.planetRadius;
    const float horizon = std::sqrt(std::max(1.0f - (groundRadius * groundRadius) / (radius * radius), 0.0f));
    if (mu < -horizon) {
        return 1.0e9f;
    }
    const float distance = limitForLut(settings, radius, mu);
    if (distance <= 0.0f) {
        return 0.0f;
    }
    const float step = distance / static_cast<float>(kTransmittanceSamples);
    float result = 0.0f;
    float previous = std::exp(-(radius - groundRadius) / scaleHeight);
    for (uint32_t index = 1; index <= kTransmittanceSamples; ++index) {
        const float x = static_cast<float>(index) * step;
        const float current =
            std::exp(-(std::sqrt(std::max(radius * radius + x * x + 2.0f * radius * mu * x,
                                          groundRadius * groundRadius)) -
                       groundRadius) /
                     scaleHeight);
        result += (previous + current) * 0.5f * step;
        previous = current;
    }
    return result;
}

AtmosphereLutDimensions sanitizeDimensions(const AtmosphereLutDimensions& requested) {
    AtmosphereLutDimensions dimensions = requested;
    dimensions.transmittanceWidth = std::max(dimensions.transmittanceWidth, 2u);
    dimensions.transmittanceHeight = std::max(dimensions.transmittanceHeight, 2u);
    dimensions.skyWidth = std::max(dimensions.skyWidth, 2u);
    dimensions.skyHeight = std::max(dimensions.skyHeight, 2u);
    dimensions.radiusSlices = std::max(dimensions.radiusSlices, 2u);
    dimensions.muSlices = std::max(dimensions.muSlices, 4u);
    dimensions.muSlices += dimensions.muSlices & 1u;
    dimensions.muSunSlices = std::max(dimensions.muSunSlices, 2u);
    dimensions.nuSlices = std::max(dimensions.nuSlices, 2u);
    return dimensions;
}

LayerParameters layerParameters(const AtmosphereSettings& settings, const AtmosphereLutDimensions& dimensions,
                                uint32_t layer) {
    LayerParameters result;
    const float groundRadius = settings.planetRadius;
    const float topRadius = settings.atmosphereRadius;
    float normalized = static_cast<float>(layer) / static_cast<float>(dimensions.radiusSlices - 1u);
    normalized *= normalized;
    result.radius = std::sqrt(groundRadius * groundRadius +
                              normalized * (topRadius * topRadius - groundRadius * groundRadius));
    if (layer == 0) {
        result.radius += 10.0f;
    } else if (layer + 1 == dimensions.radiusSlices) {
        result.radius -= 1.0f;
    }
    const float tangent = std::sqrt(std::max(result.radius * result.radius - groundRadius * groundRadius, 0.0f));
    const float topTangent = std::sqrt(std::max(topRadius * topRadius - groundRadius * groundRadius, 0.0f));
    result.dmin = topRadius - result.radius;
    result.dmax = tangent + topTangent;
    result.dminp = result.radius - groundRadius;
    result.dmaxp = tangent;
    return result;
}

void getMuMuSNu(const AtmosphereSettings& settings, const AtmosphereLutDimensions& dimensions,
                const LayerParameters& layer, uint32_t x, uint32_t y, float& mu, float& muSun, float& nu) {
    const float groundRadius = settings.planetRadius;
    const float topRadius = settings.atmosphereRadius;
    const float halfMu = static_cast<float>(dimensions.muSlices) * 0.5f;
    if (static_cast<float>(y) < halfMu) {
        float d = 1.0f - static_cast<float>(y) / (halfMu - 1.0f);
        d = std::min(std::max(layer.dminp, d * layer.dmaxp), layer.dmaxp * 0.999f);
        mu = (groundRadius * groundRadius - layer.radius * layer.radius - d * d) /
             (2.0f * layer.radius * d);
        const float horizon =
            std::sqrt(std::max(1.0f - (groundRadius * groundRadius) / (layer.radius * layer.radius), 0.0f));
        mu = std::min(mu, -horizon - 0.001f);
    } else {
        float d = (static_cast<float>(y) - halfMu) / (halfMu - 1.0f);
        d = std::min(std::max(layer.dmin, d * layer.dmax), layer.dmax * 0.999f);
        mu = (topRadius * topRadius - layer.radius * layer.radius - d * d) / (2.0f * layer.radius * d);
    }
    const uint32_t xIndex = x % dimensions.muSunSlices;
    float normalizedMuSun = static_cast<float>(xIndex) / static_cast<float>(dimensions.muSunSlices - 1u);
    muSun = std::tan((2.0f * normalizedMuSun - 1.0f + 0.26f) * 1.1f) / std::tan(1.26f * 1.1f);
    nu = -1.0f + static_cast<float>(x / dimensions.muSunSlices) /
                      static_cast<float>(dimensions.nuSlices - 1u) * 2.0f;
}

Vec4 sample2D(const std::vector<float>& data, uint32_t width, uint32_t height, float u, float v) {
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    const float px = std::clamp(u * static_cast<float>(width) - 0.5f, 0.0f,
                                static_cast<float>(width - 1u));
    const float py = std::clamp(v * static_cast<float>(height) - 0.5f, 0.0f,
                                static_cast<float>(height - 1u));
    const uint32_t x0 = static_cast<uint32_t>(std::floor(px));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(py));
    const uint32_t x1 = std::min(x0 + 1u, width - 1u);
    const uint32_t y1 = std::min(y0 + 1u, height - 1u);
    const float fx = px - static_cast<float>(x0);
    const float fy = py - static_cast<float>(y0);
    const auto fetch = [&](uint32_t x, uint32_t y, uint32_t component) {
        return data[(static_cast<std::size_t>(y) * width + x) * 4u + component];
    };
    Vec4 result;
    for (uint32_t component = 0; component < 4u; ++component) {
        const float bottom = fetch(x0, y0, component) * (1.0f - fx) + fetch(x1, y0, component) * fx;
        const float top = fetch(x0, y1, component) * (1.0f - fx) + fetch(x1, y1, component) * fx;
        const float value = bottom * (1.0f - fy) + top * fy;
        if (component == 0) {
            result.x = value;
        } else if (component == 1) {
            result.y = value;
        } else if (component == 2) {
            result.z = value;
        } else {
            result.w = value;
        }
    }
    return result;
}

Vec4 sample3D(const std::vector<float>& data, const AtmosphereLutDimensions& dimensions, float u, float v,
              float w) {
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    w = std::clamp(w, 0.0f, 1.0f);
    const float px = std::clamp(u * static_cast<float>(dimensions.muSunSlices * dimensions.nuSlices) - 0.5f, 0.0f,
                                static_cast<float>(dimensions.muSunSlices * dimensions.nuSlices - 1u));
    const float py = std::clamp(v * static_cast<float>(dimensions.muSlices) - 0.5f, 0.0f,
                                static_cast<float>(dimensions.muSlices - 1u));
    const float pz = std::clamp(w * static_cast<float>(dimensions.radiusSlices) - 0.5f, 0.0f,
                                static_cast<float>(dimensions.radiusSlices - 1u));
    const uint32_t x0 = static_cast<uint32_t>(std::floor(px));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(py));
    const uint32_t z0 = static_cast<uint32_t>(std::floor(pz));
    const uint32_t x1 = std::min(x0 + 1u, dimensions.muSunSlices * dimensions.nuSlices - 1u);
    const uint32_t y1 = std::min(y0 + 1u, dimensions.muSlices - 1u);
    const uint32_t z1 = std::min(z0 + 1u, dimensions.radiusSlices - 1u);
    const float fx = px - static_cast<float>(x0);
    const float fy = py - static_cast<float>(y0);
    const float fz = pz - static_cast<float>(z0);
    const auto fetch = [&](uint32_t x, uint32_t y, uint32_t z, uint32_t component) {
        const std::size_t index =
            (static_cast<std::size_t>(z) * dimensions.muSlices + y) * dimensions.muSunSlices *
                dimensions.nuSlices +
            x;
        return data[index * 4u + component];
    };
    Vec4 result;
    for (uint32_t component = 0; component < 4u; ++component) {
        const auto sample = [&](uint32_t z) {
            const auto a = fetch(x0, y0, z, component) * (1.0f - fx) + fetch(x1, y0, z, component) * fx;
            const auto b = fetch(x0, y1, z, component) * (1.0f - fx) + fetch(x1, y1, z, component) * fx;
            return a * (1.0f - fy) + b * fy;
        };
        const auto value = sample(z0) * (1.0f - fz) + sample(z1) * fz;
        if (component == 0) {
            result.x = value;
        } else if (component == 1) {
            result.y = value;
        } else if (component == 2) {
            result.z = value;
        } else {
            result.w = value;
        }
    }
    return result;
}

Vec3 sampleTransmittance(const AtmosphereLut& lut, const AtmosphereSettings& settings, float radius, float mu) {
    if (lut.transmittance.empty() || !lut.dimensions.transmittanceWidth) {
        return Vec3(1.0f, 1.0f, 1.0f);
    }
    const float uR = std::sqrt(std::clamp((radius - settings.planetRadius) /
                                               (settings.atmosphereRadius - settings.planetRadius),
                                           0.0f, 1.0f));
    const float uMu = std::atan((std::clamp(mu, -1.0f, 1.0f) + 0.15f) / (1.0f + 0.15f) * std::tan(1.5f)) / 1.5f;
    const Vec4 value = sample2D(lut.transmittance, lut.dimensions.transmittanceWidth,
                                lut.dimensions.transmittanceHeight, uMu, uR);
    return Vec3(value.x, value.y, value.z);
}

Vec3 transmittanceBetween(const AtmosphereLut& lut, const AtmosphereSettings& settings, float radius, float mu,
                          float distance) {
    const float radius1 = std::sqrt(std::max(radius * radius + distance * distance +
                                                 2.0f * radius * mu * distance,
                                             0.0f));
    const float mu1 = (radius * mu + distance) / std::max(radius1, 1.0e-6f);
    if (mu > 0.0f) {
        const Vec3 transmittance = sampleTransmittance(lut, settings, radius, mu);
        const Vec3 transmittance1 = sampleTransmittance(lut, settings, radius1, mu1);
        return Vec3(std::min(transmittance.x / std::max(transmittance1.x, 1.0e-6f), 1.0f),
                    std::min(transmittance.y / std::max(transmittance1.y, 1.0e-6f), 1.0f),
                    std::min(transmittance.z / std::max(transmittance1.z, 1.0e-6f), 1.0f));
    }
    const Vec3 transmittance = sampleTransmittance(lut, settings, radius, -mu);
    const Vec3 transmittance1 = sampleTransmittance(lut, settings, radius1, -mu1);
    return Vec3(std::min(transmittance1.x / std::max(transmittance.x, 1.0e-6f), 1.0f),
                std::min(transmittance1.y / std::max(transmittance.y, 1.0e-6f), 1.0f),
                std::min(transmittance1.z / std::max(transmittance.z, 1.0e-6f), 1.0f));
}

float phaseRayleigh(float nu) {
    return 3.0f / (16.0f * kPi) * (1.0f + nu * nu);
}

float phaseMie(const AtmosphereSettings& settings, float nu) {
    const float g = settings.miePhaseG;
    const float denominator = std::max(1.0f + g * g - 2.0f * g * nu, 1.0e-6f);
    return 1.5f * (1.0f - g * g) / (4.0f * kPi) * std::pow(denominator, -1.5f) *
           (1.0f + nu * nu) / (2.0f + g * g);
}

Vec3 mieFromLut(const Vec4& rayMie, const Vec3& betaRayleigh) {
    const float scale = rayMie.w / std::max(rayMie.x, 1.0e-4f);
    return Vec3(rayMie.x * scale, rayMie.y * scale * betaRayleigh.x / std::max(betaRayleigh.y, 1.0e-6f),
                rayMie.z * scale * betaRayleigh.x / std::max(betaRayleigh.z, 1.0e-6f));
}

AtmosphereLutDimensions multipleScatteringDimensions() {
    AtmosphereLutDimensions dimensions;
    dimensions.transmittanceWidth = 0;
    dimensions.transmittanceHeight = 0;
    dimensions.skyWidth = kMultipleScatteringSkyWidth;
    dimensions.skyHeight = kMultipleScatteringSkyHeight;
    dimensions.radiusSlices = kMultipleScatteringRadiusSlices;
    dimensions.muSlices = kMultipleScatteringMuSlices;
    dimensions.muSunSlices = kMultipleScatteringMuSunSlices;
    dimensions.nuSlices = kMultipleScatteringNuSlices;
    return dimensions;
}

Vec3 sampleIrradianceTexture(const std::vector<float>& data, uint32_t width, uint32_t height, float groundRadius,
                             float topRadius, float radius, float muSun) {
    if (width == 0 || height == 0 || data.empty()) {
        return Vec3(0.0f, 0.0f, 0.0f);
    }
    const float uR = std::clamp((radius - groundRadius) / std::max(topRadius - groundRadius, 1.0e-6f), 0.0f, 1.0f);
    const float uMuSun = std::clamp((muSun + 0.2f) / 1.2f, 0.0f, 1.0f);
    const Vec4 value = sample2D(data, width, height, uMuSun, uR);
    return Vec3(value.x, value.y, value.z);
}

Vec4 sampleInscatterTexture(const std::vector<float>& data, const AtmosphereLutDimensions& dimensions,
                            float groundRadius, float topRadius, float radius, float mu, float muSun, float nu) {
    if (data.empty() || dimensions.radiusSlices < 2 || dimensions.muSlices < 2 ||
        dimensions.muSunSlices < 2 || dimensions.nuSlices < 2) {
        return Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    const float height = std::sqrt(std::max(topRadius * topRadius - groundRadius * groundRadius, 0.0f));
    const float rho = std::sqrt(std::max(radius * radius - groundRadius * groundRadius, 0.0f));
    const float uR = 0.5f / static_cast<float>(dimensions.radiusSlices) +
                     rho / std::max(height, 1.0e-6f) *
                         (1.0f - 1.0f / static_cast<float>(dimensions.radiusSlices));
    const float rmu = radius * mu;
    const float delta = rmu * rmu - radius * radius + groundRadius * groundRadius;
    const bool belowHorizon = rmu < 0.0f && delta > 0.0f;
    const float cstW = belowHorizon ? 0.5f - 0.5f / static_cast<float>(dimensions.muSlices)
                                    : 0.5f + 0.5f / static_cast<float>(dimensions.muSlices);
    const float cstX = belowHorizon ? 1.0f : -1.0f;
    const float cstY = belowHorizon ? 0.0f : height * height;
    const float cstZ = belowHorizon ? 0.0f : height;
    const float uMu = cstW + (rmu * cstX + std::sqrt(std::max(delta + cstY, 0.0f))) /
                                 std::max(rho + cstZ, 1.0e-6f) *
                                 (0.5f - 1.0f / static_cast<float>(dimensions.muSlices));
    const float uMuSun = 0.5f / static_cast<float>(dimensions.muSunSlices) +
                         (std::atan(std::max(muSun, -0.1975f) * std::tan(1.26f * 1.1f)) / 1.1f +
                          (1.0f - 0.26f)) *
                             0.5f * (1.0f - 1.0f / static_cast<float>(dimensions.muSunSlices));
    const float lerpNu = (std::clamp(nu, -1.0f, 1.0f) + 1.0f) * 0.5f *
                         static_cast<float>(dimensions.nuSlices - 1u);
    uint32_t nuIndex = static_cast<uint32_t>(std::floor(lerpNu));
    nuIndex = std::min(nuIndex, dimensions.nuSlices - 2u);
    const float nuFraction = lerpNu - static_cast<float>(nuIndex);
    const float u0 = (static_cast<float>(nuIndex) + uMuSun) / static_cast<float>(dimensions.nuSlices);
    const float u1 = (static_cast<float>(nuIndex + 1u) + uMuSun) / static_cast<float>(dimensions.nuSlices);
    const Vec4 low = sample3D(data, dimensions, u0, uMu, uR);
    const Vec4 high = sample3D(data, dimensions, u1, uMu, uR);
    return Vec4(low.x + (high.x - low.x) * nuFraction, low.y + (high.y - low.y) * nuFraction,
                low.z + (high.z - low.z) * nuFraction, low.w + (high.w - low.w) * nuFraction);
}

template <typename Function>
void parallelForLayers(uint32_t layerCount, Function&& function) {
    const uint32_t threadCount = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    if (threadCount <= 1 || layerCount <= 1) {
        for (uint32_t layer = 0; layer < layerCount; ++layer) {
            function(layer);
        }
        return;
    }
    std::vector<std::thread> workers;
    workers.reserve(std::min(threadCount, layerCount));
    for (uint32_t worker = 0; worker < std::min(threadCount, layerCount); ++worker) {
        workers.emplace_back([&, worker, threadCount]() {
            for (uint32_t layer = worker; layer < layerCount; layer += threadCount) {
                function(layer);
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
}

std::vector<float> buildSingleScatteringRadiance(const AtmosphereSettings& settings, const AtmosphereLut& lut,
                                                 const AtmosphereLutDimensions& dimensions) {
    const uint32_t width = dimensions.muSunSlices * dimensions.nuSlices;
    std::vector<float> radiance(static_cast<std::size_t>(width) * dimensions.muSlices * dimensions.radiusSlices *
                                    4u,
                                0.0f);
    parallelForLayers(dimensions.radiusSlices, [&](uint32_t layer) {
        const LayerParameters layerInfo = layerParameters(settings, dimensions, layer);
        for (uint32_t y = 0; y < dimensions.muSlices; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                float mu = 0.0f;
                float muSun = 0.0f;
                float nu = 0.0f;
                getMuMuSNu(settings, dimensions, layerInfo, x, y, mu, muSun, nu);
                const Vec4 single = sampleInscatterTexture(lut.inscatter, lut.dimensions, settings.planetRadius,
                                                            settings.atmosphereRadius, layerInfo.radius, mu, muSun,
                                                            nu);
                const Vec3 mie = mieFromLut(single, settings.betaRayleigh);
                const Vec3 value(single.x * phaseRayleigh(nu) + mie.x * phaseMie(settings, nu),
                                 single.y * phaseRayleigh(nu) + mie.y * phaseMie(settings, nu),
                                 single.z * phaseRayleigh(nu) + mie.z * phaseMie(settings, nu));
                const std::size_t index =
                    ((static_cast<std::size_t>(layer) * dimensions.muSlices + y) * width + x) * 4u;
                radiance[index + 0] = value.x;
                radiance[index + 1] = value.y;
                radiance[index + 2] = value.z;
                radiance[index + 3] = 1.0f;
            }
        }
    });
    return radiance;
}

std::vector<float> computeSkyIrradiance(const AtmosphereSettings& settings, const AtmosphereLutDimensions& dimensions,
                                        const std::vector<float>& radiance, uint32_t width, uint32_t height) {
    std::vector<float> result(static_cast<std::size_t>(width) * height * 4u, 0.0f);
    const float dtheta = 0.5f * kPi / static_cast<float>(kMultipleScatteringIrradianceThetaSamples);
    const float dphi = kPi / static_cast<float>(kMultipleScatteringIrradiancePhiSamples);
    for (uint32_t y = 0; y < height; ++y) {
        const float radius = settings.planetRadius +
                             static_cast<float>(y) / static_cast<float>(std::max(height - 1u, 1u)) *
                                 (settings.atmosphereRadius - settings.planetRadius);
        for (uint32_t x = 0; x < width; ++x) {
            const float muSun =
                -0.2f + static_cast<float>(x) / static_cast<float>(std::max(width - 1u, 1u)) * 1.2f;
            const float sinSun = std::sqrt(std::max(1.0f - muSun * muSun, 0.0f));
            const Vec3 sun(sinSun, 0.0f, muSun);
            Vec3 irradiance(0.0f, 0.0f, 0.0f);
            for (uint32_t thetaIndex = 0; thetaIndex < kMultipleScatteringIrradianceThetaSamples;
                 ++thetaIndex) {
                const float theta = (static_cast<float>(thetaIndex) + 0.5f) * dtheta;
                const float sinTheta = std::sin(theta);
                const float cosTheta = std::cos(theta);
                for (uint32_t phiIndex = 0; phiIndex < 2u * kMultipleScatteringIrradiancePhiSamples;
                     ++phiIndex) {
                    const float phi = (static_cast<float>(phiIndex) + 0.5f) * dphi;
                    const Vec3 direction(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);
                    const float nu = dot(sun, direction);
                    const float solidAngle = dtheta * dphi * sinTheta;
                    const Vec4 value =
                        sampleInscatterTexture(radiance, dimensions, settings.planetRadius,
                                               settings.atmosphereRadius, radius, cosTheta, muSun, nu);
                    irradiance = irradiance + Vec3(value.x, value.y, value.z) * (cosTheta * solidAngle);
                }
            }
            const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4u;
            result[index + 0] = irradiance.x;
            result[index + 1] = irradiance.y;
            result[index + 2] = irradiance.z;
            result[index + 3] = 1.0f;
        }
    }
    return result;
}

std::vector<float> buildMultipleScatteringSource(const AtmosphereSettings& settings, const AtmosphereLut& lut,
                                                 const AtmosphereLutDimensions& dimensions,
                                                 const std::vector<float>& radiance,
                                                 const std::vector<float>& groundIrradiance) {
    const uint32_t width = dimensions.muSunSlices * dimensions.nuSlices;
    std::vector<float> source(static_cast<std::size_t>(width) * dimensions.muSlices * dimensions.radiusSlices *
                                  4u,
                              0.0f);
    const float dtheta = kPi / static_cast<float>(kMultipleScatteringThetaSamples);
    const float dphi = kPi / static_cast<float>(kMultipleScatteringPhiSamples);
    parallelForLayers(dimensions.radiusSlices, [&](uint32_t layer) {
        const LayerParameters layerInfo = layerParameters(settings, dimensions, layer);
        const float radius = layerInfo.radius;
        const float densityRayleigh = std::exp(-(radius - settings.planetRadius) / settings.rayleighScaleHeight);
        const float densityMie = std::exp(-(radius - settings.planetRadius) / settings.mieScaleHeight);
        const float cthetamin =
            -std::sqrt(std::max(1.0f - (settings.planetRadius * settings.planetRadius) / (radius * radius), 0.0f));
        for (uint32_t y = 0; y < dimensions.muSlices; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                float mu = 0.0f;
                float muSun = 0.0f;
                float nu = 0.0f;
                getMuMuSNu(settings, dimensions, layerInfo, x, y, mu, muSun, nu);
                const float viewCos = std::sqrt(std::max(1.0f - mu * mu, 0.0f));
                const float sunCos = std::sqrt(std::max(1.0f - muSun * muSun, 0.0f));
                const float nuMin = muSun * mu - viewCos * sunCos;
                const float nuMax = muSun * mu + viewCos * sunCos;
                nu = std::clamp(nu, nuMin, nuMax);
                const Vec3 view(viewCos, 0.0f, mu);
                const float sx = view.x > 1.0e-6f ? std::clamp((nu - muSun * mu) / view.x, -1.0f, 1.0f) : 0.0f;
                const Vec3 sun(sx, std::sqrt(std::max(1.0f - sx * sx - muSun * muSun, 0.0f)), muSun);
                Vec3 value(0.0f, 0.0f, 0.0f);
                const float groundReflectance =
                    std::clamp(settings.averageGroundReflectance, 0.0f, 1.0f) / kPi;
                for (uint32_t thetaIndex = 0; thetaIndex < kMultipleScatteringThetaSamples; ++thetaIndex) {
                    const float theta = (static_cast<float>(thetaIndex) + 0.5f) * dtheta;
                    const float sinTheta = std::sin(theta);
                    const float cosTheta = std::cos(theta);
                    for (uint32_t phiIndex = 0; phiIndex < 2u * kMultipleScatteringPhiSamples;
                         ++phiIndex) {
                        const float phi = (static_cast<float>(phiIndex) + 0.5f) * dphi;
                        const Vec3 direction(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);
                        Vec3 incoming(0.0f, 0.0f, 0.0f);
                        if (cosTheta < cthetamin) {
                            const float delta = radius * radius * (cosTheta * cosTheta - 1.0f) +
                                                settings.planetRadius * settings.planetRadius;
                            const float groundDistance =
                                -radius * cosTheta - std::sqrt(std::max(delta, 0.0f));
                            if (groundDistance > 0.0f) {
                                const Vec3 groundTransmittance =
                                    transmittanceBetween(lut, settings, radius, cosTheta, groundDistance);
                                const Vec3 groundPoint =
                                    Vec3(0.0f, 0.0f, radius) + direction * groundDistance;
                                const Vec3 groundNormal = groundPoint * (1.0f / settings.planetRadius);
                                const float groundMuSun = dot(groundNormal, sun);
                                const Vec3 groundIrradianceValue = sampleIrradianceTexture(
                                    groundIrradiance, kMultipleScatteringSkyWidth,
                                    kMultipleScatteringSkyHeight, settings.planetRadius,
                                    settings.atmosphereRadius, settings.planetRadius, groundMuSun);
                                incoming = multiply(groundTransmittance, groundIrradianceValue) *
                                           groundReflectance;
                            }
                        }
                        const float nu1 = dot(sun, direction);
                        const Vec4 previous =
                            sampleInscatterTexture(radiance, dimensions, settings.planetRadius,
                                                   settings.atmosphereRadius, radius, cosTheta, muSun, nu1);
                        incoming = incoming + Vec3(previous.x, previous.y, previous.z);
                        const float nu2 = dot(view, direction);
                        const float phaseR = phaseRayleigh(nu2);
                        const float phaseM = phaseMie(settings, nu2);
                        const Vec3 coefficient =
                            Vec3(settings.betaRayleigh.x * densityRayleigh * phaseR +
                                     settings.betaMieScattering.x * densityMie * phaseM,
                                 settings.betaRayleigh.y * densityRayleigh * phaseR +
                                     settings.betaMieScattering.y * densityMie * phaseM,
                                 settings.betaRayleigh.z * densityRayleigh * phaseR +
                                     settings.betaMieScattering.z * densityMie * phaseM);
                        const float solidAngle = dtheta * dphi * sinTheta;
                        value = value + multiply(incoming, coefficient) * solidAngle;
                    }
                }
                const std::size_t index =
                    ((static_cast<std::size_t>(layer) * dimensions.muSlices + y) * width + x) * 4u;
                source[index + 0] = value.x;
                source[index + 1] = value.y;
                source[index + 2] = value.z;
                source[index + 3] = 1.0f;
            }
        }
    });
    return source;
}

std::vector<float> buildMultipleScatteringRay(const AtmosphereSettings& settings, const AtmosphereLut& lut,
                                              const AtmosphereLutDimensions& dimensions,
                                              const std::vector<float>& source) {
    const uint32_t width = dimensions.muSunSlices * dimensions.nuSlices;
    std::vector<float> result(static_cast<std::size_t>(width) * dimensions.muSlices * dimensions.radiusSlices *
                                  4u,
                              0.0f);
    parallelForLayers(dimensions.radiusSlices, [&](uint32_t layer) {
        const LayerParameters layerInfo = layerParameters(settings, dimensions, layer);
        const float radius = layerInfo.radius;
        for (uint32_t y = 0; y < dimensions.muSlices; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                float mu = 0.0f;
                float muSun = 0.0f;
                float nu = 0.0f;
                getMuMuSNu(settings, dimensions, layerInfo, x, y, mu, muSun, nu);
                const float distance = limitForLut(settings, radius, mu);
                Vec3 value(0.0f, 0.0f, 0.0f);
                if (distance > 0.0f) {
                    const float step = distance / static_cast<float>(kMultipleScatteringRaySamples);
                    for (uint32_t sample = 0; sample < kMultipleScatteringRaySamples; ++sample) {
                        const float t = (static_cast<float>(sample) + 0.5f) * step;
                        const float sampleRadius =
                            std::sqrt(std::max(radius * radius + t * t + 2.0f * radius * mu * t,
                                               settings.planetRadius * settings.planetRadius));
                        const float sampleMu = (radius * mu + t) / std::max(sampleRadius, 1.0e-6f);
                        const float sampleMuSun = (nu * t + muSun * radius) / std::max(sampleRadius, 1.0e-6f);
                        const Vec4 sourceValue =
                            sampleInscatterTexture(source, dimensions, settings.planetRadius,
                                                   settings.atmosphereRadius, sampleRadius, sampleMu,
                                                   sampleMuSun, nu);
                        const Vec3 transmittance = transmittanceBetween(lut, settings, radius, mu, t);
                        value = value + multiply(Vec3(sourceValue.x, sourceValue.y, sourceValue.z),
                                                 transmittance) *
                                            step;
                    }
                }
                const std::size_t index =
                    ((static_cast<std::size_t>(layer) * dimensions.muSlices + y) * width + x) * 4u;
                result[index + 0] = value.x;
                result[index + 1] = value.y;
                result[index + 2] = value.z;
                result[index + 3] = 1.0f;
            }
        }
    });
    return result;
}

void addInPlace(std::vector<float>& target, const std::vector<float>& source) {
    const std::size_t count = std::min(target.size(), source.size());
    for (std::size_t index = 0; index < count; index += 4u) {
        target[index + 0] += source[index + 0];
        target[index + 1] += source[index + 1];
        target[index + 2] += source[index + 2];
    }
}

void buildMultipleScattering(const AtmosphereSettings& settings, AtmosphereLut& lut) {
    if (settings.multipleScatteringIterations == 0) {
        return;
    }

    const uint32_t iterations = std::min(settings.multipleScatteringIterations, 8u);
    const AtmosphereLutDimensions dimensions = multipleScatteringDimensions();
    std::vector<float> singleRadiance = buildSingleScatteringRadiance(settings, lut, dimensions);
    std::vector<float> totalRadiance = singleRadiance;

    std::vector<float> coarseIrradiance(
        static_cast<std::size_t>(kMultipleScatteringSkyWidth) * kMultipleScatteringSkyHeight * 4u, 0.0f);
    for (uint32_t y = 0; y < kMultipleScatteringSkyHeight; ++y) {
        const float radius = settings.planetRadius +
                             static_cast<float>(y) /
                                 static_cast<float>(std::max(kMultipleScatteringSkyHeight - 1u, 1u)) *
                                 (settings.atmosphereRadius - settings.planetRadius);
        for (uint32_t x = 0; x < kMultipleScatteringSkyWidth; ++x) {
            const float muSun = -0.2f + static_cast<float>(x) /
                                            static_cast<float>(std::max(kMultipleScatteringSkyWidth - 1u, 1u)) *
                                            1.2f;
            const Vec3 direct =
                sampleIrradianceTexture(lut.irradiance, lut.dimensions.skyWidth, lut.dimensions.skyHeight,
                                        settings.planetRadius, settings.atmosphereRadius, radius, muSun);
            const std::size_t index = (static_cast<std::size_t>(y) * kMultipleScatteringSkyWidth + x) * 4u;
            coarseIrradiance[index + 0] = direct.x;
            coarseIrradiance[index + 1] = direct.y;
            coarseIrradiance[index + 2] = direct.z;
            coarseIrradiance[index + 3] = 1.0f;
        }
    }
    const std::vector<float> directIrradiance = coarseIrradiance;
    std::vector<float> previousOrder = singleRadiance;
    for (uint32_t iteration = 0; iteration < iterations; ++iteration) {
        const std::vector<float> source =
            buildMultipleScatteringSource(settings, lut, dimensions, previousOrder, coarseIrradiance);
        const std::vector<float> scattered =
            buildMultipleScatteringRay(settings, lut, dimensions, source);
        addInPlace(totalRadiance, scattered);
        addInPlace(coarseIrradiance,
                   computeSkyIrradiance(settings, dimensions, previousOrder, kMultipleScatteringSkyWidth,
                                        kMultipleScatteringSkyHeight));
        previousOrder = scattered;
    }
    addInPlace(coarseIrradiance,
               computeSkyIrradiance(settings, dimensions, previousOrder, kMultipleScatteringSkyWidth,
                                    kMultipleScatteringSkyHeight));

    std::vector<float> multipleScattering(totalRadiance.size(), 0.0f);
    for (std::size_t index = 0; index < totalRadiance.size(); index += 4u) {
        multipleScattering[index + 0] = std::max(totalRadiance[index + 0] - singleRadiance[index + 0], 0.0f);
        multipleScattering[index + 1] = std::max(totalRadiance[index + 1] - singleRadiance[index + 1], 0.0f);
        multipleScattering[index + 2] = std::max(totalRadiance[index + 2] - singleRadiance[index + 2], 0.0f);
        multipleScattering[index + 3] = 1.0f;
    }

    for (uint32_t y = 0; y < lut.dimensions.skyHeight; ++y) {
        const float radius =
            settings.planetRadius +
            static_cast<float>(y) / static_cast<float>(std::max(lut.dimensions.skyHeight - 1u, 1u)) *
                (settings.atmosphereRadius - settings.planetRadius);
        for (uint32_t x = 0; x < lut.dimensions.skyWidth; ++x) {
            const float muSun =
                -0.2f + static_cast<float>(x) /
                            static_cast<float>(std::max(lut.dimensions.skyWidth - 1u, 1u)) * 1.2f;
            const Vec3 sky = sampleIrradianceTexture(
                coarseIrradiance, kMultipleScatteringSkyWidth, kMultipleScatteringSkyHeight,
                settings.planetRadius, settings.atmosphereRadius, radius, muSun);
            const Vec3 direct = sampleIrradianceTexture(
                directIrradiance, kMultipleScatteringSkyWidth, kMultipleScatteringSkyHeight,
                settings.planetRadius, settings.atmosphereRadius, radius, muSun);
            const std::size_t index = (static_cast<std::size_t>(y) * lut.dimensions.skyWidth + x) * 4u;
            lut.irradiance[index + 0] += std::max(sky.x - direct.x, 0.0f);
            lut.irradiance[index + 1] += std::max(sky.y - direct.y, 0.0f);
            lut.irradiance[index + 2] += std::max(sky.z - direct.z, 0.0f);
        }
    }
    lut.multipleScattering = std::move(multipleScattering);
    lut.multipleScatteringDimensions = dimensions;
    lut.multipleScatteringIterations = iterations;
}

} // namespace

bool AtmosphereLut::valid() const {
    const uint64_t transmittanceTexels = static_cast<uint64_t>(dimensions.transmittanceWidth) *
                                         dimensions.transmittanceHeight;
    const uint64_t irradianceTexels = static_cast<uint64_t>(dimensions.skyWidth) * dimensions.skyHeight;
    const uint64_t inscatterTexels = static_cast<uint64_t>(dimensions.muSunSlices) * dimensions.nuSlices *
                                      dimensions.muSlices * dimensions.radiusSlices;
    const uint64_t multipleScatteringTexels =
        static_cast<uint64_t>(multipleScatteringDimensions.muSunSlices) *
        multipleScatteringDimensions.nuSlices * multipleScatteringDimensions.muSlices *
        multipleScatteringDimensions.radiusSlices;
    const bool multipleScatteringValid =
        multipleScatteringIterations == 0 ||
        (multipleScatteringDimensions.radiusSlices > 1 && multipleScatteringDimensions.muSlices > 1 &&
         multipleScatteringDimensions.muSunSlices > 1 && multipleScatteringDimensions.nuSlices > 1 &&
         multipleScattering.size() == multipleScatteringTexels * 4u);
    return dimensions.transmittanceWidth > 0 && dimensions.transmittanceHeight > 0 && dimensions.skyWidth > 0 &&
           dimensions.skyHeight > 0 && dimensions.radiusSlices > 1 && dimensions.muSlices > 1 &&
           dimensions.muSunSlices > 1 && dimensions.nuSlices > 1 &&
           transmittance.size() == transmittanceTexels * 4u &&
           irradiance.size() == irradianceTexels * 4u && inscatter.size() == inscatterTexels * 4u &&
           multipleScatteringValid;
}

bool buildAtmosphereLut(const AtmosphereSettings& settings, AtmosphereLut& lut,
                        const AtmosphereLutDimensions& requestedDimensions, std::string* error) {
    if (!settings.active()) {
        if (error != nullptr) {
            *error = "atmosphere settings are not active";
        }
        return false;
    }

    const AtmosphereLutDimensions dimensions = sanitizeDimensions(requestedDimensions);
    lut = {};
    lut.dimensions = dimensions;
    lut.planetRadius = settings.planetRadius;
    lut.atmosphereRadius = settings.atmosphereRadius;

    const uint64_t transmittanceTexels = static_cast<uint64_t>(dimensions.transmittanceWidth) *
                                         dimensions.transmittanceHeight;
    const uint64_t irradianceTexels = static_cast<uint64_t>(dimensions.skyWidth) * dimensions.skyHeight;
    const uint64_t inscatterTexels = static_cast<uint64_t>(dimensions.muSunSlices) * dimensions.nuSlices *
                                      dimensions.muSlices * dimensions.radiusSlices;
    lut.transmittance.assign(static_cast<std::size_t>(transmittanceTexels) * 4u, 0.0f);
    lut.irradiance.assign(static_cast<std::size_t>(irradianceTexels) * 4u, 0.0f);
    lut.inscatter.assign(static_cast<std::size_t>(inscatterTexels) * 4u, 0.0f);

    const float groundRadius = settings.planetRadius;
    const float topRadius = settings.atmosphereRadius;
    const Vec3 betaMieExtinction = settings.betaMieScattering * settings.mieExtinctionScale;

    for (uint32_t y = 0; y < dimensions.transmittanceHeight; ++y) {
        const float uR = (static_cast<float>(y) + 0.5f) / static_cast<float>(dimensions.transmittanceHeight);
        const float radius = groundRadius + uR * uR * (topRadius - groundRadius);
        for (uint32_t x = 0; x < dimensions.transmittanceWidth; ++x) {
            const float uMu = (static_cast<float>(x) + 0.5f) / static_cast<float>(dimensions.transmittanceWidth);
            const float mu = -0.15f + std::tan(1.5f * uMu) / std::tan(1.5f) * (1.0f + 0.15f);
            const float rayleigh = opticalDepth(settings, settings.rayleighScaleHeight, radius, mu);
            const float mie = opticalDepth(settings, settings.mieScaleHeight, radius, mu);
            const std::size_t index = (static_cast<std::size_t>(y) * dimensions.transmittanceWidth + x) * 4u;
            lut.transmittance[index + 0] = std::exp(-settings.betaRayleigh.x * rayleigh -
                                                    betaMieExtinction.x * mie);
            lut.transmittance[index + 1] = std::exp(-settings.betaRayleigh.y * rayleigh -
                                                    betaMieExtinction.y * mie);
            lut.transmittance[index + 2] = std::exp(-settings.betaRayleigh.z * rayleigh -
                                                    betaMieExtinction.z * mie);
            lut.transmittance[index + 3] = 1.0f;
        }
    }

    for (uint32_t y = 0; y < dimensions.skyHeight; ++y) {
        const float radius = groundRadius +
                             static_cast<float>(y) / static_cast<float>(dimensions.skyHeight - 1u) *
                                 (topRadius - groundRadius);
        for (uint32_t x = 0; x < dimensions.skyWidth; ++x) {
            const float muSun =
                -0.2f + static_cast<float>(x) / static_cast<float>(dimensions.skyWidth - 1u) * 1.2f;
            const Vec3 transmittance = sampleTransmittance(lut, settings, radius, muSun);
            const float direct = std::max(muSun, 0.0f);
            const std::size_t index = (static_cast<std::size_t>(y) * dimensions.skyWidth + x) * 4u;
            lut.irradiance[index + 0] = transmittance.x * direct;
            lut.irradiance[index + 1] = transmittance.y * direct;
            lut.irradiance[index + 2] = transmittance.z * direct;
            lut.irradiance[index + 3] = 1.0f;
        }
    }

    const uint32_t inscatterWidth = dimensions.muSunSlices * dimensions.nuSlices;
    const auto buildInscatterLayer = [&](uint32_t layer) {
        const LayerParameters layerInfo = layerParameters(settings, dimensions, layer);
        const float radius = layerInfo.radius;
        for (uint32_t y = 0; y < dimensions.muSlices; ++y) {
            for (uint32_t x = 0; x < inscatterWidth; ++x) {
                float mu = 0.0f;
                float muSun = 0.0f;
                float nu = 0.0f;
                getMuMuSNu(settings, dimensions, layerInfo, x, y, mu, muSun, nu);
                const float distance = limitForLut(settings, radius, mu);
                Vec3 rayleigh(0.0f, 0.0f, 0.0f);
                Vec3 mie(0.0f, 0.0f, 0.0f);
                if (distance > 0.0f) {
                    const float step = distance / static_cast<float>(kInscatterSamples);
                    const auto integrand = [&](float t, Vec3& rayValue, Vec3& mieValue) {
                        rayValue = Vec3(0.0f, 0.0f, 0.0f);
                        mieValue = Vec3(0.0f, 0.0f, 0.0f);
                        const float sampleRadius =
                            std::sqrt(std::max(radius * radius + t * t + 2.0f * radius * mu * t,
                                               groundRadius * groundRadius));
                        const float sampleMuSun = (nu * t + muSun * radius) / sampleRadius;
                        const float horizon = -std::sqrt(
                            std::max(1.0f - (groundRadius * groundRadius) / (sampleRadius * sampleRadius), 0.0f));
                        if (sampleMuSun < horizon) {
                            return;
                        }
                        const Vec3 viewTransmittance =
                            transmittanceBetween(lut, settings, radius, mu, t);
                        const Vec3 sunTransmittance =
                            sampleTransmittance(lut, settings, sampleRadius, sampleMuSun);
                        const Vec3 transmittance = multiply(viewTransmittance, sunTransmittance);
                        rayValue = transmittance * std::exp(-(sampleRadius - groundRadius) /
                                                            settings.rayleighScaleHeight);
                        mieValue = transmittance * std::exp(-(sampleRadius - groundRadius) /
                                                            settings.mieScaleHeight);
                    };

                    Vec3 previousRay;
                    Vec3 previousMie;
                    integrand(0.0f, previousRay, previousMie);
                    for (uint32_t sample = 1; sample <= kInscatterSamples; ++sample) {
                        Vec3 currentRay;
                        Vec3 currentMie;
                        integrand(static_cast<float>(sample) * step, currentRay, currentMie);
                        rayleigh = rayleigh + (previousRay + currentRay) * (0.5f * step);
                        mie = mie + (previousMie + currentMie) * (0.5f * step);
                        previousRay = currentRay;
                        previousMie = currentMie;
                    }
                }
                rayleigh = multiply(rayleigh, settings.betaRayleigh);
                mie = multiply(mie, settings.betaMieScattering);
                const std::size_t index =
                    ((static_cast<std::size_t>(layer) * dimensions.muSlices + y) * inscatterWidth + x) * 4u;
                lut.inscatter[index + 0] = rayleigh.x;
                lut.inscatter[index + 1] = rayleigh.y;
                lut.inscatter[index + 2] = rayleigh.z;
                lut.inscatter[index + 3] = mie.x;
            }
        }
    };


    const uint32_t threadCount = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (uint32_t worker = 0; worker < threadCount; ++worker)
    {
        workers.emplace_back([&, worker]() {
            for (uint32_t layer = worker; layer < dimensions.radiusSlices; layer += threadCount)
            {
                buildInscatterLayer(layer);
            }
        });
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }
    buildMultipleScattering(settings, lut);

    if (!lut.valid()) {
        if (error != nullptr) {
            *error = "built atmosphere LUT has invalid dimensions or size";
        }
        return false;
    }
    return true;
}

Vec3 sampleAtmosphereLutTransmittance(const AtmosphereLut& lut, float radius, float mu) {
    if (!lut.valid()) {
        return Vec3(1.0f, 1.0f, 1.0f);
    }
    const float groundRadius = lut.planetRadius;
    const float topRadius = lut.atmosphereRadius;
    const float uR = std::sqrt(std::clamp((radius - groundRadius) / (topRadius - groundRadius), 0.0f, 1.0f));
    const float uMu = std::atan((std::clamp(mu, -1.0f, 1.0f) + 0.15f) / 1.15f * std::tan(1.5f)) / 1.5f;
    const Vec4 value =
        sample2D(lut.transmittance, lut.dimensions.transmittanceWidth, lut.dimensions.transmittanceHeight, uMu, uR);
    return Vec3(value.x, value.y, value.z);
}

Vec3 sampleAtmosphereLutIrradiance(const AtmosphereLut& lut, float radius, float muSun) {
    if (!lut.valid()) {
        return Vec3(0.0f, 0.0f, 0.0f);
    }
    const float groundRadius = lut.planetRadius;
    const float topRadius = lut.atmosphereRadius;
    const float uR = std::clamp((radius - groundRadius) / (topRadius - groundRadius), 0.0f, 1.0f);
    const float uMuSun = std::clamp((muSun + 0.2f) / 1.2f, 0.0f, 1.0f);
    const Vec4 value = sample2D(lut.irradiance, lut.dimensions.skyWidth, lut.dimensions.skyHeight, uMuSun, uR);
    return Vec3(value.x, value.y, value.z);
}

Vec4 sampleAtmosphereLutInscatter(const AtmosphereLut& lut, float radius, float mu, float muSun, float nu) {
    if (!lut.valid()) {
        return Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    const float groundRadius = lut.planetRadius;
    const float topRadius = lut.atmosphereRadius;
    const float height = std::sqrt(std::max(topRadius * topRadius - groundRadius * groundRadius, 0.0f));
    const float rho = std::sqrt(std::max(radius * radius - groundRadius * groundRadius, 0.0f));
    const float uR = 0.5f / static_cast<float>(lut.dimensions.radiusSlices) +
                     rho / std::max(height, 1.0e-6f) *
                         (1.0f - 1.0f / static_cast<float>(lut.dimensions.radiusSlices));
    const float rmu = radius * mu;
    const float delta = rmu * rmu - radius * radius + groundRadius * groundRadius;
    const bool belowHorizon = rmu < 0.0f && delta > 0.0f;
    const float cstW = belowHorizon ? 0.5f - 0.5f / static_cast<float>(lut.dimensions.muSlices)
                                    : 0.5f + 0.5f / static_cast<float>(lut.dimensions.muSlices);
    const float cstX = belowHorizon ? 1.0f : -1.0f;
    const float cstY = belowHorizon ? 0.0f : height * height;
    const float cstZ = belowHorizon ? 0.0f : height;
    const float uMu = cstW + (rmu * cstX + std::sqrt(std::max(delta + cstY, 0.0f))) /
                                 std::max(rho + cstZ, 1.0e-6f) *
                                 (0.5f - 1.0f / static_cast<float>(lut.dimensions.muSlices));
    const float uMuSun =
        0.5f / static_cast<float>(lut.dimensions.muSunSlices) +
        (std::atan(std::max(muSun, -0.1975f) * std::tan(1.26f * 1.1f)) / 1.1f + (1.0f - 0.26f)) * 0.5f *
            (1.0f - 1.0f / static_cast<float>(lut.dimensions.muSunSlices));
    const float lerpNu = (std::clamp(nu, -1.0f, 1.0f) + 1.0f) * 0.5f *
                         static_cast<float>(lut.dimensions.nuSlices - 1u);
    uint32_t nuIndex = static_cast<uint32_t>(std::floor(lerpNu));
    nuIndex = std::min(nuIndex, lut.dimensions.nuSlices - 2u);
    const float nuFraction = lerpNu - static_cast<float>(nuIndex);
    const float u0 = (static_cast<float>(nuIndex) + uMuSun) / static_cast<float>(lut.dimensions.nuSlices);
    const float u1 = (static_cast<float>(nuIndex + 1u) + uMuSun) /
                     static_cast<float>(lut.dimensions.nuSlices);
    const Vec4 low = sample3D(lut.inscatter, lut.dimensions, u0, uMu, uR);
    const Vec4 high = sample3D(lut.inscatter, lut.dimensions, u1, uMu, uR);
    return Vec4(low.x + (high.x - low.x) * nuFraction, low.y + (high.y - low.y) * nuFraction,
                low.z + (high.z - low.z) * nuFraction, low.w + (high.w - low.w) * nuFraction);
}

Vec4 sampleAtmosphereLutMultipleScattering(const AtmosphereLut& lut, float radius, float mu, float muSun,
                                           float nu) {
    if (!lut.valid() || lut.multipleScatteringIterations == 0 || lut.multipleScattering.empty()) {
        return Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    return sampleInscatterTexture(lut.multipleScattering, lut.multipleScatteringDimensions, lut.planetRadius,
                                  lut.atmosphereRadius, radius, mu, muSun, nu);
}

} // namespace hbg::render

