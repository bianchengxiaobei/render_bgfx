#include "asset/scene_loader.h"

#include "asset/gltf_loader.h"
#include "asset/texture_loader.h"
#include "core/log.h"
#include "render/shadows/shadow_settings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <exception>
#include <fstream>
#include <string>
#include <utility>

namespace hbg::asset {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

std::filesystem::path defaultSidecarPath(const std::filesystem::path& scenePath) {
    return scenePath.parent_path() / (scenePath.stem().string() + ".hdrp.json");
}

void applyFeature(Material& material, uint32_t feature, const nlohmann::json& features, const char* key) {
    const auto iterator = features.find(key);
    if (iterator == features.end() || !iterator->is_boolean()) {
        return;
    }
    if (iterator->get<bool>()) {
        material.features |= feature;
    } else {
        material.features &= ~feature;
    }
}

bool applyMaterialOverride(Material& material, const nlohmann::json& override, std::string* error) {
    if (!override.is_object()) {
        if (error != nullptr) {
            *error = "material override must be an object";
        }
        return false;
    }

    const auto type = override.find("materialType");
    if (type != override.end() && type->is_string()) {
        const std::string value = lower(type->get<std::string>());
        if (value == "lit") {
            material.type = MaterialType::Lit;
        } else if (value == "unlit") {
            material.type = MaterialType::Unlit;
        }
    }

    const auto renderQueue = override.find("renderQueue");
    if (renderQueue != override.end() && renderQueue->is_number_integer()) {
        material.renderQueue = renderQueue->get<int>();
    }

    const auto maskMap = override.find("maskMap");
    if (maskMap != override.end() && maskMap->is_string()) {
        material.maskMapPacked = lower(maskMap->get<std::string>()) != "separate";
    }

    const auto diffusionProfile = override.find("diffusionProfile");
    if (diffusionProfile != override.end() && diffusionProfile->is_string()) {
        material.diffusionProfile = diffusionProfile->get<std::string>();
    }

    const auto features = override.find("features");
    if (features != override.end()) {
        if (!features->is_object()) {
            if (error != nullptr) {
                *error = "features must be an object";
            }
            return false;
        }
        applyFeature(material, MaterialFeatureClearCoat, *features, "clearCoat");
        applyFeature(material, MaterialFeatureTransmission, *features, "transmission");
        applyFeature(material, MaterialFeatureIor, *features, "ior");
        applyFeature(material, MaterialFeatureIridescence, *features, "iridescence");
        applyFeature(material, MaterialFeatureAnisotropy, *features, "anisotropy");
    }

    return true;
}

float readFloat(const nlohmann::json& object, const char* key, float fallback) {
    const auto iterator = object.find(key);
    return iterator != object.end() && iterator->is_number() ? iterator->get<float>() : fallback;
}

bool readBool(const nlohmann::json& object, const char* key, bool& value, std::string* error) {
    const auto iterator = object.find(key);
    if (iterator == object.end()) {
        return true;
    }
    if (!iterator->is_boolean()) {
        if (error != nullptr) {
            *error = std::string(key) + " must be a boolean";
        }
        return false;
    }
    value = iterator->get<bool>();
    return true;
}

bool readNumber(const nlohmann::json& object, const char* key, float& value, std::string* error) {
    const auto iterator = object.find(key);
    if (iterator == object.end()) {
        return true;
    }
    if (!iterator->is_number()) {
        if (error != nullptr) {
            *error = std::string(key) + " must be a number";
        }
        return false;
    }
    value = iterator->get<float>();
    return true;
}

bool readUint(const nlohmann::json& object, const char* key, uint32_t& value, std::string* error) {
    const auto iterator = object.find(key);
    if (iterator == object.end()) {
        return true;
    }
    if (!iterator->is_number_integer() || iterator->get<int>() < 0) {
        if (error != nullptr) {
            *error = std::string(key) + " must be a non-negative integer";
        }
        return false;
    }
    value = static_cast<uint32_t>(iterator->get<int>());
    return true;
}

bool readVec3(const nlohmann::json& object, const char* key, Vec3& value, std::string* error) {
    const auto iterator = object.find(key);
    if (iterator == object.end()) {
        return true;
    }
    if (!iterator->is_array() || iterator->size() != 3) {
        if (error != nullptr) {
            *error = std::string(key) + " must be an array of three numbers";
        }
        return false;
    }
    for (const auto& component : *iterator) {
        if (!component.is_number()) {
            if (error != nullptr) {
                *error = std::string(key) + " must be an array of three numbers";
            }
            return false;
        }
    }
    value = Vec3((*iterator)[0].get<float>(), (*iterator)[1].get<float>(), (*iterator)[2].get<float>());
    return true;
}

bool readVec4(const nlohmann::json& object, const char* key, Vec4& value, std::string* error) {
    const auto iterator = object.find(key);
    if (iterator == object.end()) {
        return true;
    }
    if (!iterator->is_array() || iterator->size() != 4) {
        if (error != nullptr) {
            *error = std::string(key) + " must be an array of four numbers";
        }
        return false;
    }
    for (const auto& component : *iterator) {
        if (!component.is_number()) {
            if (error != nullptr) {
                *error = std::string(key) + " must be an array of four numbers";
            }
            return false;
        }
    }
    value = Vec4((*iterator)[0].get<float>(), (*iterator)[1].get<float>(), (*iterator)[2].get<float>(),
                 (*iterator)[3].get<float>());
    return true;
}

bool loadSidecarTexture(render::RenderScene& scene, const std::filesystem::path& baseDirectory,
                        const nlohmann::json& object, const char* key, int& imageIndex, const char* owner,
                        std::string* error) {
    imageIndex = -1;
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->is_null()) {
        return true;
    }
    if (!iterator->is_string()) {
        if (error != nullptr) {
            *error = std::string(owner) + " " + key + " must be a string path";
        }
        return false;
    }
    const std::string path = iterator->get<std::string>();
    if (path.empty()) {
        return true;
    }

