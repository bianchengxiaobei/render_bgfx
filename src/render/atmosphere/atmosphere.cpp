#include "render/atmosphere/atmosphere.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <iterator>

namespace hbg::render {
namespace {

using Json = nlohmann::json;

constexpr float kPi = 3.14159265358979323846f;

float finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

float clampMu(float value) {
    return std::clamp(value, -1.0f, 1.0f);
}

float opticalDepthAtHeight(float scaleHeight, float radius, float groundRadius, float mu, float distance) {
    if (distance <= 0.0f || scaleHeight <= 0.0f) {
        return 0.0f;
    }
    const float a = std::sqrt(0.5f / scaleHeight * radius);
    const float a0 = a * mu;
    const float a1 = a * (mu + distance / radius);
    const float a0sq = a0 * a0;
    const float a1sq = a1 * a1;
    const float s0 = a0 < 0.0f ? -1.0f : 1.0f;
    const float s1 = a1 < 0.0f ? -1.0f : 1.0f;
    const float x = s1 > s0 ? std::exp(a0sq) : 0.0f;
    const float y0 = s0 / (2.3193f * std::abs(a0) + std::sqrt(1.52f * a0sq + 4.0f));
    const float y1 =
        s1 / (2.3193f * std::abs(a1) + std::sqrt(1.52f * a1sq + 4.0f)) *
        std::exp(-distance / scaleHeight * (distance / (2.0f * radius) + mu));
    return std::sqrt(6.2831f * scaleHeight * radius) * std::exp((groundRadius - radius) / scaleHeight) *
           (x + y0 - y1);
}

Vec3 finiteOr(const Vec3& value, const Vec3& fallback) {
    return Vec3(finiteOr(value.x, fallback.x), finiteOr(value.y, fallback.y), finiteOr(value.z, fallback.z));
}

bool readBool(const Json& object, const char* key, bool& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return true;
    }
    if (!it->is_boolean()) {
        error = std::string(key) + " must be a boolean";
        return false;
    }
    value = it->get<bool>();
    return true;
}

bool readFloat(const Json& object, const char* key, float& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return true;
    }
    if (!it->is_number()) {
        error = std::string(key) + " must be a number";
        return false;
    }
    value = it->get<float>();
    return true;
}

bool readUint(const Json& object, const char* key, uint32_t& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return true;
    }
    if (!it->is_number_integer()) {
        error = std::string(key) + " must be an integer";
        return false;
    }
    const int parsed = it->get<int>();
    if (parsed < 0) {
        error = std::string(key) + " must be non-negative";
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool readVec3(const Json& object, const char* key, Vec3& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return true;
    }
    if (!it->is_array() || it->size() != 3 || !(*it)[0].is_number() || !(*it)[1].is_number() ||
        !(*it)[2].is_number()) {
        error = std::string(key) + " must be an array of three numbers";
        return false;
    }
    value = Vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
    return true;
}

