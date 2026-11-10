#pragma once

#include "core/math.h"
#include "render/camera/history_buffer.h"
#include "render/camera/resolution_group.h"

#include <array>
#include <cstdint>

namespace hbg::render {

struct Camera;

struct ViewConstants {
    Mat4 view = Mat4::identity();
    Mat4 invView = Mat4::identity();
    Mat4 proj = Mat4::identity();
    Mat4 nonJitteredProj = Mat4::identity();
    Mat4 invProj = Mat4::identity();
    Mat4 viewProj = Mat4::identity();
    Mat4 invViewProj = Mat4::identity();
    Mat4 nonJitteredViewProj = Mat4::identity();
    Mat4 nonJitteredInvViewProj = Mat4::identity();
    Mat4 prevView = Mat4::identity();
    Mat4 prevProj = Mat4::identity();
    Mat4 prevViewProj = Mat4::identity();
    Mat4 prevInvViewProj = Mat4::identity();
    Vec3 worldSpaceCameraPos{};
    Vec3 prevWorldSpaceCameraPos{};
    std::array<Vec4, 6> frustumPlanes{};
};

class ViewState {
  public:
    ViewState() = default;

    void reset();
    void resetHistory();
    void update(const Camera& camera, uint16_t targetWidth, uint16_t targetHeight, float renderScale,
                bool jitterEnabled, bool homogeneousDepth);

    const ViewConstants& constants() const { return m_constants; }
    const ViewResolution& resolution() const { return m_resolution; }
    const Vec4& taaJitter() const { return m_taaJitter; }
    const Vec4& taaFrameInfo() const { return m_taaFrameInfo; }
    const Vec4& rtScale() const { return m_rtScale; }
    const Vec4& dynamicResolutionScale() const { return m_dynamicResolutionScale; }
    Vec4 screenSize() const { return m_resolution.screenSize(ResolutionGroup::BeforeDynamicResUpscale); }
    Vec3 cameraPosition() const { return m_cameraPosition; }
    Vec3 cameraForward() const { return m_cameraForward; }
    uint16_t targetWidth() const { return m_resolution.targetWidth; }
    uint16_t targetHeight() const { return m_resolution.targetHeight; }
    uint16_t renderWidth() const { return m_resolution.renderWidth; }
    uint16_t renderHeight() const { return m_resolution.renderHeight; }
    uint32_t frameIndex() const { return m_frameIndex; }
    uint32_t taaFrameIndex() const { return m_taaFrameIndex; }
    bool jitterEnabled() const { return m_jitterEnabled; }
    bool historyValid() const { return m_history.hasHistory(); }

    void setJitterScale(float scale);

  private:
    struct Snapshot {
        Mat4 view = Mat4::identity();
        Mat4 proj = Mat4::identity();
        Mat4 nonJitteredProj = Mat4::identity();
        Mat4 viewProj = Mat4::identity();
        Mat4 nonJitteredViewProj = Mat4::identity();
        Vec3 cameraPosition{};
        uint16_t renderWidth = 0;
        uint16_t renderHeight = 0;
    };

    void rebuildConstants(const Camera& camera);

    HistoryBuffer<Snapshot> m_history;
    ViewConstants m_constants;
    ViewResolution m_resolution;
    Vec4 m_taaJitter{};
    Vec4 m_taaFrameInfo{};
    Vec4 m_rtScale{};
    Vec4 m_dynamicResolutionScale{};
    Vec3 m_cameraPosition{};
    Vec3 m_cameraForward{0.0f, 0.0f, -1.0f};
    uint32_t m_frameIndex = 0;
    uint32_t m_taaFrameIndex = 0;
    float m_jitterScale = 1.0f;
    bool m_jitterEnabled = false;
};

} // namespace hbg::render
