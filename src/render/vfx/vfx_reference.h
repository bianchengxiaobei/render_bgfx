#pragma once

#include "gfx/resource_manager.h"

#include <string>

namespace hbg {

bool runVfxReference(gfx::ResourceManager& resources, const std::string& assetPath,
                     const std::string& graphPath);

} // namespace hbg
