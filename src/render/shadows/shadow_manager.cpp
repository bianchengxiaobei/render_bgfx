#include "render/shadows/shadow_manager.h"

#include "core/log.h"
#include "gfx/encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace hbg::render {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDirectionalDepthBias = 0.01f;
constexpr float kDirectionalNormalBias = 0.2f;
constexpr float kPointDepthBias = 0.01f;
constexpr float kPointNormalBias = 0.2f;
constexpr float kSpotDepthBias = 0.003f;
constexpr float kSpotNormalBias = 0.05f;

Mat4 biasCrop(bool originBottomLeft, bool homogeneousDepth) {
    Mat4 crop = Mat4::identity();
    crop.values[0] = 0.5f;
    crop.values[5] = originBottomLeft ? 0.5f : -0.5f;
    crop.values[10] = homogeneousDepth ? 0.5f : 1.0f;
    crop.values[12] = 0.5f;
    crop.values[13] = 0.5f;
    crop.values[14] = homogeneousDepth ? 0.5f : 0.0f;
    return crop;
}

Vec3 worldTranslation(const Mat4& matrix) {
    return Vec3(matrix.values[12], matrix.values[13], matrix.values[14]);
}

Vec3 lightUp(const Vec3& direction) {
    if (std::abs(dot(direction, Vec3(0.0f, 1.0f, 0.0f))) > 0.95f) {
        return Vec3(0.0f, 0.0f, 1.0f);
    }
    return Vec3(0.0f, 1.0f, 0.0f);
}

std::array<Vec3, 8> cameraSliceCorners(const ViewConstants& view, const Camera& camera, float nearPlane,
                                       float farPlane) {
    std::array<Vec3, 8> corners{};
    const float aspect = camera.aspect > 0.0f ? camera.aspect : 1.7777778f;
    const float tanHalfFov = std::tan(camera.yfov * 0.5f);
    uint32_t index = 0;
    for (float distance : {nearPlane, farPlane}) {
        for (float y : {-1.0f, 1.0f}) {
            for (float x : {-1.0f, 1.0f}) {
                Vec3 viewPosition;
                if (camera.projection == Camera::Projection::Perspective) {
                    const float halfHeight = distance * tanHalfFov;
                    const float halfWidth = halfHeight * aspect;
                    viewPosition = Vec3(x * halfWidth, y * halfHeight, -distance);
                } else {
                    viewPosition = Vec3(x * camera.xmag, y * camera.ymag, -distance);
                }
                corners[index++] = transformPoint(view.invView, viewPosition);
            }
        }
    }
    return corners;
}

bool fitDirectionalRequest(const ViewConstants& view, const Camera& camera, const Light& light, float nearZ, float farZ,
                           bool homogeneousDepth, bool originBottomLeft, Mat4& outView, Mat4& outProjection,
                           Mat4& outWorldToShadow) {
    if (!(farZ > nearZ)) {
        return false;
    }
    const std::array<Vec3, 8> corners = cameraSliceCorners(view, camera, nearZ, farZ);
    Vec3 center(0.0f, 0.0f, 0.0f);
    for (const Vec3& corner : corners) {
        center = center + corner;
    }
    center = center * (1.0f / static_cast<float>(corners.size()));

    float radius = 0.0f;
    for (const Vec3& corner : corners) {
        radius = std::max(radius, length(corner - center));
    }
    radius = std::max(radius, 0.1f);

    const Vec3 lightDirection = normalize(transformDirection(light.world, Vec3(0.0f, 0.0f, 1.0f)));
    const Vec3 eye = center + lightDirection * (radius * 2.0f + 1.0f);
    const Mat4 lightView = lookAtRH(eye, center, lightUp(lightDirection));

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    float maxZ = std::numeric_limits<float>::lowest();
    for (const Vec3& corner : corners) {
        const Vec3 lightSpace = transformPoint(lightView, corner);
        minX = std::min(minX, lightSpace.x);
        minY = std::min(minY, lightSpace.y);
        minZ = std::min(minZ, lightSpace.z);
        maxX = std::max(maxX, lightSpace.x);
        maxY = std::max(maxY, lightSpace.y);
        maxZ = std::max(maxZ, lightSpace.z);
    }

    const float margin = std::max(radius * 0.025f, 0.01f);
    const float left = minX - margin;
    const float right = maxX + margin;
    const float bottom = minY - margin;
    const float top = maxY + margin;
    const float nearPlane = std::max(0.01f, -maxZ - margin);
    const float farPlane = std::max(nearPlane + 0.01f, -minZ + margin);
    if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(bottom) || !std::isfinite(top) ||
        !std::isfinite(nearPlane) || !std::isfinite(farPlane)) {
        return false;
    }

    outView = lightView;
    outProjection = orthographicRH(left, right, bottom, top, nearPlane, farPlane, homogeneousDepth);
    outWorldToShadow = biasCrop(originBottomLeft, homogeneousDepth) * (outProjection * outView);
    return true;
}

