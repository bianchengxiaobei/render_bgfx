#include "app/asset_test.h"

#include "asset/scene_loader.h"
#include "asset/texture_loader.h"
#include "core/log.h"

#include <cstddef>

namespace hbg {

bool runAssetTest(gfx::ResourceManager& resources, const std::string& path) {
    HBG_LOGI("loading scene '{}'", path);
    asset::SceneLoadResult result = asset::SceneLoader::load(path);
    if (!result.ok()) {
        HBG_LOGE("scene load failed: {}", result.error);
        return false;
    }

    const render::RenderScene& scene = *result.scene;
    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
    std::size_t subMeshCount = 0;
    for (const asset::Mesh& mesh : scene.meshes) {
        vertexCount += mesh.vertices.size();
        indexCount += mesh.indices.size();
        subMeshCount += mesh.submeshes.size();
    }

    HBG_LOGI("scene loaded: meshes={} vertices={} indices={} submeshes={} materials={} images={} objects={} "
             "cameras={} lights={}",
             scene.meshes.size(), vertexCount, indexCount, subMeshCount, scene.materials.size(), scene.images.size(),
             scene.objects.size(), scene.cameras.size(), scene.lights.size());
    if (scene.bounds.valid()) {
        HBG_LOGI("scene bounds: min=({}, {}, {}) max=({}, {}, {})", scene.bounds.min.x, scene.bounds.min.y,
                 scene.bounds.min.z, scene.bounds.max.x, scene.bounds.max.y, scene.bounds.max.z);
    }

    for (const asset::ImageAsset& image : scene.images) {
        const gfx::TextureHandle texture = asset::TextureLoader::createTexture(resources, image, true, image.name);
        if (!texture.valid()) {
            HBG_LOGE("failed to upload scene image '{}'", image.name);
            return false;
        }
        HBG_LOGI("uploaded image '{}' ({}x{})", image.name, image.width, image.height);
        resources.destroyTexture(texture);
    }
    resources.flushPending(true);
    return true;
}

} // namespace hbg
