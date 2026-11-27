#pragma once

#include "core/bounds.h"
#include "core/math.h"

namespace hbg::render {

struct Camera;

struct EditorCameraInput {
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float scrollY = 0.0f;
    bool rightDown = false;
    bool middleDown = false;
    bool mouseCaptured = false;
};

// Proland TerrainViewController + BasicViewHandler style orbit camera. The
// camera looks at a ground target with spherical yaw/pitch and a distance;
// RMB rotates, MMB pans, and the wheel changes distance.
class EditorCamera {
  public:
    bool init(const Camera& camera, const Bounds& bounds);
    bool valid() const { return m_valid; }

    bool update(const EditorCameraInput& input, Camera& camera);

    const Vec3& target() const { return m_target; }
    float distance() const { return m_distance; }
    float yaw() const { return m_yaw; }
    float pitch() const { return m_pitch; }

  private:
    void apply(Camera& camera) const;
    Vec3 eye() const;
    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;

    Vec3 m_target{};
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_distance = 500.0f;
    float m_lastMouseX = 0.0f;
    float m_lastMouseY = 0.0f;
    bool m_prevRight = false;
    bool m_prevMiddle = false;
    bool m_valid = false;
};

} // namespace hbg::render
