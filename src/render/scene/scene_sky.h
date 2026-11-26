#pragma once

#include "core/math.h"

#include <cstdint>
#include <filesystem>

namespace hbg::render {

enum class SceneSkyMode : uint8_t {
    None,
    Skybox,
    Trilight,
    Flat,
};

struct SceneSky {
    SceneSkyMode mode = SceneSkyMode::None;
    uint32_t instanceId = 0;
    std::filesystem::path texture;
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float indirectMultiplier = 1.0f;
    float indirectIntensity = 1.0f;
    float rotationDegrees = 0.0f;
    float shadowTranslucency = 1.0f;
    Vec3 adaptRotationEulerDegrees{0.0f, 0.0f, 0.0f};
    Vec3 ambientColor{0.5f, 0.5f, 0.5f};
    Vec3 skyColor{0.5f, 0.5f, 0.5f};
    Vec3 equatorColor{0.5f, 0.5f, 0.5f};
    Vec3 groundColor{0.5f, 0.5f, 0.5f};
    float lerpExponent = 1.0f;
    bool enabled = true;
};

} // namespace hbg::render
