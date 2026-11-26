#include "render/camera/view_state.h"

#include "render/scene/render_scene.h"

#include <algorithm>
#include <cmath>

namespace hbg::render {
namespace {

constexpr uint32_t kTaaSequenceLength = 1024;

Vec3 worldTranslation(const Mat4& matrix) {
    return Vec3(matrix.values[12], matrix.values[13], matrix.values[14]);
}

Vec4 combine(const Vec4& lhs, float lhsScale, const Vec4& rhs, float rhsScale) {
    return Vec4(lhs.x * lhsScale + rhs.x * rhsScale, lhs.y * lhsScale + rhs.y * rhsScale,
                lhs.z * lhsScale + rhs.z * rhsScale, lhs.w * lhsScale + rhs.w * rhsScale);
}

Vec4 normalizePlane(const Vec4& plane) {
    const float magnitude = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
    if (magnitude <= 1.0e-8f) {
        return plane;
    }
    const float inverseMagnitude = 1.0f / magnitude;
    return Vec4(plane.x * inverseMagnitude, plane.y * inverseMagnitude, plane.z * inverseMagnitude,
                plane.w * inverseMagnitude);
}

Vec4 matrixRow(const Mat4& matrix, std::size_t row) {
    return Vec4(matrix.values[row], matrix.values[4 + row], matrix.values[8 + row], matrix.values[12 + row]);
}

float radicalInverse(uint32_t index, uint32_t base) {
    float result = 0.0f;
    float fraction = 1.0f;
    while (index > 0) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

float halton(uint32_t index, uint32_t base) {
    return radicalInverse(index, base);
}

Mat4 makeProjection(const Camera& camera, float aspect, bool homogeneousDepth) {
    if (camera.projection == Camera::Projection::Orthographic) {
        return orthographicRH(-camera.xmag, camera.xmag, -camera.ymag, camera.ymag, camera.nearClip, camera.farClip,
                              homogeneousDepth);
    }
    return perspectiveRH(camera.yfov, aspect, camera.nearClip, camera.farClip, homogeneousDepth);
}

Mat4 jitterProjection(const Mat4& projection, float jitterX, float jitterY, uint16_t width, uint16_t height,
                      bool orthographic) {
    Mat4 result = projection;
    if (width == 0 || height == 0 || (jitterX == 0.0f && jitterY == 0.0f)) {
        return result;
    }

    const float offsetX = 2.0f * jitterX / static_cast<float>(width);
    const float offsetY = 2.0f * jitterY / static_cast<float>(height);
    if (orthographic) {
        result.values[12] += offsetX;
        result.values[13] += offsetY;
    } else {
        result.values[8] -= offsetX;
        result.values[9] -= offsetY;
    }
    return result;
}

std::array<Vec4, 6> extractFrustumPlanes(const Mat4& viewProj, const Vec3& cameraPosition, const Vec3& viewDirection,
                                         float nearClip, float farClip) {
    const Vec4 row0 = matrixRow(viewProj, 0);
    const Vec4 row1 = matrixRow(viewProj, 1);
    const Vec4 row3 = matrixRow(viewProj, 3);

    std::array<Vec4, 6> planes = {
        normalizePlane(combine(row3, 1.0f, row0, 1.0f)),
        normalizePlane(combine(row3, 1.0f, row0, -1.0f)),
        normalizePlane(combine(row3, 1.0f, row1, -1.0f)),
        normalizePlane(combine(row3, 1.0f, row1, 1.0f)),
        Vec4(viewDirection.x, viewDirection.y, viewDirection.z, -dot(viewDirection, cameraPosition) - nearClip),
        Vec4(-viewDirection.x, -viewDirection.y, -viewDirection.z, dot(viewDirection, cameraPosition) + farClip),
    };
    return planes;
}

} // namespace

void ViewState::reset() {
    m_history.reset();
    m_constants = {};
    m_resolution = {};
    m_taaJitter = {};
    m_taaFrameInfo = {};
    m_rtScale = {};
    m_dynamicResolutionScale = {};
    m_cameraPosition = {};
    m_cameraForward = Vec3(0.0f, 0.0f, -1.0f);
    m_frameIndex = 0;
    m_taaFrameIndex = 0;
    m_jitterEnabled = false;
}

void ViewState::resetHistory() {
    m_history.reset();
    m_taaFrameIndex = 0;
}

void ViewState::setJitterScale(float scale) {
    const float nextScale = std::max(scale, 0.0f);
    if (std::fabs(nextScale - m_jitterScale) <= 1.0e-6f) {
        return;
    }
    m_jitterScale = nextScale;
    m_history.reset();
    m_taaFrameIndex = 0;
}

void ViewState::update(const Camera& camera, uint16_t targetWidth, uint16_t targetHeight, float renderScale,
                       bool jitterEnabled, bool homogeneousDepth, bool preserveHistoryOnScaleChange) {
    const ViewResolution nextResolution = makeViewResolution(targetWidth, targetHeight, renderScale);
    const bool targetChanged = nextResolution.targetWidth != m_resolution.targetWidth ||
                               nextResolution.targetHeight != m_resolution.targetHeight;
    const bool renderSizeChanged = nextResolution.renderWidth != m_resolution.renderWidth ||
                                   nextResolution.renderHeight != m_resolution.renderHeight;
    const bool aspectChanged =
        m_resolution.renderWidth > 0 && m_resolution.renderHeight > 0 && nextResolution.renderHeight > 0 &&
        std::fabs(static_cast<float>(nextResolution.renderWidth) / static_cast<float>(nextResolution.renderHeight) -
                  static_cast<float>(m_resolution.renderWidth) / static_cast<float>(m_resolution.renderHeight)) >
            1.0e-6f;
    const bool jitterChanged = jitterEnabled != m_jitterEnabled;
    const bool preserveScaleHistory = preserveHistoryOnScaleChange && !targetChanged && !aspectChanged &&
                                      nextResolution.renderWidth > 0 && nextResolution.renderHeight > 0;
    if (targetChanged || (renderSizeChanged && !preserveScaleHistory) || jitterChanged) {
        m_history.reset();
        m_taaFrameIndex = 0;
    }

    m_resolution = nextResolution;
    m_jitterEnabled = jitterEnabled;
    if (m_jitterEnabled) {
        m_taaFrameIndex = (m_taaFrameIndex + 1) & (kTaaSequenceLength - 1);
    } else {
        m_taaFrameIndex = 0;
    }

    const float aspect = m_resolution.renderHeight > 0 ? static_cast<float>(m_resolution.renderWidth) /
                                                             static_cast<float>(m_resolution.renderHeight)
                                                       : 1.0f;
    const Mat4 view = inverse(camera.world);
    const Mat4 nonJitteredProj = makeProjection(camera, aspect, homogeneousDepth);

    float jitterX = 0.0f;
    float jitterY = 0.0f;
    if (m_jitterEnabled) {
        jitterX = (halton(m_taaFrameIndex + 1, 2) - 0.5f) * m_jitterScale;
        jitterY = (halton(m_taaFrameIndex + 1, 3) - 0.5f) * m_jitterScale;
    }
    if (m_resolution.renderWidth > 0 && m_resolution.renderHeight > 0) {
        m_taaJitter = Vec4(jitterX, jitterY, jitterX / static_cast<float>(m_resolution.renderWidth),
                           jitterY / static_cast<float>(m_resolution.renderHeight));
    } else {
        m_taaJitter = {};
    }

    const Mat4 proj =
        jitterProjection(nonJitteredProj, jitterX, jitterY, m_resolution.renderWidth, m_resolution.renderHeight,
                         camera.projection == Camera::Projection::Orthographic);

    Snapshot snapshot;
    snapshot.view = view;
    snapshot.proj = proj;
    snapshot.nonJitteredProj = nonJitteredProj;
    snapshot.viewProj = proj * view;
    snapshot.nonJitteredViewProj = nonJitteredProj * view;
    snapshot.cameraPosition = worldTranslation(camera.world);
    snapshot.renderWidth = m_resolution.renderWidth;
    snapshot.renderHeight = m_resolution.renderHeight;
    m_history.push(snapshot);

    rebuildConstants(camera);

    m_cameraPosition = snapshot.cameraPosition;
    m_cameraForward = normalize(transformDirection(camera.world, Vec3(0.0f, 0.0f, -1.0f)));
    m_dynamicResolutionScale = m_resolution.dynamicResolutionScale();

    const Snapshot& previous = m_history.previous();
    m_rtScale = Vec4(m_resolution.targetWidth > 0
                         ? static_cast<float>(m_resolution.renderWidth) / static_cast<float>(m_resolution.targetWidth)
                         : 0.0f,
                     m_resolution.targetHeight > 0
                         ? static_cast<float>(m_resolution.renderHeight) / static_cast<float>(m_resolution.targetHeight)
                         : 0.0f,
                     m_resolution.targetWidth > 0
                         ? static_cast<float>(previous.renderWidth) / static_cast<float>(m_resolution.targetWidth)
                         : 0.0f,
                     m_resolution.targetHeight > 0
                         ? static_cast<float>(previous.renderHeight) / static_cast<float>(m_resolution.targetHeight)
                         : 0.0f);
    m_taaFrameInfo = Vec4(0.0f, 0.0f, static_cast<float>(m_taaFrameIndex), m_jitterEnabled ? 1.0f : 0.0f);

    ++m_frameIndex;
}

void ViewState::rebuildConstants(const Camera& camera) {
    const Snapshot& current = m_history.current();
    const Snapshot& previous = m_history.previous();

    m_constants.view = current.view;
    m_constants.invView = inverse(current.view);
    m_constants.proj = current.proj;
    m_constants.nonJitteredProj = current.nonJitteredProj;
    m_constants.invProj = inverse(current.proj);
    m_constants.viewProj = current.viewProj;
    m_constants.invViewProj = inverse(current.viewProj);
    m_constants.nonJitteredViewProj = current.nonJitteredViewProj;
    m_constants.nonJitteredInvViewProj = inverse(current.nonJitteredViewProj);
    m_constants.prevView = previous.view;
    m_constants.prevProj = previous.nonJitteredProj;
    m_constants.prevViewProj = previous.nonJitteredViewProj;
    m_constants.prevInvViewProj = inverse(previous.nonJitteredViewProj);
    m_constants.worldSpaceCameraPos = current.cameraPosition;
    m_constants.prevWorldSpaceCameraPos = previous.cameraPosition;
    m_constants.frustumPlanes = extractFrustumPlanes(
        current.nonJitteredViewProj, current.cameraPosition,
        normalize(transformDirection(camera.world, Vec3(0.0f, 0.0f, -1.0f))), camera.nearClip, camera.farClip);
}

} // namespace hbg::render
