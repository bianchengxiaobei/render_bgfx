#include "asset/scene_loader.h"

#include "asset/gltf_loader.h"
#include "asset/texture_loader.h"
#include "core/log.h"
#include "render/lighting/gpu_light_data.h"
#include "render/shadows/shadow_settings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <exception>
#include <fstream>
#include <sstream>
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
        applyFeature(material, MaterialFeatureSubsurface, *features, "subsurfaceScattering");
    }

    const auto readFeatureFloat = [&](const char* key, float& target) {
        const auto iterator = override.find(key);
        if (iterator != override.end() && iterator->is_number()) {
            target = iterator->get<float>();
        }
    };
    readFeatureFloat("clearCoat", material.clearCoat);
    readFeatureFloat("clearCoatRoughness", material.clearCoatRoughness);
    readFeatureFloat("clearCoatNormalScale", material.clearCoatNormalScale);
    readFeatureFloat("transmission", material.transmission);
    readFeatureFloat("thickness", material.thickness);
    readFeatureFloat("ior", material.ior);
    readFeatureFloat("iridescence", material.iridescence);
    readFeatureFloat("iridescenceIor", material.iridescenceIor);
    readFeatureFloat("iridescenceThickness", material.iridescenceThickness);
    readFeatureFloat("anisotropy", material.anisotropy);
    readFeatureFloat("anisotropyRotation", material.anisotropyRotation);
    readFeatureFloat("subsurfaceMask", material.subsurfaceMask);

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

