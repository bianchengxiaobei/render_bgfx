#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hbg {

struct SgPreviewOptions {
    std::string mesh = "sphere";
    int size = 512;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float distance = 0.0f;
    std::string background = "checker";
    std::string channel = "rgb";
    std::string format = "jpeg";
    int fps = 10;
};

using SgPreviewCallback =
    std::function<bool(const std::vector<std::uint8_t>& sgb, const SgPreviewOptions& options,
                       std::vector<std::uint8_t>& jpeg, std::string& error)>;

} // namespace hbg
