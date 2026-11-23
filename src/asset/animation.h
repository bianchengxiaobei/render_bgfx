#pragma once

#include "core/math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hbg::render {
struct RenderScene;
}

namespace hbg::asset {

enum class AnimationPath : uint8_t { Translation, Rotation, Scale, Weights };
enum class AnimationInterpolation : uint8_t { Step, Linear, CubicSpline };

struct AnimationChannel {
    uint32_t node = UINT32_MAX;
    AnimationPath path = AnimationPath::Translation;
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    uint32_t targetIndex = UINT32_MAX;
    std::vector<float> times;
    std::vector<Vec4> values;
    std::vector<Vec4> inTangents;
    std::vector<Vec4> outTangents;
};

struct AnimationClip {
    std::string name;
    std::vector<AnimationChannel> channels;
    float duration = 0.0f;
};

struct SceneNode {
    std::string name;
    int parent = -1;
    Vec3 translation{0.0f, 0.0f, 0.0f};
    Vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Vec3 baseTranslation{0.0f, 0.0f, 0.0f};
    Vec4 baseRotation{0.0f, 0.0f, 0.0f, 1.0f};
    Vec3 baseScale{1.0f, 1.0f, 1.0f};
    std::vector<float> weights;
    std::vector<float> baseWeights;
    bool hasMatrix = false;
    Mat4 matrix = Mat4::identity();
};

bool applyAnimation(render::RenderScene& scene, float time, bool loop = true);
std::vector<Mat4> evaluateNodeWorlds(const render::RenderScene& scene);

} // namespace hbg::asset