Vec3 integrateScattering(const AtmosphereSettings& settings, const Vec3& cameraPosition,
                         const Vec3& viewDirection, const Vec3& sunDirection, float maxDistance,
                         uint32_t stepCount, Vec3* transmittance) {
    const float radius = std::max(length(cameraPosition), settings.planetRadius);
    const Vec3 view = normalize(viewDirection);
    const Vec3 sun = normalize(sunDirection);
    const float mu = clampMu(dot(cameraPosition, view) / radius);
    const float muSun = clampMu(dot(cameraPosition, sun) / radius);
    const float nu = clampMu(dot(view, sun));
    const float limit = atmosphereRayLimit(settings, radius, mu);
    const float distance = std::max(std::min(maxDistance, limit), 0.0f);
    if (transmittance != nullptr) {
        *transmittance = atmosphereTransmittance(settings, radius, mu, distance);
    }
    if (distance <= 0.0f) {
        return Vec3(0.0f, 0.0f, 0.0f);
    }

    const uint32_t steps = std::clamp(stepCount, 1u, 64u);
    const float stepLength = distance / static_cast<float>(steps);
    const float groundRadius = settings.planetRadius;
    Vec3 rayleigh(0.0f, 0.0f, 0.0f);
    Vec3 mie(0.0f, 0.0f, 0.0f);
    for (uint32_t index = 0; index < steps; ++index) {
        const float t = (static_cast<float>(index) + 0.5f) * stepLength;
        const float sampleRadius =
            std::sqrt(std::max(radius * radius + t * t + 2.0f * radius * mu * t, groundRadius * groundRadius));
        const float sampleMuSun = clampMu((nu * t + muSun * radius) / sampleRadius);
        const float sampleHorizonRatio =
            std::max(1.0f - (groundRadius * groundRadius) / (sampleRadius * sampleRadius), 0.0f);
        if (sampleMuSun < -std::sqrt(sampleHorizonRatio)) {
            continue;
        }

        const Vec3 viewTransmittance = atmosphereTransmittance(settings, radius, mu, t);
        const Vec3 sunTransmittance = atmosphereSunTransmittance(settings, sampleRadius, sampleMuSun);
        const float densityRayleigh = std::exp(-(sampleRadius - groundRadius) / settings.rayleighScaleHeight);
        const float densityMie = std::exp(-(sampleRadius - groundRadius) / settings.mieScaleHeight);
        const float rayleighWeight = densityRayleigh * stepLength;
        const float mieWeight = densityMie * stepLength;
        rayleigh = rayleigh + Vec3(viewTransmittance.x * sunTransmittance.x * rayleighWeight,
                                   viewTransmittance.y * sunTransmittance.y * rayleighWeight,
                                   viewTransmittance.z * sunTransmittance.z * rayleighWeight);
        mie = mie + Vec3(viewTransmittance.x * sunTransmittance.x * mieWeight,
                         viewTransmittance.y * sunTransmittance.y * mieWeight,
                         viewTransmittance.z * sunTransmittance.z * mieWeight);
    }

    const float phaseRayleigh = 3.0f / (16.0f * kPi) * (1.0f + nu * nu);
    const float g = settings.miePhaseG;
    const float mieDenom = std::max(1.0f + g * g - 2.0f * g * nu, 1.0e-6f);
    const float phaseMie = 1.5f * (1.0f - g * g) / (4.0f * kPi) *
                           std::pow(mieDenom, -1.5f) * (1.0f + nu * nu) / (2.0f + g * g);
    Vec3 result(rayleigh.x * settings.betaRayleigh.x * phaseRayleigh +
                    mie.x * settings.betaMieScattering.x * phaseMie,
                rayleigh.y * settings.betaRayleigh.y * phaseRayleigh +
                    mie.y * settings.betaMieScattering.y * phaseMie,
                rayleigh.z * settings.betaRayleigh.z * phaseRayleigh +
                    mie.z * settings.betaMieScattering.z * phaseMie);
    if (result.x < 0.0f) {
        result.x = 0.0f;
    }
    if (result.y < 0.0f) {
        result.y = 0.0f;
    }
    if (result.z < 0.0f) {
        result.z = 0.0f;
    }
    return result * settings.sunIntensity;
}

} // namespace

bool AtmosphereSettings::active() const {
    return enabled && std::isfinite(planetRadius) && std::isfinite(atmosphereRadius) &&
           atmosphereRadius > planetRadius && std::isfinite(rayleighScaleHeight) &&
           rayleighScaleHeight > 0.0f && std::isfinite(mieScaleHeight) && mieScaleHeight > 0.0f &&
           std::isfinite(sunIntensity) && sunIntensity >= 0.0f && skyStepCount > 0 && aerialStepCount > 0;
}

