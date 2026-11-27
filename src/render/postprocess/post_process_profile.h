#pragma once

#include "render/postprocess/post_process_settings.h"

#include <string>
#include <string_view>

namespace hbg::render {

struct PostProcessProfile {
    std::string name = "baseline";
    PostProcessSettings settings{};
};

PostProcessProfile baselinePostProcessProfile();
bool parsePostProcessProfile(std::string_view json, PostProcessProfile& profile, std::string& error);
bool loadPostProcessProfile(const std::string& path, PostProcessProfile& profile, std::string& error);

} // namespace hbg::render
