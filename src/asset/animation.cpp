#include "asset/animation.h"

#include "render/scene/render_scene.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace hbg::asset {
namespace {

Vec4 normalizeQuat(const Vec4& value) {
    const float magnitude = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
    if (magnitude <= 1.0e-8f) {
        return Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    const float inverseMagnitude = 1.0f / magnitude;
    return Vec4(value.x * inverseMagnitude, value.y * inverseMagnitude, value.z * inverseMagnitude,
                value.w * inverseMagnitude);
}

Vec4 slerp(const Vec4& lhs, const Vec4& rhs, float alpha) {
    Vec4 from = normalizeQuat(lhs);
    Vec4 to = normalizeQuat(rhs);
    float cosine = from.x * to.x + from.y * to.y + from.z * to.z + from.w * to.w;
    if (cosine < 0.0f) {
        to = Vec4(-to.x, -to.y, -to.z, -to.w);
        cosine = -cosine;
    }
    if (cosine > 0.9995f) {
        return normalizeQuat(Vec4(from.x + (to.x - from.x) * alpha, from.y + (to.y - from.y) * alpha,
                                  from.z + (to.z - from.z) * alpha, from.w + (to.w - from.w) * alpha));
    }
    const float theta = std::acos(std::clamp(cosine, -1.0f, 1.0f));
    const float sinTheta = std::sin(theta);
    if (std::abs(sinTheta) <= 1.0e-8f) {
        return from;
    }
    const float weightFrom = std::sin((1.0f - alpha) * theta) / sinTheta;
    const float weightTo = std::sin(alpha * theta) / sinTheta;
    return normalizeQuat(Vec4(from.x * weightFrom + to.x * weightTo, from.y * weightFrom + to.y * weightTo,
                              from.z * weightFrom + to.z * weightTo, from.w * weightFrom + to.w * weightTo));
}

Mat4 composeTrs(const Vec3& translation, const Vec4& rotation, const Vec3& scale) {
    const float qx = rotation.x;
    const float qy = rotation.y;
    const float qz = rotation.z;
    const float qw = rotation.w;

    Mat4 matrix{};
    matrix[0] = (1.0f - 2.0f * qy * qy - 2.0f * qz * qz) * scale.x;
    matrix[1] = (2.0f * qx * qy + 2.0f * qz * qw) * scale.x;
    matrix[2] = (2.0f * qx * qz - 2.0f * qy * qw) * scale.x;
    matrix[3] = 0.0f;

    matrix[4] = (2.0f * qx * qy - 2.0f * qz * qw) * scale.y;
    matrix[5] = (1.0f - 2.0f * qx * qx - 2.0f * qz * qz) * scale.y;
    matrix[6] = (2.0f * qy * qz + 2.0f * qx * qw) * scale.y;
    matrix[7] = 0.0f;

    matrix[8] = (2.0f * qx * qz + 2.0f * qy * qw) * scale.z;
    matrix[9] = (2.0f * qy * qz - 2.0f * qx * qw) * scale.z;
    matrix[10] = (1.0f - 2.0f * qx * qx - 2.0f * qy * qy) * scale.z;
    matrix[11] = 0.0f;

    matrix[12] = translation.x;
    matrix[13] = translation.y;
    matrix[14] = translation.z;
    matrix[15] = 1.0f;
    return matrix;
}

Vec4 sampleChannel(const AnimationChannel& channel, float time) {
    if (channel.times.empty() || channel.values.empty()) {
        return Vec4{};
    }
    if (time <= channel.times.front()) {
        return channel.values.front();
    }
    if (time >= channel.times.back()) {
        return channel.values.back();
    }

    const auto upper = std::upper_bound(channel.times.begin(), channel.times.end(), time);
    const std::size_t index1 = static_cast<std::size_t>(upper - channel.times.begin());
    const std::size_t index0 = index1 - 1u;
    const Vec4& value0 = channel.values[std::min(index0, channel.values.size() - 1u)];
    const Vec4& value1 = channel.values[std::min(index1, channel.values.size() - 1u)];
    if (channel.interpolation == AnimationInterpolation::Step) {
        return value0;
    }

    const float time0 = channel.times[index0];
    const float time1 = channel.times[index1];
    const float span = time1 - time0;
    const float alpha = span > 1.0e-8f ? std::clamp((time - time0) / span, 0.0f, 1.0f) : 0.0f;
    if (channel.interpolation == AnimationInterpolation::CubicSpline &&
        channel.inTangents.size() == channel.values.size() &&
        channel.outTangents.size() == channel.values.size()) {
        const Vec4& tangent0 = channel.outTangents[index0];
        const Vec4& tangent1 = channel.inTangents[index1];
        const float alpha2 = alpha * alpha;
        const float alpha3 = alpha2 * alpha;
        const float h00 = 2.0f * alpha3 - 3.0f * alpha2 + 1.0f;
        const float h10 = alpha3 - 2.0f * alpha2 + alpha;
        const float h01 = -2.0f * alpha3 + 3.0f * alpha2;
        const float h11 = alpha3 - alpha2;
        const Vec4 result(tangent0.x * span * h10 + value0.x * h00 + value1.x * h01 + tangent1.x * span * h11,
                          tangent0.y * span * h10 + value0.y * h00 + value1.y * h01 + tangent1.y * span * h11,
                          tangent0.z * span * h10 + value0.z * h00 + value1.z * h01 + tangent1.z * span * h11,
                          tangent0.w * span * h10 + value0.w * h00 + value1.w * h01 + tangent1.w * span * h11);
        return channel.path == AnimationPath::Rotation ? normalizeQuat(result) : result;
    }
    if (channel.path == AnimationPath::Rotation) {
        return slerp(value0, value1, alpha);
    }
    return Vec4(value0.x + (value1.x - value0.x) * alpha, value0.y + (value1.y - value0.y) * alpha,
                value0.z + (value1.z - value0.z) * alpha, value0.w + (value1.w - value0.w) * alpha);
}

float clipTime(float time, float duration, bool loop) {
    if (duration <= 0.0f) {
        return 0.0f;
    }
    if (!loop) {
        return std::clamp(time, 0.0f, duration);
    }
    float local = std::fmod(time, duration);
    if (local < 0.0f) {
        local += duration;
    }
    return local;
}

} // namespace

std::vector<Mat4> evaluateNodeWorlds(const render::RenderScene& scene) {
    std::vector<Mat4> worlds(scene.nodes.size(), Mat4::identity());
    std::vector<uint8_t> state(scene.nodes.size(), 0);
    const auto evaluateWorld = [&](const auto& self, uint32_t index) -> Mat4 {
        if (index >= scene.nodes.size()) {
            return Mat4::identity();
        }
        if (state[index] == 2u) {
            return worlds[index];
        }
        if (state[index] == 1u) {
            return Mat4::identity();
        }
        state[index] = 1u;
        const SceneNode& node = scene.nodes[index];
        const Mat4 local = node.hasMatrix ? node.matrix : composeTrs(node.translation, node.rotation, node.scale);
        const Mat4 world = node.parent >= 0 ? self(self, static_cast<uint32_t>(node.parent)) * local : local;
        worlds[index] = world;
        state[index] = 2u;
        return world;
    };

    for (uint32_t index = 0; index < scene.nodes.size(); ++index) {
        evaluateWorld(evaluateWorld, index);
    }
    return worlds;
}

bool applyAnimation(render::RenderScene& scene, float time, bool loop) {
    if (scene.nodes.empty() || scene.animations.empty()) {
        return false;
    }

    for (SceneNode& node : scene.nodes) {
        node.weights = node.baseWeights;
        if (node.hasMatrix) {
            continue;
        }
        node.translation = node.baseTranslation;
        node.rotation = node.baseRotation;
        node.scale = node.baseScale;
    }

    bool applied = false;
    for (const AnimationClip& clip : scene.animations) {
        if (clip.channels.empty() || clip.duration <= 0.0f) {
            continue;
        }
        const float local = clipTime(time, clip.duration, loop);
        for (const AnimationChannel& channel : clip.channels) {
            if (channel.node >= scene.nodes.size()) {
                continue;
            }
            SceneNode& node = scene.nodes[channel.node];
            if (node.hasMatrix && channel.path != AnimationPath::Weights) {
                continue;
            }
            const Vec4 value = sampleChannel(channel, local);
            switch (channel.path) {
            case AnimationPath::Translation:
                node.translation = Vec3(value.x, value.y, value.z);
                break;
            case AnimationPath::Rotation:
                node.rotation = normalizeQuat(value);
                break;
            case AnimationPath::Scale:
                node.scale = Vec3(value.x, value.y, value.z);
                break;
            case AnimationPath::Weights:
                if (channel.targetIndex < node.weights.size()) {
                    node.weights[channel.targetIndex] = value.x;
                }
                break;
            }
            applied = true;
        }
    }
    if (!applied) {
        return false;
    }

    const std::vector<Mat4> worlds = evaluateNodeWorlds(scene);
    for (render::RenderObject& object : scene.objects) {
        if (object.nodeIndex < 0 || static_cast<std::size_t>(object.nodeIndex) >= worlds.size()) {
            continue;
        }
        const SceneNode& node = scene.nodes[static_cast<std::size_t>(object.nodeIndex)];
        if (object.morphWeights.size() == node.weights.size()) {
            object.morphWeights = node.weights;
        }
        object.prevWorld = object.world;
        object.world = worlds[static_cast<std::size_t>(object.nodeIndex)];
        if (object.meshIndex < scene.meshes.size()) {
            object.worldBounds = object.skinIndex >= 0 ? scene.meshes[object.meshIndex].bounds
                                                        : transformBounds(object.world, scene.meshes[object.meshIndex].bounds);
        }
    }
    for (render::Camera& camera : scene.cameras) {
        if (camera.nodeIndex >= 0 && static_cast<std::size_t>(camera.nodeIndex) < worlds.size()) {
            camera.world = worlds[static_cast<std::size_t>(camera.nodeIndex)];
        }
    }
    for (render::Light& light : scene.lights) {
        if (light.nodeIndex >= 0 && static_cast<std::size_t>(light.nodeIndex) < worlds.size()) {
            light.world = worlds[static_cast<std::size_t>(light.nodeIndex)];
        }
    }
    scene.recomputeBounds();
    return true;
}

} // namespace hbg::asset