    asset::ImageAsset image;
    const std::filesystem::path resolved = baseDirectory / path;
    std::string textureError;
    if (!asset::TextureLoader::loadFromFile(resolved, image, &textureError)) {
        if (error != nullptr) {
            *error = "failed to load " + std::string(owner) + " " + key + " '" + resolved.string() +
                     "': " + textureError;
        }
        return false;
    }
    imageIndex = static_cast<int>(scene.images.size());
    scene.images.push_back(std::move(image));
    return true;
}

bool applyDecal(render::RenderScene& scene, const nlohmann::json& object, const std::filesystem::path& baseDirectory,
                std::size_t index, std::string* error) {
    if (!object.is_object()) {
        if (error != nullptr) {
            *error = "decals[" + std::to_string(index) + "] must be an object";
        }
        return false;
    }

    render::Decal decal;
    decal.name = "Decal " + std::to_string(index);
    const auto name = object.find("name");
    if (name != object.end() && name->is_string()) {
        decal.name = name->get<std::string>();
    }

    if (!loadSidecarTexture(scene, baseDirectory, object, "texture", decal.albedoImage, "decal", error) ||
        !loadSidecarTexture(scene, baseDirectory, object, "normalTexture", decal.normalImage, "decal", error) ||
        !loadSidecarTexture(scene, baseDirectory, object, "maskTexture", decal.maskImage, "decal", error)) {
        return false;
    }
    if (!readVec3(object, "position", decal.position, error) ||
        !readVec3(object, "rotation", decal.rotation, error) ||
        !readVec3(object, "size", decal.size, error) ||
        !readVec4(object, "baseColor", decal.baseColor, error)) {
        return false;
    }

    const auto enabled = object.find("enabled");
    if (enabled != object.end() && enabled->is_boolean()) {
        decal.enabled = enabled->get<bool>();
    }

    decal.opacity = std::clamp(readFloat(object, "opacity", decal.opacity), 0.0f, 1.0f);
    decal.angleFade = std::clamp(readFloat(object, "angleFade", decal.angleFade), 0.0f, 180.0f);
    decal.normalBlend = std::clamp(readFloat(object, "normalBlend", decal.normalBlend), 0.0f, 1.0f);
    decal.metallic = std::clamp(readFloat(object, "metallic", decal.metallic), 0.0f, 1.0f);
    decal.ambientOcclusion = std::clamp(readFloat(object, "ao", decal.ambientOcclusion), 0.0f, 1.0f);
    decal.roughness = std::clamp(readFloat(object, "roughness", decal.roughness), 0.0f, 1.0f);
    decal.size = Vec3(std::max(std::abs(decal.size.x), 1.0e-3f), std::max(std::abs(decal.size.y), 1.0e-3f),
                      std::max(std::abs(decal.size.z), 1.0e-3f));

    scene.decals.push_back(std::move(decal));
    return true;
}