void sanitizeAtmosphereSettings(AtmosphereSettings& settings) {
    settings.planetRadius = std::clamp(finiteOr(settings.planetRadius, 6360000.0f), 1000.0f, 1.0e9f);
    settings.atmosphereRadius = std::clamp(
        finiteOr(settings.atmosphereRadius, settings.planetRadius + 60000.0f), settings.planetRadius + 1000.0f,
        1.1e9f);
    const float rayleighScaleHeight = finiteOr(settings.rayleighScaleHeight, 8000.0f);
    const float mieScaleHeight = finiteOr(settings.mieScaleHeight, 1200.0f);
    settings.rayleighScaleHeight =
        rayleighScaleHeight > 0.0f ? std::clamp(rayleighScaleHeight, 1.0f, settings.atmosphereRadius) : 8000.0f;
    settings.mieScaleHeight =
        mieScaleHeight > 0.0f ? std::clamp(mieScaleHeight, 1.0f, settings.atmosphereRadius) : 1200.0f;
    settings.betaRayleigh = finiteOr(settings.betaRayleigh, Vec3(5.8e-6f, 1.35e-5f, 3.31e-5f));
    settings.betaMieScattering = finiteOr(settings.betaMieScattering, Vec3(4.0e-6f, 4.0e-6f, 4.0e-6f));
    settings.betaRayleigh.x = std::clamp(settings.betaRayleigh.x, 0.0f, 1.0f);
    settings.betaRayleigh.y = std::clamp(settings.betaRayleigh.y, 0.0f, 1.0f);
    settings.betaRayleigh.z = std::clamp(settings.betaRayleigh.z, 0.0f, 1.0f);
    settings.betaMieScattering.x = std::clamp(settings.betaMieScattering.x, 0.0f, 1.0f);
    settings.betaMieScattering.y = std::clamp(settings.betaMieScattering.y, 0.0f, 1.0f);
    settings.betaMieScattering.z = std::clamp(settings.betaMieScattering.z, 0.0f, 1.0f);
    settings.mieExtinctionScale = std::clamp(finiteOr(settings.mieExtinctionScale, 1.0f / 0.9f), 1.0f, 4.0f);
    settings.miePhaseG = std::clamp(finiteOr(settings.miePhaseG, 0.8f), -0.99f, 0.99f);
    settings.sunIntensity = std::clamp(finiteOr(settings.sunIntensity, 10.0f), 0.0f, 1000000.0f);
    settings.averageGroundReflectance =
        std::clamp(finiteOr(settings.averageGroundReflectance, 0.1f), 0.0f, 1.0f);
    settings.multipleScatteringIterations =
        std::clamp(settings.multipleScatteringIterations, 0u, 8u);
    settings.skyStepCount = std::clamp(settings.skyStepCount, 1u, 64u);
    settings.aerialStepCount = std::clamp(settings.aerialStepCount, 1u, 64u);
}

bool parseAtmosphereSettings(std::string_view json, AtmosphereSettings& settings, std::string& error) {
    Json root;
    try {
        root = Json::parse(json.begin(), json.end());
    } catch (const std::exception& exception) {
        error = std::string("failed to parse atmosphere settings: ") + exception.what();
        return false;
    }
    if (!root.is_object()) {
        error = "atmosphere settings must be a JSON object";
        return false;
    }

    if (!readBool(root, "enabled", settings.enabled, error) ||
        !readBool(root, "aerialPerspective", settings.aerialPerspective, error) ||
        !readBool(root, "usePrecomputedScattering", settings.usePrecomputedScattering, error) ||
        !readFloat(root, "planetRadius", settings.planetRadius, error) ||
        !readFloat(root, "atmosphereRadius", settings.atmosphereRadius, error) ||
        !readFloat(root, "rayleighScaleHeight", settings.rayleighScaleHeight, error) ||
        !readFloat(root, "mieScaleHeight", settings.mieScaleHeight, error) ||
        !readVec3(root, "betaRayleigh", settings.betaRayleigh, error) ||
        !readVec3(root, "betaMieScattering", settings.betaMieScattering, error) ||
        !readFloat(root, "mieExtinctionScale", settings.mieExtinctionScale, error) ||
        !readFloat(root, "miePhaseG", settings.miePhaseG, error) ||
        !readFloat(root, "sunIntensity", settings.sunIntensity, error) ||
        !readFloat(root, "averageGroundReflectance", settings.averageGroundReflectance, error) ||
        !readUint(root, "multipleScatteringIterations", settings.multipleScatteringIterations, error) ||
        !readUint(root, "skyStepCount", settings.skyStepCount, error) ||
        !readUint(root, "aerialStepCount", settings.aerialStepCount, error)) {
        return false;
    }

    sanitizeAtmosphereSettings(settings);
    return true;
}

bool loadAtmosphereSettings(const std::string& path, AtmosphereSettings& settings, std::string& error) {
    std::ifstream file(path);
    if (!file.is_open()) {
        error = "failed to open atmosphere settings '" + path + "'";
        return false;
    }
    std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return parseAtmosphereSettings(json, settings, error);
}

