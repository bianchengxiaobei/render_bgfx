#include "asset/scene_loader.h"

#include "asset/gltf_loader.h"
#include "core/log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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

bool applySidecar(render::RenderScene& scene, const std::filesystem::path& sidecar, std::string* error) {
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
            if (!applySidecar(*result.scene, sidecar, &error)) {
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