bool applyProbe(render::RenderScene& scene, const nlohmann::json& object, const std::filesystem::path& baseDirectory,
                std::size_t index, std::string* error) {
    if (!object.is_object()) {
        if (error != nullptr) {
            *error = "probes[" + std::to_string(index) + "] must be an object";
        }
        return false;
    }

    render::ReflectionProbe probe;
    probe.name = "Probe " + std::to_string(index);
    const auto name = object.find("name");
    if (name != object.end() && name->is_string()) {
        probe.name = name->get<std::string>();
    }

    if (!loadSidecarTexture(scene, baseDirectory, object, "texture", probe.image, "probe", error)) {
        return false;
    }
    if (probe.image < 0) {
        if (error != nullptr) {
            *error = "probes[" + std::to_string(index) + "] requires a texture";
        }
        return false;
    }
    if (!readVec3(object, "position", probe.position, error) ||
        !readVec3(object, "rotation", probe.rotation, error) ||
        !readVec3(object, "boxExtents", probe.boxExtents, error)) {
        return false;
    }

    const auto enabled = object.find("enabled");
    if (enabled != object.end() && enabled->is_boolean()) {
        probe.enabled = enabled->get<bool>();
    }

    probe.blendDistance = std::max(readFloat(object, "blendDistance", probe.blendDistance), 0.0f);
    probe.intensity = std::max(readFloat(object, "intensity", probe.intensity), 0.0f);
    probe.boxExtents = Vec3(std::max(std::abs(probe.boxExtents.x), 1.0e-3f),
                            std::max(std::abs(probe.boxExtents.y), 1.0e-3f),
                            std::max(std::abs(probe.boxExtents.z), 1.0e-3f));

    scene.probes.push_back(std::move(probe));
    return true;
}

bool applyLocalFogVolume(render::RenderScene& scene, const nlohmann::json& object,
                         const std::filesystem::path& baseDirectory, std::size_t index, std::string* error) {
    if (!object.is_object()) {
        if (error != nullptr) {
            *error = "localFogVolumes[" + std::to_string(index) + "] must be an object";
        }
        return false;
    }

    render::LocalFogVolume volume;
    volume.name = "Local Fog " + std::to_string(index);
    const auto name = object.find("name");
    if (name != object.end() && name->is_string()) {
        volume.name = name->get<std::string>();
    }

    if (!loadSidecarTexture(scene, baseDirectory, object, "maskTexture", volume.maskImage, "local fog",
                            error) ||
        !readVec3(object, "position", volume.center, error) ||
        !readVec3(object, "rotation", volume.rotation, error) ||
        !readVec3(object, "size", volume.size, error) ||
        !readVec3(object, "albedo", volume.albedo, error) ||
        !readVec3(object, "positiveFade", volume.positiveFade, error) ||
        !readVec3(object, "negativeFade", volume.negativeFade, error) ||
        !readVec3(object, "maskTiling", volume.maskTiling, error) ||
        !readVec3(object, "maskScroll", volume.maskScroll, error)) {
        return false;
    }

    const auto shape = object.find("shape");
    if (shape != object.end()) {
        if (!shape->is_string()) {
            if (error != nullptr) {
                *error = "localFogVolumes[" + std::to_string(index) + "].shape must be a string";
            }
            return false;
        }
        const std::string value = lower(shape->get<std::string>());
        if (value == "box" || value == "obb") {
            volume.shape = render::LocalFogShape::Box;
        } else if (value == "sphere" || value == "ellipsoid") {
            volume.shape = render::LocalFogShape::Sphere;
        } else {
            if (error != nullptr) {
                *error = "localFogVolumes[" + std::to_string(index) + "].shape must be box or sphere";
            }
            return false;
        }
    }

    const auto falloff = object.find("falloffMode");
    if (falloff != object.end()) {
        if (!falloff->is_string()) {
            if (error != nullptr) {
                *error = "localFogVolumes[" + std::to_string(index) + "].falloffMode must be a string";
            }
            return false;
        }
        const std::string value = lower(falloff->get<std::string>());
        if (value == "linear") {
            volume.falloff = render::LocalFogFalloff::Linear;
        } else if (value == "exponential") {
            volume.falloff = render::LocalFogFalloff::Exponential;
        } else {
            if (error != nullptr) {
                *error = "localFogVolumes[" + std::to_string(index) +
                         "].falloffMode must be linear or exponential";
            }
            return false;
        }
    }

    const auto maskPlane = object.find("maskPlane");
    if (maskPlane != object.end()) {
        if (!maskPlane->is_string()) {
            if (error != nullptr) {
                *error = "localFogVolumes[" + std::to_string(index) + "].maskPlane must be a string";
            }
            return false;
        }
        const std::string value = lower(maskPlane->get<std::string>());
        if (value == "xy") {
            volume.maskPlane = render::LocalFogMaskPlane::XY;
        } else if (value == "xz") {
            volume.maskPlane = render::LocalFogMaskPlane::XZ;
        } else if (value == "yz") {
            volume.maskPlane = render::LocalFogMaskPlane::YZ;
        } else {
            if (error != nullptr) {
                *error = "localFogVolumes[" + std::to_string(index) + "].maskPlane must be xy, xz or yz";
            }
            return false;
        }
    }

    if (!readBool(object, "enabled", volume.enabled, error) ||
        !readBool(object, "invertFade", volume.invertFade, error) ||
        !readBool(object, "maskInvert", volume.maskInvert, error)) {
        return false;
    }

    volume.meanFreePath = readFloat(object, "meanFreePath", volume.meanFreePath);
    volume.anisotropy = readFloat(object, "anisotropy", volume.anisotropy);
    volume.distanceFadeStart = readFloat(object, "distanceFadeStart", volume.distanceFadeStart);
    volume.distanceFadeEnd = readFloat(object, "distanceFadeEnd", volume.distanceFadeEnd);
    volume.maskIntensity = readFloat(object, "maskIntensity", volume.maskIntensity);
    render::sanitizeLocalFogVolume(volume);

    scene.localFogVolumes.push_back(std::move(volume));
    return true;
}