Vec3 atmosphereCameraPosition(const AtmosphereSettings& settings, const Vec3& localCameraPosition) {
    return Vec3(0.0f, settings.planetRadius + std::max(localCameraPosition.y, 0.0f), 0.0f);
}

float atmosphereRayLimit(const AtmosphereSettings& settings, float radius, float mu) {
    const float groundRadius = settings.planetRadius;
    const float topRadius = settings.atmosphereRadius;
    const float mu2 = mu * mu;
    const float outer = -radius * mu + std::sqrt(std::max(radius * radius * (mu2 - 1.0f) + topRadius * topRadius, 0.0f));
    const float delta = radius * radius * (mu2 - 1.0f) + groundRadius * groundRadius;
    if (delta >= 0.0f) {
        const float inner = -radius * mu - std::sqrt(delta);
        if (inner >= 0.0f) {
            return std::min(outer, inner);
        }
    }
    return outer;
}

AtmosphereHit intersectAtmosphere(const AtmosphereSettings& settings, const Vec3& cameraPosition,
                                  const Vec3& direction) {
    AtmosphereHit hit;
    const float radius = std::max(length(cameraPosition), settings.planetRadius);
    const Vec3 ray = normalize(direction);
    const float mu = clampMu(dot(cameraPosition, ray) / radius);
    hit.distance = atmosphereRayLimit(settings, radius, mu);
    hit.valid = hit.distance > 0.0f;
    if (!hit.valid) {
        return hit;
    }
    hit.position = cameraPosition + ray * hit.distance;
    const float groundRadius = settings.planetRadius;
    const float delta = radius * radius * (mu * mu - 1.0f) + groundRadius * groundRadius;
    if (delta >= 0.0f) {
        const float inner = -radius * mu - std::sqrt(delta);
        hit.ground = inner >= 0.0f && inner <= hit.distance;
    }
    return hit;
}

Vec3 atmosphereTransmittance(const AtmosphereSettings& settings, float radius, float mu, float distance) {
    const float rayleigh = opticalDepthAtHeight(settings.rayleighScaleHeight, radius, settings.planetRadius, mu,
                                                distance);
    const float mie = opticalDepthAtHeight(settings.mieScaleHeight, radius, settings.planetRadius, mu, distance);
    return Vec3(std::exp(-settings.betaRayleigh.x * rayleigh - settings.betaMieScattering.x *
                             settings.mieExtinctionScale * mie),
                std::exp(-settings.betaRayleigh.y * rayleigh - settings.betaMieScattering.y *
                             settings.mieExtinctionScale * mie),
                std::exp(-settings.betaRayleigh.z * rayleigh - settings.betaMieScattering.z *
                             settings.mieExtinctionScale * mie));
}

Vec3 atmosphereSunTransmittance(const AtmosphereSettings& settings, float radius, float muSun) {
    const float groundRadius = settings.planetRadius;
    const float horizonRatio = std::max(1.0f - (groundRadius * groundRadius) / (radius * radius), 0.0f);
    if (muSun < -std::sqrt(horizonRatio)) {
        return Vec3(0.0f, 0.0f, 0.0f);
    }
    return atmosphereTransmittance(settings, radius, muSun, atmosphereRayLimit(settings, radius, muSun));
}

Vec3 atmosphereSkyRadiance(const AtmosphereSettings& settings, const Vec3& cameraPosition,
                           const Vec3& viewDirection, const Vec3& sunDirection, uint32_t stepCount,
                           Vec3* transmittance) {
    if (!settings.active()) {
        if (transmittance != nullptr) {
            *transmittance = Vec3(1.0f, 1.0f, 1.0f);
        }
        return Vec3(0.0f, 0.0f, 0.0f);
    }
    const Vec3 camera = Vec3(0.0f, std::max(cameraPosition.y, settings.planetRadius + 1.0f), 0.0f);
    return integrateScattering(settings, camera, viewDirection, sunDirection, 1.0e30f, stepCount, transmittance);
}

