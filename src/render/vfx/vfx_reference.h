#pragma once

#include "gfx/resource_manager.h"

#include <string>

namespace hbg {

bool runVfxReference(gfx::ResourceManager& resources, const std::string& assetPath,
                     const std::string& graphPath);
bool runVfxEventReference(gfx::ResourceManager& resources, const std::string& assetPath,
                          const std::string& graphPath);
bool runVfxCpuEventReference(gfx::ResourceManager& resources, const std::string& assetPath,
                             const std::string& graphPath);

} // namespace hbg
