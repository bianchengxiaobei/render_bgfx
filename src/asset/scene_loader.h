#pragma once

#include "render/scene/render_scene.h"

#include <filesystem>
#include <memory>
#include <string>

namespace hbg::asset {

struct SceneLoadOptions {
    std::filesystem::path sidecar;
    bool loadSidecar = true;
};

struct SceneLoadResult {
    std::unique_ptr<render::RenderScene> scene;
    std::string error;

    bool ok() const { return scene != nullptr; }
};

class SceneLoader {
  public:
    static SceneLoadResult load(const std::filesystem::path& path, const SceneLoadOptions& options = {});
    static bool applySidecarToScene(render::RenderScene& scene, const std::filesystem::path& sidecar,
                                    std::string* error = nullptr);
};

} // namespace hbg::asset