namespace {

Vec3 atmosphereLutSkyRadiance(const AtmosphereLut& lut, const AtmosphereSettings& settings,
                              const Vec3& camera, const Vec3& viewDirection, const Vec3& sunDirection) {
    if (!lut.valid() || !settings.active()) {
        return Vec3(0.0f, 0.0f, 0.0f);
    }

    const Vec3 view = normalize(viewDirection);
    const Vec3 sun = normalize(sunDirection);
    const float radius = std::max(length(camera), settings.planetRadius + 1.0f);
    const float mu = clampMu(dot(camera, view) / radius);
    const float muSun = clampMu(dot(camera, sun) / radius);
    const float nu = clampMu(dot(view, sun));
    const Vec4 inScatter = sampleAtmosphereLutInscatter(lut, radius, mu, muSun, nu);
    const Vec4 multipleScattering = sampleAtmosphereLutMultipleScattering(lut, radius, mu, muSun, nu);
    const Vec3 mie = Vec3(inScatter.x * inScatter.w, inScatter.y * inScatter.w, inScatter.z * inScatter.w) *
                     (1.0f / std::max(inScatter.x, 1.0e-4f));
    const Vec3 betaRatio(settings.betaRayleigh.x / std::max(settings.betaRayleigh.x, 1.0e-6f),
                         settings.betaRayleigh.x / std::max(settings.betaRayleigh.y, 1.0e-6f),
                         settings.betaRayleigh.x / std::max(settings.betaRayleigh.z, 1.0e-6f));
    const Vec3 mieRadiance(mie.x * betaRatio.x, mie.y * betaRatio.y, mie.z * betaRatio.z);
    const float phaseRayleigh = 3.0f / (16.0f * kPi) * (1.0f + nu * nu);
    const float g = settings.miePhaseG;
    const float mieDenominator = std::max(1.0f + g * g - 2.0f * g * nu, 1.0e-6f);
    const float phaseMie = 1.5f * (1.0f - g * g) / (4.0f * kPi) *
                           std::pow(mieDenominator, -1.5f) * (1.0f + nu * nu) / (2.0f + g * g);
    const Vec3 result(inScatter.x * phaseRayleigh + mieRadiance.x * phaseMie + multipleScattering.x,
                      inScatter.y * phaseRayleigh + mieRadiance.y * phaseMie + multipleScattering.y,
                      inScatter.z * phaseRayleigh + mieRadiance.z * phaseMie + multipleScattering.z);
    return Vec3(std::max(result.x, 0.0f), std::max(result.y, 0.0f), std::max(result.z, 0.0f)) *
           settings.sunIntensity;
}

double skyShBasis(int index, const Vec3& direction) {
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

} // namespace

std::array<Vec4, 7> buildAtmosphereSkySh9(const AtmosphereLut& lut, const AtmosphereSettings& settings,
                                          const Vec3& sunDirection) {
    std::array<Vec4, 7> packed{};
    if (!lut.valid() || !settings.active()) {
        return packed;
    }

    constexpr uint32_t width = 64;
    constexpr uint32_t height = 32;
    constexpr std::array<double, 9> kBasisScale = {
        0.28209479177387814347, 0.48860251190291992159, 0.48860251190291992159,
        0.48860251190291992159, 1.09254843059207907054, 1.09254843059207907054,
        0.31539156525252000603, 1.09254843059207907054, 0.54627421529603953527,
    };

    const float sunLength = length(sunDirection);
    const Vec3 sun = sunLength > 1.0e-6f ? sunDirection * (1.0f / sunLength) : Vec3(0.0f, 1.0f, 0.0f);
    const Vec3 camera = atmosphereCameraPosition(settings, Vec3(0.0f, 10.0f, 0.0f));
    std::array<std::array<double, 9>, 3> coefficients{};
    for (uint32_t y = 0; y < height; ++y) {
        const double theta = (static_cast<double>(y) + 0.5) / static_cast<double>(height) * kPi;
        const double sinTheta = std::sin(theta);
        const double cosTheta = std::cos(theta);
        if (cosTheta <= 0.0) {
            continue;
        }
        const double solidAngle = (2.0 * kPi / static_cast<double>(width)) *
                                  (kPi / static_cast<double>(height)) * sinTheta;
        for (uint32_t x = 0; x < width; ++x) {
            const double phi = (static_cast<double>(x) + 0.5) / static_cast<double>(width) * 2.0 * kPi;
            const Vec3 direction(static_cast<float>(sinTheta * std::sin(phi)), static_cast<float>(cosTheta),
                                 static_cast<float>(sinTheta * std::cos(phi)));
            const Vec3 radiance = atmosphereLutSkyRadiance(lut, settings, camera, direction, sun);
            for (int index = 0; index < 9; ++index) {
                const double basis = skyShBasis(index, direction) * solidAngle;
                coefficients[0][static_cast<std::size_t>(index)] += static_cast<double>(radiance.x) * basis;
                coefficients[1][static_cast<std::size_t>(index)] += static_cast<double>(radiance.y) * basis;
                coefficients[2][static_cast<std::size_t>(index)] += static_cast<double>(radiance.z) * basis;
            }
        }
    }

    // Keep radiance SH (not the cosine-convolved irradiance SH used by the
    // environment probe) so reflection evaluates the sky radiance directly.
    for (std::array<double, 9>& channel : coefficients) {
        for (int index = 0; index < 9; ++index) {
            channel[static_cast<std::size_t>(index)] *= kBasisScale[static_cast<std::size_t>(index)];
        }
    }

    for (int channel = 0; channel < 3; ++channel) {
        const auto& c = coefficients[static_cast<std::size_t>(channel)];
        packed[static_cast<std::size_t>(channel)] =
            Vec4(static_cast<float>(c[3]), static_cast<float>(c[1]), static_cast<float>(c[2]),
                 static_cast<float>(c[0] - c[6]));
        packed[static_cast<std::size_t>(3 + channel)] =
            Vec4(static_cast<float>(c[4]), static_cast<float>(c[5]), static_cast<float>(c[6] * 3.0),
                 static_cast<float>(c[7]));
    }
    packed[6] = Vec4(static_cast<float>(coefficients[0][8]), static_cast<float>(coefficients[1][8]),
                     static_cast<float>(coefficients[2][8]), 1.0f);
    return packed;
}

Vec3 evaluateAtmosphereSkySh9(const std::array<Vec4, 7>& coefficients, const Vec3& direction) {
    const Vec3 d = normalize(direction);
    const Vec4 vA(d.x, d.y, d.z, 1.0f);
    const Vec4 vB(d.x * d.y, d.y * d.z, d.z * d.z, d.z * d.x);
    const float vC = d.x * d.x - d.y * d.y;
    const auto evaluateChannel = [&](const Vec4& a, const Vec4& b, float c) {
        const float constant = a.x * vA.x + a.y * vA.y + a.z * vA.z + a.w * vA.w;
        const float quadratic = b.x * vB.x + b.y * vB.y + b.z * vB.z + b.w * vB.w;
        return std::max(constant + quadratic + c * vC, 0.0f);
    };
    return Vec3(evaluateChannel(coefficients[0], coefficients[3], coefficients[6].x),
                evaluateChannel(coefficients[1], coefficients[4], coefficients[6].y),
                evaluateChannel(coefficients[2], coefficients[5], coefficients[6].z));
}

namespace {
void buildSkyMapBoxMips(Vec4* texels, uint32_t width, uint32_t height, uint32_t mipCount,
                        const uint32_t* offsets) {
    for (uint32_t mip = 1; mip < mipCount; ++mip) {
        const uint32_t sourceWidth = std::max(1u, width >> (mip - 1u));
        const uint32_t sourceHeight = std::max(1u, height >> (mip - 1u));
        const uint32_t targetWidth = std::max(1u, width >> mip);
        const uint32_t targetHeight = std::max(1u, height >> mip);
        const uint32_t sourceOffset = offsets[mip - 1u];
        const uint32_t targetOffset = offsets[mip];
        for (uint32_t y = 0; y < targetHeight; ++y) {
            const uint32_t sourceY0 = std::min(y * 2u, sourceHeight - 1u);
            const uint32_t sourceY1 = std::min(y * 2u + 1u, sourceHeight - 1u);
            for (uint32_t x = 0; x < targetWidth; ++x) {
                const uint32_t sourceX0 = std::min(x * 2u, sourceWidth - 1u);
                const uint32_t sourceX1 = std::min(x * 2u + 1u, sourceWidth - 1u);
                const Vec4& c00 = texels[sourceOffset + static_cast<std::size_t>(sourceY0) * sourceWidth + sourceX0];
                const Vec4& c10 = texels[sourceOffset + static_cast<std::size_t>(sourceY0) * sourceWidth + sourceX1];
                const Vec4& c01 = texels[sourceOffset + static_cast<std::size_t>(sourceY1) * sourceWidth + sourceX0];
                const Vec4& c11 = texels[sourceOffset + static_cast<std::size_t>(sourceY1) * sourceWidth + sourceX1];
                texels[targetOffset + static_cast<std::size_t>(y) * targetWidth + x] =
                    Vec4((c00.x + c10.x + c01.x + c11.x) * 0.25f,
                         (c00.y + c10.y + c01.y + c11.y) * 0.25f,
                         (c00.z + c10.z + c01.z + c11.z) * 0.25f, 0.0f);
            }
        }
    }
}
} // namespace

AtmosphereSkyMap buildAtmosphereSkyMap(const AtmosphereLut& lut, const AtmosphereSettings& settings,
                                       const Vec3& sunDirection, float cameraAltitude) {
    AtmosphereSkyMap packed{};
    if (!lut.valid() || !settings.active()) {
        return packed;
    }

    const float sunLength = length(sunDirection);
    const Vec3 sun = sunLength > 1.0e-6f ? sunDirection * (1.0f / sunLength) : Vec3(0.0f, 1.0f, 0.0f);
    const Vec3 camera =
        atmosphereCameraPosition(settings, Vec3(0.0f, std::max(cameraAltitude, 0.0f), 0.0f));

    constexpr float tauExtent = 1.1f;
    for (uint32_t y = 0; y < kAtmosphereSkyMapHeight; ++y) {
        const float tauY =
            (static_cast<float>(y) + 0.5f) / static_cast<float>(kAtmosphereSkyMapHeight) * 2.0f * tauExtent -
            tauExtent;
        for (uint32_t x = 0; x < kAtmosphereSkyMapWidth; ++x) {
            const float tauX =
                (static_cast<float>(x) + 0.5f) / static_cast<float>(kAtmosphereSkyMapWidth) * 2.0f * tauExtent -
                tauExtent;
            const float tauSq = tauX * tauX + tauY * tauY;
            const float denominator = 1.0f + tauSq;
            Vec3 direction(-2.0f * tauX / denominator, (1.0f - tauSq) / denominator,
                           -2.0f * tauY / denominator);
            direction.y = std::max(direction.y, 0.0f);
            direction = normalize(direction);
            const Vec3 radiance = atmosphereLutSkyRadiance(lut, settings, camera, direction, sun);
            packed[static_cast<std::size_t>(y) * kAtmosphereSkyMapWidth + x] =
                Vec4(radiance.x, radiance.y, radiance.z, 0.0f);
        }
    }

    buildSkyMapBoxMips(packed.data(), kAtmosphereSkyMapWidth, kAtmosphereSkyMapHeight,
                       kAtmosphereSkyMapMipCount, kAtmosphereSkyMapMipOffsets.data());
    return packed;
}


AtmosphereSkyMapArray buildAtmosphereSkyMapArray(const AtmosphereLut& lut, const AtmosphereSettings& settings,
                                                 float cameraAltitude) {
    if (!lut.valid() || !settings.active()) {
        return {};
    }

    const Vec3 camera =
        atmosphereCameraPosition(settings, Vec3(0.0f, std::max(cameraAltitude, 0.0f), 0.0f));

    constexpr float tauExtent = 1.1f;
    constexpr float halfPi = 1.57079632679489661923f;
    AtmosphereSkyMapArray packed(kAtmosphereSkyMapLayerCount * kAtmosphereSkyMapLayerTexels);
    for (uint32_t layer = 0; layer < kAtmosphereSkyMapLayerCount; ++layer) {
        const float elevation =
            halfPi * (1.0f - static_cast<float>(layer) / static_cast<float>(kAtmosphereSkyMapLayerCount - 1u));
        const Vec3 layerSun(std::cos(elevation), std::sin(elevation), 0.0f);
        Vec4* base = packed.data() + static_cast<std::size_t>(layer) * kAtmosphereSkyMapLayerTexels;
        for (uint32_t y = 0; y < kAtmosphereSkyMapHeight; ++y) {
            const float tauY =
                (static_cast<float>(y) + 0.5f) / static_cast<float>(kAtmosphereSkyMapHeight) * 2.0f * tauExtent -
                tauExtent;
            for (uint32_t x = 0; x < kAtmosphereSkyMapWidth; ++x) {
                const float tauX = (static_cast<float>(x) + 0.5f) /
                                       static_cast<float>(kAtmosphereSkyMapWidth) * 2.0f * tauExtent -
                                   tauExtent;
                const float tauSq = tauX * tauX + tauY * tauY;
                const float denominator = 1.0f + tauSq;
                Vec3 direction(-2.0f * tauX / denominator, (1.0f - tauSq) / denominator,
                               -2.0f * tauY / denominator);
                direction.y = std::max(direction.y, 0.0f);
                direction = normalize(direction);
                const Vec3 radiance = atmosphereLutSkyRadiance(lut, settings, camera, direction, layerSun);
                base[static_cast<std::size_t>(y) * kAtmosphereSkyMapWidth + x] =
                    Vec4(radiance.x, radiance.y, radiance.z, 0.0f);
            }
        }
        buildSkyMapBoxMips(base, kAtmosphereSkyMapWidth, kAtmosphereSkyMapHeight,
                           kAtmosphereSkyMapArrayMipCount, kAtmosphereSkyMapArrayMipOffsets.data());
    }
    return packed;
}

AtmosphereSkyE buildAtmosphereSkyE(const AtmosphereLut& lut, const AtmosphereSettings& settings) {
    AtmosphereSkyE packed{};
    if (!lut.valid() || !settings.active()) {
        return packed;
    }

    const float groundRadius = settings.planetRadius;
    const float topRadius = settings.atmosphereRadius;
    for (uint32_t y = 0; y < kAtmosphereSkyEHeight; ++y) {
        const float uR = (static_cast<float>(y) + 0.5f) / static_cast<float>(kAtmosphereSkyEHeight);
        const float radius = groundRadius + uR * (topRadius - groundRadius);
        for (uint32_t x = 0; x < kAtmosphereSkyEWidth; ++x) {
            const float uMu = (static_cast<float>(x) + 0.5f) / static_cast<float>(kAtmosphereSkyEWidth);
            const float muSun = -0.2f + uMu * 1.2f;
            const Vec3 total = sampleAtmosphereLutIrradiance(lut, radius, muSun);
            Vec3 transmittance(0.0f, 0.0f, 0.0f);
            const float horizon = -std::sqrt(std::max(
                1.0f - (groundRadius * groundRadius) / (radius * radius), 0.0f));
            if (muSun >= horizon) {
                transmittance = sampleAtmosphereLutTransmittance(lut, radius, muSun);
            }
            const float directScale = std::max(muSun, 0.0f);
            const Vec3 sky = Vec3(std::max(total.x - transmittance.x * directScale, 0.0f),
                                  std::max(total.y - transmittance.y * directScale, 0.0f),
                                  std::max(total.z - transmittance.z * directScale, 0.0f)) *
                             settings.sunIntensity;
            packed[static_cast<std::size_t>(y) * kAtmosphereSkyEWidth + x] =
                Vec4(sky.x, sky.y, sky.z, 0.0f);
        }
    }
    return packed;
}

Vec3 atmosphereAerialPerspective(const AtmosphereSettings& settings, const Vec3& cameraPosition,
                                 const Vec3& worldPosition, const Vec3& sunDirection, uint32_t stepCount,
                                 Vec3* transmittance) {
    if (!settings.active()) {
        if (transmittance != nullptr) {
            *transmittance = Vec3(1.0f, 1.0f, 1.0f);
        }
        return Vec3(0.0f, 0.0f, 0.0f);
    }
    const Vec3 delta = worldPosition - cameraPosition;
    const float distance = length(delta);
    if (distance <= 1.0e-4f) {
        if (transmittance != nullptr) {
            *transmittance = Vec3(1.0f, 1.0f, 1.0f);
        }
        return Vec3(0.0f, 0.0f, 0.0f);
    }
    const Vec3 camera = Vec3(0.0f, std::max(cameraPosition.y, settings.planetRadius + 1.0f), 0.0f);
    return integrateScattering(settings, camera, delta * (1.0f / distance), sunDirection, distance, stepCount,
                               transmittance);
}

} // namespace hbg::render

