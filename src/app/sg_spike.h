#pragma once

#include "gfx/resource_manager.h"

#include <cstdint>
#include <string>

namespace hbg {

struct SgSpikeOptions {
    std::string assetPath;
    std::string screenshotPath;
    std::string token;
    std::uint16_t port = 8777;
    std::uint32_t frames = 3;
    bool framesSpecified = false;
    bool preview = false;
};

bool runSgSpike(gfx::ResourceManager& resources, std::uint16_t width, std::uint16_t height,
                const SgSpikeOptions& options);

} // namespace hbg