bool makePerspectiveRequest(const Vec3& position, const Vec3& direction, const Vec3& up, float fov, float nearPlane,
                            float farPlane, bool homogeneousDepth, bool originBottomLeft, ShadowRequest& request) {
    if (!(farPlane > nearPlane) || !(fov > 0.0f)) {
        return false;
    }
    const Mat4 view = lookAtRH(position, position + direction, up);
    const Mat4 projection = perspectiveRH(fov, 1.0f, nearPlane, farPlane, homogeneousDepth);
    request.view = view;
    request.projection = projection;
    request.viewProj = projection * view;
    request.worldToShadow = biasCrop(originBottomLeft, homogeneousDepth) * request.viewProj;
    request.lightPosition = position;
    request.lightDirection = direction;
    request.enabled = true;
    return true;
}

} // namespace

bool ShadowManager::init(gfx::ResourceManager& resources, uint16_t size) {
    m_resources = &resources;
    m_directionalAtlasSize = size > 0 ? size : 2048;
    m_punctualAtlasSize = size > 0 ? size : 2048;

    gfx::TextureDesc directionalDesc;
    directionalDesc.width = m_directionalAtlasSize;
    directionalDesc.height = m_directionalAtlasSize;
    directionalDesc.format = gfx::TextureFormat::D24S8;
    directionalDesc.depthStencil = true;
    directionalDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT | BGFX_SAMPLER_COMPARE_LEQUAL;
    directionalDesc.name = "shadow.directionalAtlas";
    m_directionalTexture = resources.createTexture(directionalDesc);
    if (!m_directionalTexture.valid()) {
        HBG_LOGE("shadow manager: failed to create directional atlas {}x{}", m_directionalAtlasSize,
                 m_directionalAtlasSize);
        return false;
    }

    gfx::TextureDesc punctualDesc = directionalDesc;
    punctualDesc.name = "shadow.punctualAtlas";
    m_punctualTexture = resources.createTexture(punctualDesc);
    if (!m_punctualTexture.valid()) {
        HBG_LOGE("shadow manager: failed to create punctual atlas {}x{}", m_punctualAtlasSize, m_punctualAtlasSize);
        resources.destroyTexture(m_directionalTexture);
        m_directionalTexture = {};
        return false;
    }

    gfx::TextureDesc contextDesc;
    contextDesc.width = static_cast<uint16_t>(kShadowContextTexels);
    contextDesc.height = static_cast<uint16_t>(kMaxShadowRequests);
    contextDesc.format = gfx::TextureFormat::RGBA32F;
    contextDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    contextDesc.name = "shadow.context";
    m_contextTexture = resources.createTexture(contextDesc);
    if (!m_contextTexture.valid()) {
        HBG_LOGE("shadow manager: failed to create shadow context texture");
        resources.destroyTexture(m_directionalTexture);
        resources.destroyTexture(m_punctualTexture);
        m_directionalTexture = {};
        m_punctualTexture = {};
        return false;
    }

    invalidate();
    return true;
}

void ShadowManager::shutdown(gfx::ResourceManager& resources) {
    invalidate();
    if (m_directionalTexture.valid()) {
        resources.destroyTexture(m_directionalTexture);
        m_directionalTexture = {};
    }
    if (m_punctualTexture.valid()) {
        resources.destroyTexture(m_punctualTexture);
        m_punctualTexture = {};
    }
    if (m_contextTexture.valid()) {
        resources.destroyTexture(m_contextTexture);
        m_contextTexture = {};
    }
    m_resources = nullptr;
}

