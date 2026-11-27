#pragma once

#include "render/scene/render_scene.h"

#include <filesystem>
#include <memory>
#include <string>

namespace hbg::asset {

struct GltfLoadResult {
    std::unique_ptr<render::RenderScene> scene;
    std::string error;

    bool ok() const { return scene != nullptr; }
};

class GltfLoader {
  public:
    static GltfLoadResult load(const std::filesystem::path& path);
};

} // namespace hbg::asset
