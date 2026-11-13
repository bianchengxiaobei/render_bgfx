#pragma once

#include "gfx/resource_manager.h"
#include "gfx/texture.h"
#include "render/sky/environment_processor.h"

#include <string>

namespace hbg::render {

bool uploadEnvironmentTexture(gfx::ResourceManager& resources, const EnvironmentImage& environment, const char* name,
                              gfx::TextureHandle& texture, std::string* error = nullptr);

} // namespace hbg::render