void ShadowManager::invalidate() {
    m_requests.clear();
    m_contextData = {};
    m_cascadeSplits = {};
    m_directionalRequestCount = 0;
    m_punctualRequestCount = 0;
    m_shadowedLightCount = 0;
    resetPacking();
}

void ShadowManager::resetPacking() {
    m_punctualCursorX = 0;
    m_punctualCursorY = 0;
    m_punctualRowHeight = 0;
}

bool ShadowManager::update(const RenderScene& scene, const ViewConstants& view, const Camera& camera,
                           LightBuilder& lightBuilder) {
    invalidate();
    const bool homogeneousDepth = bgfx::getCaps() != nullptr && bgfx::getCaps()->homogeneousDepth;
    const bool originBottomLeft = bgfx::getCaps() != nullptr && bgfx::getCaps()->originBottomLeft;

    const auto& selectedLights = lightBuilder.selectedLights();
    for (uint32_t index = 0; index < selectedLights.size(); ++index) {
        const Light& light = *selectedLights[index];
        if (!light.castShadows || light.intensity <= 0.0f) {
            continue;
        }
        if (light.type == LightType::Directional) {
            buildDirectionalRequests(scene, view, camera, light, index, lightBuilder, homogeneousDepth,
                                     originBottomLeft);
        } else {
            buildPunctualRequest(light, index, lightBuilder, homogeneousDepth, originBottomLeft);
        }
    }

    buildContextData();
    uploadContext();
    lightBuilder.setShadowedCount(m_shadowedLightCount);
    return valid();
}

bool ShadowManager::buildDirectionalRequests(const RenderScene& scene, const ViewConstants& view, const Camera& camera,
                                             const Light& light, uint32_t lightIndex, LightBuilder& lightBuilder,
                                             bool homogeneousDepth, bool originBottomLeft) {
    const float sceneRadius = scene.bounds.valid() ? std::max(length(scene.bounds.extents()), 1.0f) : 10.0f;
    const float shadowDistance =
        std::min(camera.farClip, std::max(camera.nearClip + 1.0f, std::min(sceneRadius * 8.0f, 200.0f)));
    if (shadowDistance <= camera.nearClip + 0.01f) {
        return false;
    }

    constexpr std::array<float, kDirectionalCascadeCount> kSplitRatios = {0.10f, 0.25f, 0.50f, 1.0f};
    const uint16_t cascadeSize = static_cast<uint16_t>(m_directionalAtlasSize / 2);
    if (cascadeSize == 0) {
        return false;
    }

    std::array<ShadowRequest, kDirectionalCascadeCount> requests{};
    std::array<float, kDirectionalCascadeCount> splits{};
    float previousFar = camera.nearClip;
    for (uint32_t cascade = 0; cascade < kDirectionalCascadeCount; ++cascade) {
        const float farZ = camera.nearClip + (shadowDistance - camera.nearClip) * kSplitRatios[cascade];
        ShadowRequest& request = requests[cascade];
        if (!fitDirectionalRequest(view, camera, light, previousFar, farZ, homogeneousDepth, originBottomLeft,
                                   request.view, request.projection, request.worldToShadow)) {
            return false;
        }
        request.enabled = true;
        request.type = ShadowRequestType::Directional;
        request.atlas = ShadowAtlasType::Directional;
        request.lightIndex = lightIndex;
        request.faceIndex = cascade;
        request.atlasSize = m_directionalAtlasSize;
        request.viewportX = static_cast<uint16_t>((cascade % 2u) * cascadeSize);
        request.viewportY = static_cast<uint16_t>((cascade / 2u) * cascadeSize);
        request.viewportWidth = cascadeSize;
        request.viewportHeight = cascadeSize;
        request.viewProj = request.projection * request.view;
        request.lightDirection = normalize(transformDirection(light.world, Vec3(0.0f, 0.0f, 1.0f)));
        request.depthBias = kDirectionalDepthBias;
        request.normalBias = kDirectionalNormalBias;
        splits[cascade] = farZ;
        previousFar = farZ;
    }

    const uint32_t base = static_cast<uint32_t>(m_requests.size());
    for (uint32_t cascade = 0; cascade < kDirectionalCascadeCount; ++cascade) {
        requests[cascade].requestIndex = base + cascade;
        m_requests.push_back(requests[cascade]);
    }
    m_cascadeSplits = Vec4(splits[0], splits[1], splits[2], splits[3]);
    m_directionalRequestCount = kDirectionalCascadeCount;
    ++m_shadowedLightCount;
    lightBuilder.setShadowIndex(lightIndex, static_cast<int32_t>(base));
    return true;
}