bool applyLightShadow(render::RenderScene& scene, const nlohmann::json& object, std::size_t index,
                      std::string* error) {
    if (!object.is_object()) {
        if (error != nullptr) {
            *error = "lights[" + std::to_string(index) + "] must be an object";
        }
        return false;
    }

    const auto shadow = object.find("shadow");
    if (shadow == object.end()) {
        return true;
    }
    if (!shadow->is_object()) {
        if (error != nullptr) {
            *error = "lights[" + std::to_string(index) + "].shadow must be an object";
        }
        return false;
    }

    render::Light* target = nullptr;
    const auto name = object.find("name");
    if (name != object.end()) {
        if (!name->is_string()) {
            if (error != nullptr) {
                *error = "lights[" + std::to_string(index) + "].name must be a string";
            }
            return false;
        }
        const std::string requested = name->get<std::string>();
        for (render::Light& light : scene.lights) {
            if (light.name == requested) {
                target = &light;
                break;
            }
        }
        if (target == nullptr) {
            if (error != nullptr) {
                *error = "lights[" + std::to_string(index) + "] references unknown light '" + requested + "'";
            }
            return false;
        }
    } else if (index < scene.lights.size()) {
        target = &scene.lights[index];
    } else {
        if (error != nullptr) {
            *error = "lights[" + std::to_string(index) + "] has no name and no matching light index";
        }
        return false;
    }

    render::LightShadowSettings settings;
    if (!readNumber(*shadow, "dimmer", settings.dimmer, error) ||
        !readNumber(*shadow, "fadeDistance", settings.fadeDistance, error) ||
        !readNumber(*shadow, "nearPlane", settings.nearPlane, error) ||
        !readUint(*shadow, "resolution", settings.resolution, error) ||
        !readVec3(*shadow, "tint", settings.tint, error) ||
        !readBool(*shadow, "penumbraTint", settings.penumbraTint, error) ||
        !readBool(*shadow, "contactShadows", settings.contactShadows, error) ||
        !readNumber(*shadow, "softness", settings.softness, error) ||
        !readUint(*shadow, "blockerSamples", settings.blockerSamples, error) ||
        !readUint(*shadow, "filterSamples", settings.filterSamples, error)) {
        return false;
    }

    const auto updateMode = shadow->find("updateMode");
    if (updateMode != shadow->end()) {
        if (!updateMode->is_string() ||
            !render::parseShadowUpdateMode(updateMode->get<std::string>(), settings.updateMode)) {
            if (error != nullptr) {
                *error = "lights[" + std::to_string(index) +
                         "].shadow.updateMode must be everyFrame, onEnable or onDemand";
            }
            return false;
        }
    }

    const uint32_t requestedResolution = settings.resolution;
    render::sanitizeLightShadowSettings(settings);
    if (requestedResolution != 0 && settings.resolution != requestedResolution) {
        HBG_LOGW("light '{}': unsupported shadow resolution {}; using {}", target->name, requestedResolution,
                 settings.resolution);
    }
    target->shadowConfigured = true;
    target->shadow = settings;
    return true;
}

