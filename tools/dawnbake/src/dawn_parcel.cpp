#include "hbg/dawn/dawn_parcel.h"

#include "asset/scene_loader.h"
#include "asset/texture_loader.h"
#include "core/log.h"
#include "hbg/dawn/dawn_launch.h"

#include "parcel/assets/asset.hpp"
#include "parcel/assets/material_asset.hpp"
#include "parcel/assets/scene_asset.hpp"
#include "parcel/assets/texture_asset.hpp"
#include "parcel/core/error.hpp"
#include "parcel/entt/scene_loader.hpp"
#include "parcel/hbg/scene_render_bridge.hpp"
#include "parcel/import/importer.hpp"
#include "parcel/import/meta.hpp"
#include "parcel/import/texture_dedup.hpp"

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace parcel::import {
std::shared_ptr<const IAssetImporter> MakeFbxImporter();
std::shared_ptr<const IAssetImporter> MakeTextureImporter();
}

namespace hbg::dawn {
namespace {

std::string lowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

std::filesystem::path resolveBlenderExecutable(const DawnSceneExportOptions& options) {
    if (!options.blenderExecutable.empty()) {
        return options.blenderExecutable;
    }
    if (const char* environment = std::getenv("HBG_BLENDER_EXECUTABLE");
        environment != nullptr && environment[0] != '\0') {
        return environment;
    }
#ifdef HBG_BLENDER_EXECUTABLE
    return HBG_BLENDER_EXECUTABLE;
#else
    return {};
#endif
}

std::filesystem::path convertBlendToGlb(const std::filesystem::path& blendPath,
                                        const DawnSceneExportOptions& options,
                                        std::filesystem::path* temporaryDirectory,
                                        std::string* error) {
    const std::filesystem::path blender = resolveBlenderExecutable(options);
    if (blender.empty()) {
        if (error != nullptr) {
            *error = "Blender executable is not configured; set --blender or HBG_BLENDER_EXECUTABLE";
        }
        return {};
    }
    if (!std::filesystem::is_regular_file(blender)) {
        if (error != nullptr) {
            *error = "Blender executable not found: " + blender.string();
        }
        return {};
    }

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path temporary =
        std::filesystem::temp_directory_path() / "hbg_dawnbake" / ("blend_" + std::to_string(stamp));
    std::error_code ec;
    std::filesystem::create_directories(temporary, ec);
    if (ec) {
        if (error != nullptr) {
            *error = "failed to create Blender temporary directory: " + ec.message();
        }
        return {};
    }
    if (temporaryDirectory != nullptr) {
        *temporaryDirectory = temporary;
    }

    const std::filesystem::path absoluteBlendPath = std::filesystem::absolute(blendPath);
    const std::filesystem::path glb = temporary / (absoluteBlendPath.stem().string() + ".glb");
    const std::string script =
        "import bpy; bpy.ops.export_scene.gltf(filepath=" +
        nlohmann::json(glb.generic_string()).dump() +
        ", export_format='GLB', export_apply=True)";

    ProcessOptions process;
    process.executable = blender;
    process.arguments = {"-b", absoluteBlendPath.string(), "--python-expr", script};
    process.workingDirectory = blender.parent_path();
    process.logPath = temporary / "blender.log";
    process.timeout = std::chrono::seconds(options.blenderTimeoutSeconds);
    const ProcessResult result = runProcess(process);
    if (result.status != ProcessStatus::Completed || result.exitCode != 0u ||
        !std::filesystem::is_regular_file(glb)) {
        if (error != nullptr) {
            *error = "Blender failed to export .blend: " + result.error;
            if (!result.logTail.empty()) {
                *error += "\n" + result.logTail;
            }
        }
        return {};
    }
    HBG_LOGI("Blender exported {} -> {}", blendPath.string(), glb.string());
    return glb;
}

bool isAssimpSource(const std::filesystem::path& path) {
    const std::string extension = lowerExtension(path);
    return extension == ".fbx" || extension == ".obj" || extension == ".dae" ||
           extension == ".3ds" || extension == ".ply" || extension == ".stl";
}

using AssetMap = std::unordered_map<parcel::core::AssetGuid,
                                    std::shared_ptr<parcel::assets::AssetObject>,
                                    parcel::core::Guid128Hash>;

bool isTextureSource(const std::filesystem::path& path) {
    const std::string extension = lowerExtension(path);
    return extension == ".png" || extension == ".tga" || extension == ".jpg" ||
           extension == ".jpeg" || extension == ".hdr";
}

bool isHbgTextureSource(const std::filesystem::path& path) {
    return lowerExtension(path) == ".exr";
}

std::size_t textureBytesPerPixel(hbg::gfx::TextureFormat format) {
    switch (format) {
    case hbg::gfx::TextureFormat::RGBA8:
        return 4;
    case hbg::gfx::TextureFormat::RGBA16F:
        return 8;
    case hbg::gfx::TextureFormat::RGBA32F:
        return 16;
    default:
        return 0;
    }
}

std::shared_ptr<parcel::assets::TextureAsset> makeParcelTexture(
    const asset::ImageAsset& image, std::string_view label) {
    const std::size_t bytesPerPixel = textureBytesPerPixel(image.format);
    if (bytesPerPixel == 0 || image.width == 0 || image.height == 0 ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * image.height * bytesPerPixel ||
        image.width > std::numeric_limits<uint32_t>::max() / bytesPerPixel) {
        HBG_LOGW("hbg texture import produced unsupported image: {}", label);
        return {};
    }

    auto texture = std::make_shared<parcel::assets::TextureAsset>();
    switch (image.format) {
    case hbg::gfx::TextureFormat::RGBA8:
        texture->format = parcel::assets::TextureFormat::RGBA8;
        break;
    case hbg::gfx::TextureFormat::RGBA16F:
        texture->format = parcel::assets::TextureFormat::RGBA16F;
        break;
    case hbg::gfx::TextureFormat::RGBA32F:
        texture->format = parcel::assets::TextureFormat::RGBA32F;
        break;
    default:
        return {};
    }
    texture->sRGB = image.format == hbg::gfx::TextureFormat::RGBA8;
    texture->mip_count = 1;
    parcel::assets::TextureMip mip;
    mip.width = image.width;
    mip.height = image.height;
    mip.row_pitch = static_cast<uint32_t>(image.width * bytesPerPixel);
    mip.data.resize(image.pixels.size());
    std::memcpy(mip.data.data(), image.pixels.data(), image.pixels.size());
    texture->mips.push_back(std::move(mip));
    return texture;
}

std::shared_ptr<parcel::assets::TextureAsset> decodeEmbeddedTexture(
    const std::byte* data, std::size_t size, void*) {
    if (data == nullptr || size == 0) {
        return {};
    }
    asset::ImageAsset image;
    std::string error;
    if (!asset::TextureLoader::loadFromMemory(data, size, image, &error)) {
        HBG_LOGW("embedded texture fallback failed: {}", error);
        return {};
    }
    return makeParcelTexture(image, "embedded");
}

bool importHbgTexture(const std::filesystem::path& path, const std::string& relativeText,
                      AssetMap& assets) {
    asset::ImageAsset image;
    std::string error;
    if (!asset::TextureLoader::loadFromFile(path, image, &error)) {
        HBG_LOGW("hbg texture import failed for '{}': {}", relativeText, error);
        return false;
    }
    auto texture = makeParcelTexture(image, relativeText);
    if (texture == nullptr) {
        return false;
    }
    assets[parcel::import::DefaultTextureAssetGuid(relativeText)] = std::move(texture);
    return true;
}

void importProjectTextures(const std::filesystem::path& projectRoot, AssetMap& assets) {
    const auto importer = parcel::import::MakeTextureImporter();
    std::error_code ec;
    std::filesystem::recursive_directory_iterator iterator(projectRoot, ec);
    const std::filesystem::recursive_directory_iterator end;
    std::size_t importedCount = 0;
    for (; !ec && iterator != end; iterator.increment(ec)) {
        if (!iterator->is_regular_file(ec)) {
            continue;
        }
        std::filesystem::path relative =
            std::filesystem::relative(iterator->path(), projectRoot, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        const std::string relativeText = relative.generic_string();
        if (isHbgTextureSource(iterator->path())) {
            if (importHbgTexture(iterator->path(), relativeText, assets)) {
                ++importedCount;
            }
            continue;
        }
        if (!importer || !isTextureSource(iterator->path())) {
            continue;
        }
        parcel::import::ImportRequest request;
        request.project_root = projectRoot.string();
        request.source_path = iterator->path().string();
        request.project_relative_path = relativeText;
        request.source_guid = parcel::import::MakeSourceGuid(relativeText);
        request.settings = nullptr;
        request.texture_index = nullptr;
        auto imported = importer->Import(request);
        if (!imported) {
            HBG_LOGW("Parcel texture import failed for '{}': {}", relativeText,
                     imported.GetError().ToString());
            continue;
        }
        const parcel::core::AssetGuid guid = parcel::import::DefaultTextureAssetGuid(relativeText);
        for (auto& cooked : imported.Value().assets) {
            if (cooked.object != nullptr) {
                assets[guid] = std::move(cooked.object);
                ++importedCount;
                break;
            }
        }
    }
    HBG_LOGI("Parcel import: loaded {} texture source(s) from {}", importedCount,
             projectRoot.string());
}

std::unique_ptr<render::RenderScene> importParcelScene(const std::filesystem::path& scenePath,
                                                       std::string* error) {
    if (lowerExtension(scenePath) == ".blend") {
        if (error != nullptr) {
            *error = "Parcel/Assimp cannot import .blend directly; export it to .glb/.gltf first";
        }
        return nullptr;
    }
    if (!isAssimpSource(scenePath)) {
        if (error != nullptr) {
            *error = "Parcel bridge does not support source extension: " + scenePath.extension().string();
        }
        return nullptr;
    }

    const auto importer = parcel::import::MakeFbxImporter();
    if (!importer) {
        if (error != nullptr) {
            *error = "Parcel Assimp importer is not available (rebuild with PARCEL_ASSIMP_ROOT)";
        }
        return nullptr;
    }

    const std::filesystem::path absolutePath = std::filesystem::absolute(scenePath);
    const std::filesystem::path projectRoot = absolutePath.parent_path();
    const std::filesystem::path relativePath = absolutePath.filename();
    parcel::import::ImportRequest request;
    request.project_root = projectRoot.string();
    request.source_path = absolutePath.string();
    request.project_relative_path = relativePath.generic_string();
    request.source_guid = parcel::import::MakeSourceGuid(request.project_relative_path);
    request.settings = nullptr;
    request.texture_index = nullptr;
    request.texture_decode_fallback = &decodeEmbeddedTexture;
    request.texture_decode_user = nullptr;

    auto imported = importer->Import(request);
    if (!imported) {
        if (error != nullptr) {
            *error = "Parcel import failed: " + imported.GetError().ToString();
        }
        return nullptr;
    }
    for (const std::string& warning : imported.Value().warnings) {
        HBG_LOGW("Parcel import: {}", warning);
    }

    AssetMap assets;
    std::shared_ptr<parcel::assets::SceneAsset> sceneAsset;
    for (auto& cooked : imported.Value().assets) {
        if (cooked.guid.IsZero() || cooked.object == nullptr) {
            continue;
        }
        assets.emplace(cooked.guid, cooked.object);
        if (cooked.object->Type() == parcel::assets::kSceneAssetType && sceneAsset == nullptr) {
            sceneAsset = std::static_pointer_cast<parcel::assets::SceneAsset>(cooked.object);
        }
    }
    if (sceneAsset == nullptr) {
        if (error != nullptr) {
            *error = "Parcel import produced no SceneAsset";
        }
        return nullptr;
    }

    importProjectTextures(projectRoot, assets);

    std::size_t strippedTextures = 0;
    for (auto& [guid, object] : assets) {
        if (object->Type() != parcel::assets::kMaterialAssetType) {
            continue;
        }
        auto material = std::static_pointer_cast<parcel::assets::MaterialAsset>(object);
        const auto clearMissing = [&assets, &strippedTextures](parcel::assets::TextureRef& reference) {
            if (!reference.texture.IsZero() && assets.find(reference.texture) == assets.end()) {
                reference.texture = {};
                ++strippedTextures;
            }
        };
        clearMissing(material->base_color_texture);
        clearMissing(material->metallic_roughness_texture);
        clearMissing(material->normal_texture);
        clearMissing(material->occlusion_texture);
        clearMissing(material->emissive_texture);
    }
    if (strippedTextures != 0) {
        HBG_LOGW("Parcel import: {} unresolved texture references were cleared", strippedTextures);
    }

    entt::registry ecs;
    parcel_entt::EnttSceneLoader loader;
    auto instance = loader.Instantiate(sceneAsset->scene, &ecs);
    if (!instance) {
        if (error != nullptr) {
            *error = "failed to instantiate Parcel scene: " + instance.GetError().ToString();
        }
        return nullptr;
    }

    auto scene = std::make_unique<render::RenderScene>();
    parcel_hbg::SceneRenderBridge bridge(
        [&assets](parcel::core::AssetGuid guid)
            -> parcel::core::Result<std::shared_ptr<parcel::assets::AssetObject>> {
            const auto found = assets.find(guid);
            if (found == assets.end()) {
                return parcel::core::Error{parcel::core::ErrorCode::NotFound,
                                           "Parcel asset not found: " + guid.ToHex()};
            }
            return found->second;
        });
    auto updated = bridge.Update(ecs, *scene);
    if (auto destroyed = loader.Destroy(instance.Value()); !destroyed) {
        HBG_LOGW("Parcel scene destroy failed: {}", destroyed.GetError().ToString());
    }
    if (!updated) {
        if (error != nullptr) {
            *error = "Parcel RenderScene bridge failed: " + updated.GetError().ToString();
        }
        return nullptr;
    }
    return scene;
}

} // namespace

bool isParcelScenePath(const std::filesystem::path& path) {
    const std::string extension = lowerExtension(path);
    return extension == ".fbx" || extension == ".obj" || extension == ".dae" ||
           extension == ".3ds" || extension == ".ply" || extension == ".stl" ||
           extension == ".blend";
}

DawnScenePackage buildDawnScenePackageFromParcel(const std::filesystem::path& scenePath,
                                                 const DawnSceneExportOptions& options) {
    if (lowerExtension(scenePath) == ".blend") {
        std::filesystem::path temporaryDirectory;
        std::string error;
        const std::filesystem::path glb =
            convertBlendToGlb(scenePath, options, &temporaryDirectory, &error);
        if (glb.empty()) {
            throw std::runtime_error(error);
        }
        DawnSceneExportOptions gltfOptions = options;
        if (gltfOptions.sidecar.empty()) {
            const std::filesystem::path sourceSidecar =
                scenePath.parent_path() / (scenePath.stem().string() + ".hdrp.json");
            if (std::filesystem::is_regular_file(sourceSidecar)) {
                gltfOptions.sidecar = sourceSidecar;
            }
        }
        DawnScenePackage package = buildDawnScenePackage(glb, gltfOptions);
        std::error_code ec;
        std::filesystem::remove_all(temporaryDirectory, ec);
        return package;
    }

    std::string error;
    std::unique_ptr<render::RenderScene> scene = importParcelScene(scenePath, &error);
    if (scene == nullptr) {
        throw std::runtime_error(error);
    }

    std::filesystem::path sidecar = options.sidecar;
    if (sidecar.empty()) {
        sidecar = scenePath.parent_path() / (scenePath.stem().string() + ".hdrp.json");
    }
    if (options.loadSidecar && std::filesystem::is_regular_file(sidecar)) {
        if (!asset::SceneLoader::applySidecarToScene(*scene, sidecar, &error)) {
            throw std::runtime_error("failed to apply scene sidecar: " + error);
        }
    }
    return buildDawnScenePackage(scenePath, options, scene.get());
}

} // namespace hbg::dawn