bool ShadowManager::buildPunctualRequest(const Light& light, uint32_t lightIndex, LightBuilder& lightBuilder,
                                         bool homogeneousDepth, bool originBottomLeft) {
    const Vec3 position = worldTranslation(light.world);
    const float range = std::max(light.range, 0.01f);
    const float nearPlane = std::max(0.01f, range * 0.01f);

    if (light.type == LightType::Point) {
        constexpr std::array<Vec3, 6> kDirections = {
            Vec3(1.0f, 0.0f, 0.0f),  Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f),
            Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f),  Vec3(0.0f, 0.0f, -1.0f),
        };
        constexpr std::array<Vec3, 6> kUps = {
            Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f),
            Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f),
        };
        if (m_requests.size() + kDirections.size() > kMaxShadowRequests) {
            return false;
        }

        const uint16_t savedX = m_punctualCursorX;
        const uint16_t savedY = m_punctualCursorY;
        const uint16_t savedRowHeight = m_punctualRowHeight;
        const std::size_t savedRequests = m_requests.size();
        const uint32_t base = static_cast<uint32_t>(m_requests.size());
        for (uint32_t face = 0; face < kDirections.size(); ++face) {
            ShadowRequest request;
            if (!makePerspectiveRequest(position, kDirections[face], kUps[face], kPi * 0.5f, nearPlane, range,
                                        homogeneousDepth, originBottomLeft, request)) {
                m_punctualCursorX = savedX;
                m_punctualCursorY = savedY;
                m_punctualRowHeight = savedRowHeight;
                m_requests.resize(savedRequests);
                return false;
            }
            request.type = ShadowRequestType::PointFace;
            request.atlas = ShadowAtlasType::Punctual;
            request.lightIndex = lightIndex;
            request.faceIndex = face;
            request.atlasSize = m_punctualAtlasSize;
            request.depthBias = kPointDepthBias;
            request.normalBias = kPointNormalBias;
            if (!packPunctual(static_cast<uint16_t>(kPointShadowResolution),
                              static_cast<uint16_t>(kPointShadowResolution), request)) {
                m_punctualCursorX = savedX;
                m_punctualCursorY = savedY;
                m_punctualRowHeight = savedRowHeight;
                m_requests.resize(savedRequests);
                return false;
            }
            request.requestIndex = static_cast<uint32_t>(m_requests.size());
            m_requests.push_back(request);
        }
        m_punctualRequestCount += static_cast<uint32_t>(kDirections.size());
        ++m_shadowedLightCount;
        lightBuilder.setShadowIndex(lightIndex, static_cast<int32_t>(base));
        return true;
    }

    if (light.type == LightType::Spot) {
        if (m_requests.size() + 1 > kMaxShadowRequests) {
            return false;
        }
        const Vec3 direction = normalize(transformDirection(light.world, Vec3(0.0f, 0.0f, -1.0f)));
        const float fov = std::clamp(light.outerConeAngle * 2.0f, 0.01f, kPi - 0.01f);
        ShadowRequest request;
        if (!makePerspectiveRequest(position, direction, lightUp(direction), fov, nearPlane, range, homogeneousDepth,
                                    originBottomLeft, request)) {
            return false;
        }
        request.type = ShadowRequestType::Spot;
        request.atlas = ShadowAtlasType::Punctual;
        request.lightIndex = lightIndex;
        request.faceIndex = 0;
        request.atlasSize = m_punctualAtlasSize;
        request.depthBias = kSpotDepthBias;
        request.normalBias = kSpotNormalBias;
        const uint16_t savedX = m_punctualCursorX;
        const uint16_t savedY = m_punctualCursorY;
        const uint16_t savedRowHeight = m_punctualRowHeight;
        if (!packPunctual(static_cast<uint16_t>(kSpotShadowResolution), static_cast<uint16_t>(kSpotShadowResolution),
                          request)) {
            m_punctualCursorX = savedX;
            m_punctualCursorY = savedY;
            m_punctualRowHeight = savedRowHeight;
            return false;
        }
        request.requestIndex = static_cast<uint32_t>(m_requests.size());
        const uint32_t base = request.requestIndex;
        m_requests.push_back(request);
        ++m_punctualRequestCount;
        ++m_shadowedLightCount;
        lightBuilder.setShadowIndex(lightIndex, static_cast<int32_t>(base));
        return true;
    }

    return false;
}

