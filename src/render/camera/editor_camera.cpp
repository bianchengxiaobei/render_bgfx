#include "render/camera/editor_camera.h"

#include "render/scene/render_scene.h"

#include <algorithm>
#include <cmath>

namespace hbg::render {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMaxPitch = 1.45f;
constexpr float kMinDistance = 2.0f;
constexpr float kMaxDistance = 50000.0f;

Vec3 translation(const Mat4& matrix) {
    return Vec3(matrix.values[12], matrix.values[13], matrix.values[14]);
}

} // namespace

bool EditorCamera::init(const Camera& camera, const Bounds& bounds) {
    m_valid = false;
    const Vec3 position = translation(camera.world);
    Vec3 direction = normalize(transformDirection(camera.world, Vec3(0.0f, 0.0f, -1.0f)));
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
        !std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z)) {
        return false;
    }

    const float groundY = bounds.valid() ? bounds.center().y : 0.0f;
    Vec3 target;
    bool targetValid = false;
    if (std::abs(direction.y) > 1.0e-4f) {
        const float distance = (groundY - position.y) / direction.y;
        if (std::isfinite(distance) && distance > 0.1f && distance < 1.0e6f) {
            target = position + direction * distance;
            targetValid = std::isfinite(target.x) && std::isfinite(target.y) && std::isfinite(target.z);
        }
    }
    if (!targetValid) {
        target = bounds.valid() ? bounds.center() : position + direction * 500.0f;
    }

    const Vec3 offset = position - target;
    const float rawDistance = length(offset);
    if (rawDistance <= 0.001f) {
        return false;
    }
    m_distance = std::clamp(rawDistance, kMinDistance, kMaxDistance);
    m_target = target;
    m_yaw = std::atan2(offset.x, offset.z);
    m_pitch = std::clamp(std::asin(std::clamp(offset.y / rawDistance, -1.0f, 1.0f)), -kMaxPitch, kMaxPitch);
    m_lastMouseX = 0.0f;
    m_lastMouseY = 0.0f;
    m_prevRight = false;
    m_prevMiddle = false;
    m_valid = true;
    return true;
}

bool EditorCamera::update(const EditorCameraInput& input, Camera& camera) {
    if (!m_valid) {
        return false;
    }

    const bool rightActive = input.rightDown && !input.mouseCaptured;
    const bool middleActive = input.middleDown && !input.mouseCaptured;
    const float dx = input.mouseX - m_lastMouseX;
    const float dy = input.mouseY - m_lastMouseY;
    bool changed = false;

    if (rightActive && m_prevRight) {
        m_yaw -= dx * 0.006f;
        m_pitch = std::clamp(m_pitch + dy * 0.006f, -kMaxPitch, kMaxPitch);
        if (m_yaw > kPi) {
            m_yaw -= 2.0f * kPi;
        } else if (m_yaw < -kPi) {
            m_yaw += 2.0f * kPi;
        }
        changed = true;
    }
    if (middleActive && m_prevMiddle) {
        const float scale = m_distance * 0.0015f;
        m_target = m_target - right() * (dx * scale) + up() * (dy * scale);
        changed = true;
    }
    if (!input.mouseCaptured && std::abs(input.scrollY) > 0.0f) {
        m_distance = std::clamp(m_distance * std::exp(-input.scrollY * 0.15f), kMinDistance, kMaxDistance);
        changed = true;
    }

    if (changed) {
        apply(camera);
    }

    m_lastMouseX = input.mouseX;
    m_lastMouseY = input.mouseY;
    m_prevRight = rightActive;
    m_prevMiddle = middleActive;
    return changed;
}

void EditorCamera::apply(Camera& camera) const {
    const Mat4 view = lookAtRH(eye(), m_target, Vec3(0.0f, 1.0f, 0.0f));
    camera.world = inverse(view);
    camera.nearClip = std::max(m_distance * 0.002f, 0.1f);
    camera.farClip = std::max(m_distance * 20.0f, 5000.0f);
}

Vec3 EditorCamera::eye() const {
    const float cosPitch = std::cos(m_pitch);
    const Vec3 offset(std::sin(m_yaw) * cosPitch, std::sin(m_pitch), std::cos(m_yaw) * cosPitch);
    return m_target + offset * m_distance;
}

Vec3 EditorCamera::forward() const {
    return normalize(eye() - m_target) * -1.0f;
}

Vec3 EditorCamera::right() const {
    return normalize(cross(forward(), Vec3(0.0f, 1.0f, 0.0f)));
}

Vec3 EditorCamera::up() const {
    return normalize(cross(right(), forward()));
}

} // namespace hbg::render