bool applySidecar(render::RenderScene& scene, const std::filesystem::path& sidecar,
                  const std::filesystem::path& baseDirectory, std::string* error) {
    std::ifstream file(sidecar);
    if (!file.good()) {
        if (error != nullptr) {
            *error = "failed to open sidecar: " + sidecar.string();
        }
        return false;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = "failed to parse sidecar: " + std::string(exception.what());
        }
        return false;
    }

    if (!root.is_object()) {
        if (error != nullptr) {
            *error = "sidecar root must be an object";
        }
        return false;
    }

    const auto global = root.find("hdrp");
    if (global != root.end()) {
        for (Material& material : scene.materials) {
            if (!applyMaterialOverride(material, *global, error)) {
                return false;
            }
        }
    }

    const auto materials = root.find("materials");
    if (materials != root.end()) {
        if (!materials->is_object()) {
            if (error != nullptr) {
                *error = "materials must be an object keyed by material name";
            }
            return false;
        }
        for (const auto& [name, override] : materials->items()) {
            for (Material& material : scene.materials) {
                if (material.name == name) {
                    if (!applyMaterialOverride(material, override, error)) {
                        return false;
                    }
                }
            }
        }
    }

    const auto lights = root.find("lights");
    if (lights != root.end()) {
        if (!lights->is_array()) {
            if (error != nullptr) {
                *error = "lights must be an array";
            }
            return false;
        }
        std::size_t index = 0;
        for (const auto& light : *lights) {
            if (!applyLightShadow(scene, light, index, error)) {
                return false;
            }
            ++index;
        }
    }

    const auto decals = root.find("decals");
    if (decals != root.end()) {
        if (!decals->is_array()) {
            if (error != nullptr) {
                *error = "decals must be an array";
            }
            return false;
        }
        std::size_t index = 0;
        for (const auto& decal : *decals) {
            if (!applyDecal(scene, decal, baseDirectory, index, error)) {
                return false;
            }
            ++index;
        }
    }

    const auto probes = root.find("probes");
    if (probes != root.end()) {
        if (!probes->is_array()) {
            if (error != nullptr) {
                *error = "probes must be an array";
            }
            return false;
        }
        std::size_t index = 0;
        for (const auto& probe : *probes) {
            if (!applyProbe(scene, probe, baseDirectory, index, error)) {
                return false;
            }
            ++index;
        }
    }

    const auto localFogVolumes = root.find("localFogVolumes");
    if (localFogVolumes != root.end()) {
        if (!localFogVolumes->is_array()) {
            if (error != nullptr) {
                *error = "localFogVolumes must be an array";
            }
            return false;
        }
        std::size_t index = 0;
        for (const auto& volume : *localFogVolumes) {
            if (!applyLocalFogVolume(scene, volume, baseDirectory, index, error)) {
                return false;
            }
            ++index;
        }
    }

    return true;
}

} // namespace

SceneLoadResult SceneLoader::load(const std::filesystem::path& path, const SceneLoadOptions& options) {
    GltfLoadResult gltf = GltfLoader::load(path);
    SceneLoadResult result;
    if (!gltf.ok()) {
        result.error = std::move(gltf.error);
        return result;
    }
    result.scene = std::move(gltf.scene);

    if (options.loadSidecar) {
        const std::filesystem::path sidecar =
            options.sidecar.empty() ? defaultSidecarPath(path) : options.sidecar;
        if (std::filesystem::exists(sidecar)) {
            std::string error;
            if (!applySidecar(*result.scene, sidecar, sidecar.parent_path(), &error)) {
                result.scene.reset();
                result.error = error;
                return result;
            }
            HBG_LOGI("loaded scene sidecar: {}", sidecar.string());
        }
    }

    return result;
}

} // namespace hbg::asset