bool ShadowManager::packPunctual(uint16_t width, uint16_t height, ShadowRequest& request) {
    if (width == 0 || height == 0 || width > m_punctualAtlasSize || height > m_punctualAtlasSize) {
        return false;
    }
    if (m_punctualCursorX + width > m_punctualAtlasSize) {
        m_punctualCursorX = 0;
        m_punctualCursorY = static_cast<uint16_t>(m_punctualCursorY + m_punctualRowHeight);
        m_punctualRowHeight = 0;
    }
    if (m_punctualCursorY + height > m_punctualAtlasSize) {
        return false;
    }
    request.viewportX = m_punctualCursorX;
    request.viewportY = m_punctualCursorY;
    request.viewportWidth = width;
    request.viewportHeight = height;
    m_punctualCursorX = static_cast<uint16_t>(m_punctualCursorX + width);
    m_punctualRowHeight = std::max(m_punctualRowHeight, height);
    return true;
}

void ShadowManager::buildContextData() {
    m_contextData = {};
    for (const ShadowRequest& request : m_requests) {
        const uint32_t base = request.requestIndex * kShadowContextTexels;
        if (base + kShadowContextTexels > m_contextData.texels.size()) {
            continue;
        }
        const Mat4& matrix = request.worldToShadow;
        m_contextData.texels[base + 0] = Vec4(matrix.values[0], matrix.values[1], matrix.values[2], matrix.values[3]);
        m_contextData.texels[base + 1] = Vec4(matrix.values[4], matrix.values[5], matrix.values[6], matrix.values[7]);
        m_contextData.texels[base + 2] = Vec4(matrix.values[8], matrix.values[9], matrix.values[10], matrix.values[11]);
        m_contextData.texels[base + 3] =
            Vec4(matrix.values[12], matrix.values[13], matrix.values[14], matrix.values[15]);
        const float atlasSize = static_cast<float>(request.atlasSize);
        m_contextData.texels[base + 4] =
            Vec4(static_cast<float>(request.viewportWidth) / atlasSize,
                 static_cast<float>(request.viewportHeight) / atlasSize,
                 static_cast<float>(request.viewportX) / atlasSize, static_cast<float>(request.viewportY) / atlasSize);
        m_contextData.texels[base + 5] =
            Vec4(request.depthBias, request.normalBias, 1.0f / static_cast<float>(request.viewportWidth),
                 1.0f / static_cast<float>(request.viewportHeight));
        m_contextData.texels[base + 6] =
            Vec4(static_cast<float>(request.type), static_cast<float>(request.faceIndex), 0.0f, 1.0f);
        m_contextData.texels[base + 7] = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    m_contextData.requestCount = static_cast<uint32_t>(m_requests.size());
}

void ShadowManager::uploadContext() {
    if (m_resources == nullptr || !m_contextTexture.valid()) {
        return;
    }
    m_resources->updateTexture(m_contextTexture, m_contextData.texels.data(),
                               static_cast<uint32_t>(sizeof(m_contextData.texels)), kShadowContextTexels,
                               static_cast<uint16_t>(kMaxShadowRequests),
                               static_cast<uint16_t>(kShadowContextTexels * sizeof(Vec4)));
}

void ShadowManager::beginPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                              const ShadowRequest& request) const {
    if (!request.enabled || !frameBuffer.valid()) {
        return;
    }
    encoder.setViewRect(view, request.viewportX, request.viewportY, request.viewportWidth, request.viewportHeight);
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewTransform(view, request.view.data(), request.projection.data());
    encoder.setViewClear(view, BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
    encoder.touch(view);
}

} // namespace hbg::render
