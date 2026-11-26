#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hbg {

bool encodeJpeg(const std::uint8_t* rgba, int width, int height, int quality,
                std::vector<std::uint8_t>& output);
bool encodePng(const std::uint8_t* rgba, int width, int height,
               std::vector<std::uint8_t>& output);
bool writeImageFile(const std::string& path, const std::uint8_t* rgba, int width, int height,
                    int quality = 90);

} // namespace hbg