bool readVec2(const nlohmann::json& object, const char* key, Vec2& value, std::string* error) {
    const auto iterator = object.find(key);
    if (iterator == object.end()) {
        return true;
    }
    if (!iterator->is_array() || iterator->size() != 2) {
        if (error != nullptr) {
            *error = std::string(key) + " must be an array of two numbers";
        }
        return false;
    }
    for (const auto& component : *iterator) {
        if (!component.is_number()) {
            if (error != nullptr) {
                *error = std::string(key) + " must be an array of two numbers";
            }
            return false;
        }
    }
    value = Vec2((*iterator)[0].get<float>(), (*iterator)[1].get<float>());
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
    uint32_t layerMask = decal.layerMask;
    uint32_t channelMask = decal.channelMask;
    if (!readUint(object, "layerMask", layerMask, error) ||
        !readUint(object, "channelMask", channelMask, error)) {
        return false;
    }
    decal.layerMask = std::min<uint32_t>(layerMask, 0x7fu);
    decal.channelMask = std::min<uint32_t>(channelMask, 0x1fu);
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

bool applyLightLayer(render::RenderScene& scene, const nlohmann::json& object, std::size_t index,
                     std::string* error) {
    if (!object.is_object()) {
        if (error != nullptr) {
            *error = "lights[" + std::to_string(index) + "] must be an object";
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
    } else if (index < scene.lights.size()) {
        target = &scene.lights[index];
    }
    if (target == nullptr) {
        if (error != nullptr) {
            *error = "lights[" + std::to_string(index) + "] does not reference a light";
        }
        return false;
    }

    uint32_t mask = target->lightLayerMask;
    if (!readUint(object, "lightLayerMask", mask, error)) {
        return false;
    }
    target->lightLayerMask = static_cast<uint16_t>(std::min<uint32_t>(mask, 0xffffu));
    return true;
}

bool applyLightShadow(render::RenderScene& scene, const nlohmann::json& object, std::size_t index,
                      std::string* error) {
    if (!applyLightLayer(scene, object, index, error)) {
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

bool readVfxPropertyValue(const nlohmann::json& item, render::VfxPropertyValue& property, std::string* error) {
    const auto name = item.find("name");
    if (name != item.end()) {
        if (!name->is_string() || name->get<std::string>().empty()) {
            if (error != nullptr) {
                *error = "vfx property name must be a non-empty string";
            }
            return false;
        }
        property.name = name->get<std::string>();
    }
    if (property.name.empty()) {
        if (error != nullptr) {
            *error = "vfx property requires a name";
        }
        return false;
    }

    std::string type = "float";
    const auto typeIt = item.find("type");
    if (typeIt != item.end()) {
        if (!typeIt->is_string()) {
            if (error != nullptr) {
                *error = "vfx property type must be a string";
            }
            return false;
        }
        type = lower(typeIt->get<std::string>());
    }

    const auto value = item.find("value");
    if (value == item.end()) {
        if (error != nullptr) {
            *error = "vfx property value is required";
        }
        return false;
    }

    if (type == "float") {
        if (!value->is_number()) {
            if (error != nullptr) {
                *error = "vfx float property value must be a number";
            }
            return false;
        }
        property.kind = render::VfxPropertyKind::Float;
        property.values[0] = value->get<float>();
        return true;
    }

    std::size_t components = 0;
    if (type == "vector2") {
        property.kind = render::VfxPropertyKind::Vector2;
        components = 2;
    } else if (type == "vector3") {
        property.kind = render::VfxPropertyKind::Vector3;
        components = 3;
    } else if (type == "vector4") {
        property.kind = render::VfxPropertyKind::Vector4;
        components = 4;
    } else if (type == "color") {
        property.kind = render::VfxPropertyKind::Color;
        components = 4;
    } else {
        if (error != nullptr) {
            *error = "unknown vfx property type '" + type + "'";
        }
        return false;
    }
    if (!value->is_array() || value->size() != components) {
        if (error != nullptr) {
            *error = "vfx " + type + " property value is invalid";
        }
        return false;
    }
    for (std::size_t component = 0; component < components; ++component) {
        if (!(*value)[component].is_number()) {
            if (error != nullptr) {
                *error = "vfx " + type + " property value must contain numbers";
            }
            return false;
        }
        property.values[component] = (*value)[component].get<float>();
    }
    return true;
}

bool applyVfxInstance(render::RenderScene& scene, const nlohmann::json& object,
                      const std::filesystem::path& baseDirectory, std::size_t index, std::string* error) {
    if (!object.is_object()) {
        if (error != nullptr) {
            *error = "vfx[" + std::to_string(index) + "] must be an object";
        }
        return false;
    }

    render::VfxInstance instance;
    instance.name = "VFX " + std::to_string(index);
    const auto name = object.find("name");
    if (name != object.end()) {
        if (!name->is_string()) {
            if (error != nullptr) {
                *error = "vfx[" + std::to_string(index) + "].name must be a string";
            }
            return false;
        }
        instance.name = name->get<std::string>();
    }

    const auto asset = object.find("asset");
    if (asset == object.end() || !asset->is_string() || asset->get<std::string>().empty()) {
        if (error != nullptr) {
            *error = "vfx[" + std::to_string(index) + "].asset must be a non-empty string";
        }
        return false;
    }
    instance.asset = baseDirectory / asset->get<std::string>();

    if (!readVec3(object, "position", instance.position, error) ||
        !readVec3(object, "rotation", instance.rotation, error) ||
        !readVec3(object, "scale", instance.scale, error) ||
        !readBool(object, "enabled", instance.enabled, error) ||
        !readBool(object, "playOnAwake", instance.playOnAwake, error) ||
        !readNumber(object, "playRate", instance.playRate, error) ||
        !readUint(object, "seed", instance.randomSeed, error)) {
        return false;
    }
    const auto play = object.find("play");
    if (play != object.end()) {
        if (!play->is_boolean()) {
            if (error != nullptr) {
                *error = "vfx[" + std::to_string(index) + "].play must be a boolean";
            }
            return false;
        }
        instance.playOnAwake = play->get<bool>();
    }
    instance.playRate = std::max(instance.playRate, 0.0f);
    instance.scale = Vec3(instance.scale.x != 0.0f ? instance.scale.x : 1.0f,
                          instance.scale.y != 0.0f ? instance.scale.y : 1.0f,
                          instance.scale.z != 0.0f ? instance.scale.z : 1.0f);

    const auto properties = object.find("properties");
    if (properties != object.end()) {
        if (properties->is_array()) {
            std::size_t propertyIndex = 0;
            for (const auto& item : *properties) {
                render::VfxPropertyValue property;
                if (!item.is_object() || !readVfxPropertyValue(item, property, error)) {
                    if (error != nullptr && error->empty()) {
                        *error = "vfx[" + std::to_string(index) + "].properties[" +
                                 std::to_string(propertyIndex) + "] is invalid";
                    }
                    return false;
                }
                instance.properties.push_back(std::move(property));
                ++propertyIndex;
            }
        } else if (properties->is_object()) {
            for (const auto& [propertyName, value] : properties->items()) {
                render::VfxPropertyValue property;
                property.name = propertyName;
                if (!readVfxPropertyValue(value, property, error)) {
                    return false;
                }
                instance.properties.push_back(std::move(property));
            }
        } else {
            if (error != nullptr) {
                *error = "vfx[" + std::to_string(index) + "].properties must be an array or object";
            }
            return false;
        }
    }

    scene.vfxInstances.push_back(std::move(instance));
    return true;
}

bool applyObjectLayer(render::RenderScene& scene, const nlohmann::json& object, std::size_t index,
                      std::string* error) {
    if (!object.is_object()) {
        if (error != nullptr) {
            *error = "objects[" + std::to_string(index) + "] must be an object";
        }
        return false;
    }

    render::RenderObject* target = nullptr;
    const auto name = object.find("name");
    if (name != object.end()) {
        if (!name->is_string()) {
            if (error != nullptr) {
                *error = "objects[" + std::to_string(index) + "].name must be a string";
            }
            return false;
        }
        const std::string requested = name->get<std::string>();
        for (render::RenderObject& mesh : scene.objects) {
            if (mesh.name == requested) {
                target = &mesh;
                break;
            }
        }
    } else if (index < scene.objects.size()) {
        target = &scene.objects[index];
    }
    if (target == nullptr) {
        if (error != nullptr) {
            *error = "objects[" + std::to_string(index) + "] does not reference a render object";
        }
        return false;
    }

    uint32_t mask = target->lightLayerMask;
    if (!readUint(object, "lightLayerMask", mask, error)) {
        return false;
    }
    target->lightLayerMask = static_cast<uint16_t>(std::min<uint32_t>(mask, 0xffffu));

    uint32_t decalMask = target->decalLayerMask;
    if (!readUint(object, "decalLayerMask", decalMask, error)) {
        return false;
    }
    target->decalLayerMask = static_cast<uint8_t>(std::min<uint32_t>(decalMask, 0x7fu));
    return true;
}

bool applyLightmap(render::RenderScene& scene, const nlohmann::json& lightmap,
                   const std::filesystem::path& baseDirectory, std::string* error) {
    const auto fail = [error](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    const auto readValueUint = [](const nlohmann::json& value, uint32_t& target) {
        if (value.is_number_unsigned()) {
            const uint64_t number = value.get<uint64_t>();
            if (number <= UINT32_MAX) {
                target = static_cast<uint32_t>(number);
                return true;
            }
        } else if (value.is_number_integer()) {
            const int64_t number = value.get<int64_t>();
            if (number >= 0 && number <= UINT32_MAX) {
                target = static_cast<uint32_t>(number);
                return true;
            }
        }
        return false;
    };

    if (!lightmap.is_object()) {
        return fail("lightmap must be an object");
    }

    const auto atlases = lightmap.find("atlases");
    if (atlases == lightmap.end() || !atlases->is_array() || atlases->empty()) {
        return fail("lightmap.atlases must be a non-empty array");
    }

    scene.lightmapAtlases.clear();
    scene.lightmapBaseDirectory = baseDirectory;
    scene.lightmapAtlases.reserve(atlases->size());
    for (std::size_t index = 0; index < atlases->size(); ++index) {
        const nlohmann::json& atlas = atlases->at(index);
        if (!atlas.is_object()) {
            return fail("lightmap.atlases[" + std::to_string(index) + "] must be an object");
        }

        render::LightmapAtlasSource source;
        const auto file = atlas.find("file");
        if (file == atlas.end() || !file->is_string() || file->get<std::string>().empty()) {
            return fail("lightmap.atlases[" + std::to_string(index) + "].file must be a non-empty string");
        }
        source.file = file->get<std::string>();

        const auto size = atlas.find("size");
        if (size == atlas.end() || !readValueUint(*size, source.size) || source.size == 0) {
            return fail("lightmap.atlases[" + std::to_string(index) + "].size must be a positive integer");
        }

        const auto layers = atlas.find("layers");
        if (layers != atlas.end() && (!readValueUint(*layers, source.layers) || source.layers == 0)) {
            return fail("lightmap.atlases[" + std::to_string(index) + "].layers must be a positive integer");
        }

        const auto format = atlas.find("format");
        if (format != atlas.end()) {
            if (!format->is_string()) {
                return fail("lightmap.atlases[" + std::to_string(index) + "].format must be a string");
            }
            source.format = lower(format->get<std::string>());
        }
        if (source.format != "rgba16f") {
            return fail("lightmap.atlases[" + std::to_string(index) + "].format must be rgba16f");
        }
        scene.lightmapAtlases.push_back(std::move(source));
    }

    const auto objects = lightmap.find("objects");
    if (objects == lightmap.end() || !objects->is_object()) {
        return fail("lightmap.objects must be an object keyed by render object name");
    }

    for (const auto& [name, entry] : objects->items()) {
        if (name.empty() || !entry.is_object()) {
            return fail("lightmap.objects entries must be named objects");
        }

        render::RenderObject* target = nullptr;
        for (render::RenderObject& object : scene.objects) {
            if (object.name == name) {
                target = &object;
                break;
            }
        }
        if (target == nullptr) {
            return fail("lightmap object '" + name + "' does not match a render object");
        }

        uint32_t atlasIndex = 0;
        const auto atlas = entry.find("atlas");
        if (atlas == entry.end() || !readValueUint(*atlas, atlasIndex) || atlasIndex >= scene.lightmapAtlases.size()) {
            return fail("lightmap object '" + name + "' references an invalid atlas");
        }

        const auto rect = entry.find("rect");
        if (rect == entry.end() || !rect->is_array() || rect->size() != 4) {
            return fail("lightmap object '" + name + "' rect must contain four integers");
        }
        uint32_t values[4] = {};
        for (std::size_t component = 0; component < 4; ++component) {
            if (!readValueUint(rect->at(component), values[component])) {
                return fail("lightmap object '" + name + "' rect must contain four integers");
            }
        }
        const render::LightmapAtlasSource& atlasSource = scene.lightmapAtlases[atlasIndex];
        if (values[2] == 0 || values[3] == 0 || static_cast<uint64_t>(values[0]) + values[2] > atlasSource.size ||
            static_cast<uint64_t>(values[1]) + values[3] > atlasSource.size) {
            return fail("lightmap object '" + name + "' rect is outside its atlas");
        }

        bool contribute = true;
        const auto contributeGI = entry.find("contributeGI");
        if (contributeGI != entry.end()) {
            if (!contributeGI->is_boolean()) {
                return fail("lightmap object '" + name + "' contributeGI must be a boolean");
            }
            contribute = contributeGI->get<bool>();
        }
        if (!contribute) {
            continue;
        }

        Vec4 st(static_cast<float>(values[2]) / static_cast<float>(atlasSource.size),
                static_cast<float>(values[3]) / static_cast<float>(atlasSource.size),
                static_cast<float>(values[0]) / static_cast<float>(atlasSource.size),
                static_cast<float>(values[1]) / static_cast<float>(atlasSource.size));
        const auto stJson = entry.find("st");
        if (stJson != entry.end()) {
            if (!stJson->is_array() || stJson->size() != 4) {
                return fail("lightmap object '" + name + "' st must contain four numbers");
            }
            for (std::size_t component = 0; component < 4; ++component) {
                if (!stJson->at(component).is_number()) {
                    return fail("lightmap object '" + name + "' st must contain four numbers");
                }
            }
            st = Vec4(stJson->at(0).get<float>(), stJson->at(1).get<float>(), stJson->at(2).get<float>(),
                      stJson->at(3).get<float>());
            if (!std::isfinite(st.x) || !std::isfinite(st.y) || !std::isfinite(st.z) || !std::isfinite(st.w)) {
                return fail("lightmap object '" + name + "' st must be finite");
            }
        }

        target->lightmapAtlas = atlasIndex;
        target->lightmapST = st;
        target->lightmapSize = Vec2(static_cast<float>(values[2]), static_cast<float>(values[3]));
    }

    return true;
}

render::Light* resolveLightTarget(render::RenderScene& scene, const nlohmann::json& object, std::size_t index) {
    const auto name = object.find("name");
    if (name != object.end()) {
        if (!name->is_string()) {
            return nullptr;
        }
        const std::string requested = name->get<std::string>();
        for (render::Light& light : scene.lights) {
            if (light.name == requested) {
                return &light;
            }
        }
        return nullptr;
    }
    return index < scene.lights.size() ? &scene.lights[index] : nullptr;
}

bool applyLightArea(render::RenderScene& scene, const nlohmann::json& object, std::size_t index,
                    std::string* error) {
    render::Light* target = resolveLightTarget(scene, object, index);
    if (target == nullptr) {
        return true;
    }

    const auto type = object.find("type");
    if (type == object.end() || !type->is_string() || lower(type->get<std::string>()) != "area") {
        return true;
    }

    target->type = render::LightType::Area;
    target->castShadows = false;
    target->areaShape = render::LightAreaShape::Rectangle;
    const bool areaShadowRequested = object.find("shadow") != object.end();

    const auto shape = object.find("shape");
    if (shape != object.end()) {
        if (!shape->is_string()) {
            if (error != nullptr) {
                *error = "lights[" + std::to_string(index) + "].shape must be a string";
            }
            return false;
        }
        const std::string value = lower(shape->get<std::string>());
        if (value == "rectangle") {
            target->areaShape = render::LightAreaShape::Rectangle;
        } else if (value == "tube") {
            target->areaShape = render::LightAreaShape::Tube;
        } else if (value == "disc") {
            target->areaShape = render::LightAreaShape::Disc;
        } else {
            if (error != nullptr) {
                *error = "lights[" + std::to_string(index) + "].shape must be rectangle, tube or disc";
            }
            return false;
        }
    }

    if (!readNumber(object, "intensity", target->intensity, error) ||
        !readVec3(object, "color", target->color, error) ||
        !readNumber(object, "width", target->areaWidth, error) ||
        !readNumber(object, "height", target->areaHeight, error) ||
        !readNumber(object, "areaShadowCone", target->areaShadowCone, error)) {
        return false;
    }
    if (target->areaWidth <= 0.0f || target->areaHeight < 0.0f) {
        HBG_LOGW("light '{}': area size must have positive width and non-negative height; using 1x1",
                 target->name);
        target->areaWidth = 1.0f;
        target->areaHeight = 1.0f;
    }
    if (target->areaShape == render::LightAreaShape::Tube) {
        if (target->areaHeight > 0.0f) {
            HBG_LOGW("light '{}': tube area light ignores height and is evaluated as a line",
                     target->name);
        }
        target->areaHeight = 0.0f;
    } else if (target->areaShape == render::LightAreaShape::Disc) {
        target->intensity = 0.0f;
        HBG_LOGW("light '{}': disc area lights are unsupported in real-time; contribution disabled",
                 target->name);
    }
    target->areaShadowCone = std::max(target->areaShadowCone, 0.0f);
    target->castShadows = areaShadowRequested && target->areaShape == render::LightAreaShape::Rectangle;
    if (areaShadowRequested && target->areaShape != render::LightAreaShape::Rectangle) {
        HBG_LOGW("light '{}': area shadows are only supported for rectangle lights; shadow disabled",
                 target->name);
    }
    return true;
}

float readImageLuminance(const ImageAsset& image, uint32_t x, uint32_t y) {
    x = std::min(x, image.width - 1u);
    y = std::min(y, image.height - 1u);
    if (image.format == gfx::TextureFormat::RGBA32F) {
        const float* pixels = reinterpret_cast<const float*>(image.pixels.data());
        const std::size_t offset = (static_cast<std::size_t>(y) * image.width + x) * 4u;
        return 0.2126f * pixels[offset] + 0.7152f * pixels[offset + 1] + 0.0722f * pixels[offset + 2];
    }
    const std::size_t offset = (static_cast<std::size_t>(y) * image.width + x) * 4u;
    return (0.2126f * image.pixels[offset] + 0.7152f * image.pixels[offset + 1] +
            0.0722f * image.pixels[offset + 2]) /
           255.0f;
}

float sampleImageLuminance(const ImageAsset& image, Vec2 uv) {
    const float x = std::clamp(uv.x, 0.0f, 1.0f) * static_cast<float>(image.width - 1u);
    const float y = std::clamp(uv.y, 0.0f, 1.0f) * static_cast<float>(image.height - 1u);
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(x0 + 1u, image.width - 1u);
    const uint32_t y1 = std::min(y0 + 1u, image.height - 1u);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const float low = readImageLuminance(image, x0, y0) * (1.0f - tx) + readImageLuminance(image, x1, y0) * tx;
    const float high = readImageLuminance(image, x0, y1) * (1.0f - tx) + readImageLuminance(image, x1, y1) * tx;
    return low * (1.0f - ty) + high * ty;
}

uint32_t nextCookiePowerOfTwo(uint32_t value) {
    uint32_t result = 1;
    while (result < value && result < render::kCookieAtlasMaxNativeSize) {
        result <<= 1;
    }
    return std::clamp(result, 8u, render::kCookieAtlasMaxNativeSize);
}

Vec3 octahedralDecodeDirection(Vec2 encoded) {
    Vec2 f(encoded.x * 2.0f - 1.0f, encoded.y * 2.0f - 1.0f);
    Vec3 n(f.x, f.y, 1.0f - std::abs(f.x) - std::abs(f.y));
    if (n.z < 0.0f) {
        n.x = (1.0f - std::abs(n.y)) * (n.x >= 0.0f ? 1.0f : -1.0f);
        n.y = (1.0f - std::abs(n.x)) * (n.y >= 0.0f ? 1.0f : -1.0f);
    }
    return normalize(n);
}

Vec2 octahedralDecode(Vec2 encoded) {
    Vec2 f(encoded.x * 2.0f - 1.0f, encoded.y * 2.0f - 1.0f);
    Vec3 n(f.x, f.y, 1.0f - std::abs(f.x) - std::abs(f.y));
    if (n.z < 0.0f) {
        n.x = (1.0f - std::abs(n.y)) * (n.x >= 0.0f ? 1.0f : -1.0f);
        n.y = (1.0f - std::abs(n.x)) * (n.y >= 0.0f ? 1.0f : -1.0f);
    }
    n = normalize(n);
    return Vec2(atan2(n.x, n.z) * (0.5f / 3.14159265359f) + 0.5f,
                acos(std::clamp(n.y, -1.0f, 1.0f)) / 3.14159265359f);
}

bool loadCookieSamples(render::Light& light, const std::filesystem::path& path, std::string* error) {
    ImageAsset image;
    if (!TextureLoader::loadFromFile(path, image, error)) {
        return false;
    }
    const uint32_t size = render::kCookieLegacySize;
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const Vec2 uv((static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                          (static_cast<float>(y) + 0.5f) / static_cast<float>(size));
            Vec2 sourceUV = uv;
            if (light.cookieType == render::LightCookieType::PointOctahedral) {
                sourceUV = octahedralDecode(uv);
            }
            light.cookieSamples[y * size + x] =
                std::clamp(sampleImageLuminance(image, sourceUV), 0.0f, 1.0f);
        }
    }

    if (light.cookieType == render::LightCookieType::PointOctahedral) {
        const uint32_t nativeSize = nextCookiePowerOfTwo(std::max(image.width, image.height));
        light.cookieNativeWidth = nativeSize;
        light.cookieNativeHeight = nativeSize;
        light.cookieNative.assign(static_cast<std::size_t>(nativeSize) * nativeSize, 1.0f);
        for (uint32_t y = 0; y < nativeSize; ++y) {
            for (uint32_t x = 0; x < nativeSize; ++x) {
                const Vec2 uv((static_cast<float>(x) + 0.5f) / static_cast<float>(nativeSize),
                              (static_cast<float>(y) + 0.5f) / static_cast<float>(nativeSize));
                light.cookieNative[static_cast<std::size_t>(y) * nativeSize + x] =
                    std::clamp(sampleImageLuminance(image, octahedralDecode(uv)), 0.0f, 1.0f);
            }
        }
    } else {
        light.cookieNativeWidth = nextCookiePowerOfTwo(image.width);
        light.cookieNativeHeight = nextCookiePowerOfTwo(image.height);
        light.cookieNative.assign(
            static_cast<std::size_t>(light.cookieNativeWidth) * light.cookieNativeHeight, 1.0f);
        for (uint32_t y = 0; y < light.cookieNativeHeight; ++y) {
            for (uint32_t x = 0; x < light.cookieNativeWidth; ++x) {
                const Vec2 uv((static_cast<float>(x) + 0.5f) / static_cast<float>(light.cookieNativeWidth),
                              (static_cast<float>(y) + 0.5f) / static_cast<float>(light.cookieNativeHeight));
                light.cookieNative[static_cast<std::size_t>(y) * light.cookieNativeWidth + x] =
                    std::clamp(sampleImageLuminance(image, uv), 0.0f, 1.0f);
            }
        }
    }
    return true;
}

struct IesProfile {
    std::vector<float> verticalAngles;
    std::vector<float> horizontalAngles;
    std::vector<float> values;
    uint32_t verticalCount = 0;
    uint32_t horizontalCount = 0;
};

bool loadIesProfile(const std::filesystem::path& path, IesProfile& profile, std::string* error) {
    std::ifstream file(path);
    if (!file.good()) {
        if (error != nullptr) {
            *error = "failed to open IES profile: " + path.string();
        }
        return false;
    }
    bool foundTilt = false;
    std::string line;
    while (std::getline(file, line)) {
        const std::string normalized = lower(line);
        if (normalized.find("tilt=") == std::string::npos) {
            continue;
        }
        if (normalized.find("tilt=none") == std::string::npos) {
            if (error != nullptr) {
                *error = "IES profile must use TILT=NONE: " + path.string();
            }
            return false;
        }
        foundTilt = true;
        break;
    }
    if (!foundTilt) {
        if (error != nullptr) {
            *error = "IES profile is missing TILT: " + path.string();
        }
        return false;
    }

    uint32_t lampCount = 0;
    uint32_t verticalCount = 0;
    uint32_t horizontalCount = 0;
    float lumens = 0.0f;
    float multiplier = 0.0f;
    int photometricType = 0;
    int unitsType = 0;
    float width = 0.0f;
    float length = 0.0f;
    float height = 0.0f;
    if (!(file >> lampCount >> lumens >> multiplier >> verticalCount >> horizontalCount >> photometricType >>
          unitsType >> width >> length >> height)) {
        if (error != nullptr) {
            *error = "invalid IES header: " + path.string();
        }
        return false;
    }
    if (verticalCount < 2 || verticalCount > 512 || horizontalCount < 1 || horizontalCount > 512) {
        if (error != nullptr) {
            *error = "unsupported IES angle count: " + path.string();
        }
        return false;
    }

    profile.verticalCount = verticalCount;
    profile.horizontalCount = horizontalCount;
    profile.verticalAngles.assign(verticalCount, 0.0f);
    profile.horizontalAngles.assign(horizontalCount, 0.0f);
    for (float& angle : profile.verticalAngles) {
        file >> angle;
    }
    for (float& angle : profile.horizontalAngles) {
        file >> angle;
    }
    profile.values.assign(static_cast<std::size_t>(verticalCount) * horizontalCount, 0.0f);
    for (float& value : profile.values) {
        file >> value;
    }
    if (!file.good()) {
        if (error != nullptr) {
            *error = "truncated IES profile: " + path.string();
        }
        return false;
    }
    return true;
}

float sampleIesProfile(const IesProfile& profile, float verticalDegrees, float horizontalDegrees) {
    uint32_t verticalUpper = 0;
    while (verticalUpper + 1 < profile.verticalCount &&
           profile.verticalAngles[verticalUpper + 1] < verticalDegrees) {
        ++verticalUpper;
    }
    const uint32_t verticalLower = verticalUpper;
    const uint32_t verticalNext = std::min(verticalUpper + 1, profile.verticalCount - 1);
    const float verticalSpan = profile.verticalAngles[verticalNext] - profile.verticalAngles[verticalLower];
    const float verticalT = verticalSpan > 1.0e-5f
                                ? std::clamp((verticalDegrees - profile.verticalAngles[verticalLower]) /
                                                 verticalSpan,
                                             0.0f, 1.0f)
                                : 0.0f;

    float horizontalDegreesWrapped = horizontalDegrees;
    while (horizontalDegreesWrapped < 0.0f) {
        horizontalDegreesWrapped += 360.0f;
    }
    while (horizontalDegreesWrapped >= 360.0f) {
        horizontalDegreesWrapped -= 360.0f;
    }

    uint32_t horizontalUpper = profile.horizontalCount - 1;
    uint32_t horizontalNext = 0;
    float horizontalT = 0.0f;
    if (profile.horizontalCount > 1) {
        horizontalUpper = 0;
        while (horizontalUpper + 1 < profile.horizontalCount &&
               profile.horizontalAngles[horizontalUpper + 1] <= horizontalDegreesWrapped) {
            ++horizontalUpper;
        }
        horizontalNext = (horizontalUpper + 1) % profile.horizontalCount;
        const float lowerAngle = profile.horizontalAngles[horizontalUpper];
        const float upperAngle = profile.horizontalAngles[horizontalNext] +
                                 (horizontalNext == 0 ? 360.0f : 0.0f);
        const float span = upperAngle - lowerAngle;
        horizontalT = span > 1.0e-5f
                          ? std::clamp((horizontalDegreesWrapped - lowerAngle) / span, 0.0f, 1.0f)
                          : 0.0f;
    }

    const auto valueAt = [&profile](uint32_t vertical, uint32_t horizontal) {
        return profile.values[static_cast<std::size_t>(vertical) * profile.horizontalCount + horizontal];
    };
    const float low = valueAt(verticalLower, horizontalUpper) * (1.0f - horizontalT) +
                      valueAt(verticalLower, horizontalNext) * horizontalT;
    const float high = valueAt(verticalNext, horizontalUpper) * (1.0f - horizontalT) +
                       valueAt(verticalNext, horizontalNext) * horizontalT;
    return low + (high - low) * verticalT;
}

bool loadIesSamples(render::Light& light, const std::filesystem::path& path, std::string* error) {
    IesProfile profile;
    if (!loadIesProfile(path, profile, error)) {
        return false;
    }

    std::vector<float> slice(profile.verticalCount);
    float maximum = 0.0f;
    for (uint32_t index = 0; index < profile.verticalCount; ++index) {
        slice[index] = profile.values[static_cast<std::size_t>(index) * profile.horizontalCount];
        maximum = std::max(maximum, slice[index]);
    }
    if (maximum <= 0.0f) {
        if (error != nullptr) {
            *error = "IES profile has no positive candela values: " + path.string();
        }
        return false;
    }

    for (uint32_t sample = 0; sample < render::kLightIesSampleCount; ++sample) {
        const float angle = 90.0f * static_cast<float>(sample) / static_cast<float>(render::kLightIesSampleCount - 1);
        uint32_t upper = 0;
        while (upper + 1 < profile.verticalCount && profile.verticalAngles[upper + 1] < angle) {
            ++upper;
        }
        const uint32_t lower = upper;
        const uint32_t next = std::min(upper + 1, profile.verticalCount - 1);
        const float span = profile.verticalAngles[next] - profile.verticalAngles[lower];
        const float t = span > 1.0e-5f ? std::clamp((angle - profile.verticalAngles[lower]) / span, 0.0f, 1.0f)
                                       : 0.0f;
        light.iesSamples[sample] =
            std::clamp((slice[lower] + (slice[next] - slice[lower]) * t) / maximum, 0.0f, 1.0f);
    }
    uint32_t lastVisible = 0;
    for (uint32_t sample = 0; sample < render::kLightIesSampleCount; ++sample) {
        if (light.iesSamples[sample] > 0.001f) {
            lastVisible = sample;
        }
    }
    const float cutoffAngle =
        90.0f * static_cast<float>(lastVisible) / static_cast<float>(render::kLightIesSampleCount - 1);
    light.iesCut = std::cos(cutoffAngle * 3.14159265358979323846f / 180.0f);
    light.hasIes = true;
    return true;
}

bool loadIesPointSamples(render::Light& light, const std::filesystem::path& path, std::string* error) {
    IesProfile profile;
    if (!loadIesProfile(path, profile, error)) {
        return false;
    }
    float maximum = 0.0f;
    for (float value : profile.values) {
        maximum = std::max(maximum, value);
    }
    if (maximum <= 0.0f) {
        if (error != nullptr) {
            *error = "IES profile has no positive candela values: " + path.string();
        }
        return false;
    }

    const uint32_t size = render::kCookieLegacySize;
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const Vec2 uv((static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                          (static_cast<float>(y) + 0.5f) / static_cast<float>(size));
            const Vec3 direction = octahedralDecodeDirection(uv);
            const float vertical = std::acos(std::clamp(direction.y, -1.0f, 1.0f)) * 57.2957795131f;
            float horizontal = std::atan2(direction.x, direction.z) * 57.2957795131f;
            if (horizontal < 0.0f) {
                horizontal += 360.0f;
            }
            const float sample = std::clamp(sampleIesProfile(profile, vertical, horizontal) / maximum, 0.0f, 1.0f);
            light.cookieSamples[y * size + x] *= sample;
        }
    }

    if (light.cookieNativeWidth == 0 || light.cookieNativeHeight == 0) {
        light.cookieNativeWidth = 64;
        light.cookieNativeHeight = 64;
        light.cookieNative.assign(static_cast<std::size_t>(light.cookieNativeWidth) * light.cookieNativeHeight, 1.0f);
    }
    for (uint32_t y = 0; y < light.cookieNativeHeight; ++y) {
        for (uint32_t x = 0; x < light.cookieNativeWidth; ++x) {
            const Vec2 uv((static_cast<float>(x) + 0.5f) / static_cast<float>(light.cookieNativeWidth),
                          (static_cast<float>(y) + 0.5f) / static_cast<float>(light.cookieNativeHeight));
            const Vec3 direction = octahedralDecodeDirection(uv);
            const float vertical = std::acos(std::clamp(direction.y, -1.0f, 1.0f)) * 57.2957795131f;
            float horizontal = std::atan2(direction.x, direction.z) * 57.2957795131f;
            if (horizontal < 0.0f) {
                horizontal += 360.0f;
            }
            const float sample = std::clamp(sampleIesProfile(profile, vertical, horizontal) / maximum, 0.0f, 1.0f);
            light.cookieNative[static_cast<std::size_t>(y) * light.cookieNativeWidth + x] *= sample;
        }
    }
    light.hasIes = true;
    return true;
}

void applyLightCookie(render::RenderScene& scene, const nlohmann::json& object, std::size_t index,
                      const std::filesystem::path& baseDirectory) {
    render::Light* target = resolveLightTarget(scene, object, index);
    if (target == nullptr) {
        return;
    }

    if (!readVec2(object, "cookieScale", target->cookieScale, nullptr) ||
        !readVec2(object, "cookieOffset", target->cookieOffset, nullptr)) {
        HBG_LOGW("light '{}': invalid cookie scale/offset; using identity", target->name);
        target->cookieScale = Vec2(1.0f, 1.0f);
        target->cookieOffset = Vec2(0.0f, 0.0f);
    }

    const auto cookie = object.find("cookie");
    if (cookie != object.end() && cookie->is_string() && !cookie->get<std::string>().empty()) {
        const bool point = target->type == render::LightType::Point;
        target->cookieType = point ? render::LightCookieType::PointOctahedral
                                   : render::LightCookieType::Spot2D;
        target->cookieMode = render::LightCookieMode::Clamp;
        const auto mode = object.find("cookieMode");
        if (mode != object.end() && mode->is_string()) {
            const std::string value = lower(mode->get<std::string>());
            if (value == "none") {
                target->cookieMode = render::LightCookieMode::None;
            } else if (value == "clamp") {
                target->cookieMode = render::LightCookieMode::Clamp;
            } else if (value == "repeat") {
                target->cookieMode = render::LightCookieMode::Repeat;
            } else {
                HBG_LOGW("light '{}': unknown cookieMode '{}'; using clamp", target->name, value);
            }
        }
        std::string cookieError;
        if (!loadCookieSamples(*target, baseDirectory / cookie->get<std::string>(), &cookieError)) {
            HBG_LOGW("light '{}': {}; cookie disabled", target->name, cookieError);
            target->cookieMode = render::LightCookieMode::None;
            target->cookieType = render::LightCookieType::None;
        }
    }

    const auto ies = object.find("ies");
    if (ies != object.end() && ies->is_string() && !ies->get<std::string>().empty()) {
        std::string iesError;
        if (target->type == render::LightType::Point) {
            const bool hadCookie = target->cookieType == render::LightCookieType::PointOctahedral;
            if (!hadCookie) {
                target->cookieMode = render::LightCookieMode::Clamp;
                target->cookieType = render::LightCookieType::PointOctahedral;
                target->cookieSamples.fill(1.0f);
            }
            if (!loadIesPointSamples(*target, baseDirectory / ies->get<std::string>(), &iesError)) {
                HBG_LOGW("light '{}': {}; IES disabled", target->name, iesError);
                if (!hadCookie) {
                    target->cookieMode = render::LightCookieMode::None;
                    target->cookieType = render::LightCookieType::None;
                    target->cookieSamples.fill(0.0f);
                }
            }
        } else if (!loadIesSamples(*target, baseDirectory / ies->get<std::string>(), &iesError)) {
            HBG_LOGW("light '{}': {}; IES disabled", target->name, iesError);
        }
    }
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
            if (!applyLightArea(scene, light, index, error) ||
                !applyLightShadow(scene, light, index, error)) {
                return false;
            }
            applyLightCookie(scene, light, index, baseDirectory);
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

    const auto vfx = root.find("vfx");
    if (vfx != root.end()) {
        if (!vfx->is_array()) {
            if (error != nullptr) {
                *error = "vfx must be an array";
            }
            return false;
        }
        std::size_t index = 0;
        for (const auto& instance : *vfx) {
            if (!applyVfxInstance(scene, instance, baseDirectory, index, error)) {
                return false;
            }
            ++index;
        }
    }

    const auto objects = root.find("objects");
    if (objects != root.end()) {
        if (!objects->is_array()) {
            if (error != nullptr) {
                *error = "objects must be an array";
            }
            return false;
        }
        std::size_t index = 0;
        for (const auto& object : *objects) {
            if (!applyObjectLayer(scene, object, index, error)) {
                return false;
            }
            ++index;
        }
    }

    const auto lightmap = root.find("lightmap");
    if (lightmap != root.end() && !applyLightmap(scene, *lightmap, baseDirectory, error)) {
        return false;
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
