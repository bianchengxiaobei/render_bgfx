#pragma once

#include "hbg/dawn/dawn_scene.h"

#include <filesystem>

namespace hbg::dawn {

bool isParcelScenePath(const std::filesystem::path& path);

DawnScenePackage buildDawnScenePackageFromParcel(const std::filesystem::path& scenePath,
                                                 const DawnSceneExportOptions& options);

} // namespace hbg::dawn
