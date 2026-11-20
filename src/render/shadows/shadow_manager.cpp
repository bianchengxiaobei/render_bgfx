#include "render/shadows/shadow_manager.h"

#include "core/log.h"
#include "gfx/encoder.h"
#include "render/culling/frustum.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace hbg::render {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDirectionalDepthBias = 0.01f;
constexpr float kPointDepthBias = 0.01f;
constexpr float kSpotDepthBias = 0.003f;
constexpr float kAreaDepthBias = 0.003f;
constexpr float kReferenceSlopeBias = 5.0f;
constexpr float kWorldTexelScale = 1.41421356237f;
constexpr float kDefaultAreaShadowCone = 120.0f;
constexpr float kMinAreaShadowCone = 10.0f;
constexpr float kPointShadowGuardAngle = 0.10471975512f;
constexpr float kMaxAreaShadowCone = 179.0f;

float computePunctualShadowFade(float distanceToCamera, float fadeDistance) {
    if (fadeDistance <= 0.0f) {
        return 0.0f;
    }
    const float fadeStart = fadeDistance * 0.9f;
    if (distanceToCamera <= fadeStart) {
        return 1.0f;
    }
    if (distanceToCamera >= fadeDistance) {
        return 0.0f;
    }
    return 1.0f - (distanceToCamera - fadeStart) / std::max(fadeDistance - fadeStart, 1.0e-4f);
}

float computeWorldTexelSize(const Mat4& projection, uint16_t viewportWidth) {
    const float projectionX = std::abs(projection.values[0]);
    if (projectionX <= 1.0e-8f || viewportWidth == 0) {
        return 0.0f;
    }
    return (2.0f / projectionX / static_cast<float>(viewportWidth)) * kWorldTexelScale;
}

Vec4 computeImsParams(const ShadowRequest& request) {
    const float width = std::abs(request.projection.values[0]) > 1.0e-8f
                            ? 2.0f / std::abs(request.projection.values[0])
                            : 0.0f;
    const float height = std::abs(request.projection.values[5]) > 1.0e-8f
                             ? 2.0f / std::abs(request.projection.values[5])
                             : 0.0f;
    const float depth = std::max(request.farPlane - request.nearPlane, 1.0e-4f);
    const float scale = 0.5f * std::tan(0.5f * request.lightAngle) * depth;
    const float lightX = width > 1.0e-8f ? scale / width : 0.0f;
    const float lightY = height > 1.0e-8f ? scale / height : 0.0f;
    return Vec4(static_cast<float>(request.imsKernelSize), lightX, lightY, request.maxDepthBias);
}

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

void hashBytes(uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
}

void hashFloat(uint64_t& hash, float value) {
    hashBytes(hash, &value, sizeof(value));
}

void hashUint(uint64_t& hash, uint32_t value) {
    hashBytes(hash, &value, sizeof(value));
}

void hashVec3(uint64_t& hash, const Vec3& value) {
    hashFloat(hash, value.x);
    hashFloat(hash, value.y);
    hashFloat(hash, value.z);
}

void hashMat4(uint64_t& hash, const Mat4& value) {
    hashBytes(hash, value.values.data(), sizeof(float) * 16);
}

Vec3 lightUp(const Vec3& direction) {
    if (std::abs(dot(direction, Vec3(0.0f, 1.0f, 0.0f))) > 0.95f) {
        return Vec3(0.0f, 0.0f, 1.0f);
    }
    return Vec3(0.0f, 1.0f, 0.0f);
}

Vec4 matrixRow(const Mat4& matrix, std::size_t row) {
    return Vec4(matrix.values[row], matrix.values[4 + row], matrix.values[8 + row], matrix.values[12 + row]);
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

Vec4 planeAdd(const Vec4& lhs, const Vec4& rhs) {
    return Vec4(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w);
}

Vec4 planeSub(const Vec4& lhs, const Vec4& rhs) {
    return Vec4(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w);
}

std::array<Vec4, 6> extractFrustumPlanes(const Mat4& viewProj, bool homogeneousDepth) {
    const Vec4 row0 = matrixRow(viewProj, 0);
    const Vec4 row1 = matrixRow(viewProj, 1);
    const Vec4 row2 = matrixRow(viewProj, 2);
    const Vec4 row3 = matrixRow(viewProj, 3);
    const Vec4 nearPlane = homogeneousDepth ? row2 : planeAdd(row3, row2);
    const Vec4 farPlane = planeSub(row3, row2);
    return {
        normalizePlane(planeAdd(row3, row0)),
        normalizePlane(planeSub(row3, row0)),
        normalizePlane(planeAdd(row3, row1)),
        normalizePlane(planeSub(row3, row1)),
        normalizePlane(nearPlane),
        normalizePlane(farPlane),
    };
}

void updateFrustumPlanes(ShadowRequest& request, bool homogeneousDepth) {
    request.frustumPlanes = extractFrustumPlanes(request.viewProj, homogeneousDepth);
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
                           Mat4& outWorldToShadow, float& outNearPlane, float& outFarPlane) {
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
    outNearPlane = nearPlane;
    outFarPlane = farPlane;
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
    updateFrustumPlanes(request, homogeneousDepth);
    return true;
}

bool makeAreaPerspectiveRequest(const Vec3& position, const Vec3& direction, const Vec3& up, float fov,
                                float aspect, float nearPlane, float farPlane, bool homogeneousDepth,
                                bool originBottomLeft, ShadowRequest& request) {
    if (!(farPlane > nearPlane) || !(fov > 0.0f) || !(aspect > 0.0f)) {
        return false;
    }
    const Mat4 view = lookAtRH(position, position + direction, up);
    const float scale = 1.0f / std::tan(fov * 0.5f);
    Mat4 projection{};
    // HDRP ExtractSpotLightProjectionMatrix: the cone angle applies to the
    // smaller area dimension, so wide rectangles still fit inside the spot.
    if (aspect < 1.0f) {
        projection.values[0] = scale;
        projection.values[5] = scale * aspect;
    } else {
        projection.values[0] = scale / aspect;
        projection.values[5] = scale;
    }
    const float depthRange = nearPlane - farPlane;
    projection.values[10] = farPlane / depthRange;
    projection.values[11] = -1.0f;
    projection.values[14] = (nearPlane * farPlane) / depthRange;
    if (homogeneousDepth) {
        projection.values[10] = (farPlane + nearPlane) / depthRange;
        projection.values[14] = (2.0f * nearPlane * farPlane) / depthRange;
    }
    request.view = view;
    request.projection = projection;
    request.viewProj = projection * view;
    request.worldToShadow = biasCrop(originBottomLeft, homogeneousDepth) * request.viewProj;
    request.lightPosition = position;
    request.lightDirection = direction;
    request.enabled = true;
    updateFrustumPlanes(request, homogeneousDepth);
    return true;
}

} // namespace

bool ShadowManager::init(gfx::ResourceManager& resources, const ShadowSettings& settings) {
    m_resources = &resources;
    m_settings = settings;
    sanitizeShadowSettings(m_settings);
    m_sortedPackingWarningLogged = false;
    if (!createTextures()) {
        m_resources = nullptr;
        return false;
    }
    invalidate();
    return true;
}

bool ShadowManager::setSettings(const ShadowSettings& settings) {
    m_settings = settings;
    sanitizeShadowSettings(m_settings);
    m_directionalAtlasSize = static_cast<uint16_t>(m_settings.atlasSize);
    m_punctualAtlasSize = static_cast<uint16_t>(m_settings.atlasSize);
    m_areaAtlasSize = static_cast<uint16_t>(m_settings.areaAtlasSize);
    m_sortedPackingWarningLogged = false;
    if (m_resources == nullptr) {
        return true;
    }
    destroyTextures();
    invalidate();
    return createTextures();
}

bool ShadowManager::createTextures() {
    if (m_resources == nullptr) {
        return false;
    }

    m_directionalAtlasSize = static_cast<uint16_t>(m_settings.atlasSize);
    m_punctualAtlasSize = static_cast<uint16_t>(m_settings.atlasSize);
    m_areaAtlasSize = static_cast<uint16_t>(m_settings.areaAtlasSize);

    gfx::TextureDesc directionalDesc;
    directionalDesc.width = m_directionalAtlasSize;
    directionalDesc.height = m_directionalAtlasSize;
    directionalDesc.format = gfx::TextureFormat::D24S8;
    directionalDesc.depthStencil = true;
    directionalDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT | BGFX_SAMPLER_COMPARE_LEQUAL;
    directionalDesc.name = "shadow.directionalAtlas";
    m_directionalTexture = m_resources->createTexture(directionalDesc);
    if (!m_directionalTexture.valid()) {
        HBG_LOGE("shadow manager: failed to create directional atlas {}x{}", m_directionalAtlasSize,
                 m_directionalAtlasSize);
        destroyTextures();
        return false;
    }

    if (m_settings.cachedDirectionalShadows) {
        gfx::TextureDesc cachedDirectionalDesc = directionalDesc;
        cachedDirectionalDesc.name = "shadow.cachedDirectionalAtlas";
        m_cachedDirectionalTexture = m_resources->createTexture(cachedDirectionalDesc);
        if (!m_cachedDirectionalTexture.valid()) {
            HBG_LOGE("shadow manager: failed to create cached directional atlas {}x{}",
                     m_directionalAtlasSize, m_directionalAtlasSize);
            destroyTextures();
            return false;
        }
    }

    m_imsActive = false;
    if (m_settings.directionalFilter == ShadowFilter::Ims) {
        gfx::TextureDesc momentDesc;
        momentDesc.width = m_directionalAtlasSize;
        momentDesc.height = m_directionalAtlasSize;
        momentDesc.format = gfx::TextureFormat::RGBA32F;
        momentDesc.computeWrite = true;
        momentDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        momentDesc.name = "shadow.directionalMoments";
        m_directionalMomentTexture = m_resources->createTexture(momentDesc);
        gfx::TextureDesc satDesc = momentDesc;
        satDesc.format = gfx::TextureFormat::RGBA32U;
        satDesc.name = "shadow.directionalMomentSat";
        m_directionalMomentSatTexture = m_resources->createTexture(satDesc);
        m_imsActive = m_directionalMomentTexture.valid() && m_directionalMomentSatTexture.valid();
        if (!m_imsActive) {
            HBG_LOGW("shadow manager: IMS textures unavailable at {}x{}; falling back to PCF7x7",
                     m_directionalAtlasSize, m_directionalAtlasSize);
            if (m_directionalMomentTexture.valid()) {
                m_resources->destroyTexture(m_directionalMomentTexture);
                m_directionalMomentTexture = {};
            }
            if (m_directionalMomentSatTexture.valid()) {
                m_resources->destroyTexture(m_directionalMomentSatTexture);
                m_directionalMomentSatTexture = {};
            }
        } else {
            HBG_LOGI("shadow IMS: {}x{} RGBA32F moments + RGBA32U SAT (~{:.1f} MB)", m_directionalAtlasSize,
                     m_directionalAtlasSize,
                     static_cast<double>(m_directionalAtlasSize) * m_directionalAtlasSize * 32.0 /
                         (1024.0 * 1024.0));
        }
    }

    gfx::TextureDesc punctualDesc = directionalDesc;
    punctualDesc.name = "shadow.punctualAtlas";
    m_punctualTexture = m_resources->createTexture(punctualDesc);
    if (!m_punctualTexture.valid()) {
        HBG_LOGE("shadow manager: failed to create punctual atlas {}x{}", m_punctualAtlasSize, m_punctualAtlasSize);
        destroyTextures();
        return false;
    }

    gfx::TextureDesc cachedDesc = punctualDesc;
    cachedDesc.name = "shadow.cachedPunctualAtlas";
    m_cachedPunctualTexture = m_resources->createTexture(cachedDesc);
    if (!m_cachedPunctualTexture.valid()) {
        HBG_LOGE("shadow manager: failed to create cached punctual atlas {}x{}", m_punctualAtlasSize,
                 m_punctualAtlasSize);
        destroyTextures();
        return false;
    }

    gfx::TextureDesc areaDesc = directionalDesc;
    areaDesc.width = m_areaAtlasSize;
    areaDesc.height = m_areaAtlasSize;
    areaDesc.flags |= BGFX_TEXTURE_BLIT_DST;
    areaDesc.name = "shadow.areaAtlas";
    m_areaTexture = m_resources->createTexture(areaDesc);
    if (!m_areaTexture.valid()) {
        HBG_LOGE("shadow manager: failed to create area atlas {}x{}", m_areaAtlasSize, m_areaAtlasSize);
        destroyTextures();
        return false;
    }

    if (m_settings.cachedAreaShadows) {
        gfx::TextureDesc cachedAreaDesc = areaDesc;
        cachedAreaDesc.name = "shadow.cachedAreaAtlas";
        m_cachedAreaTexture = m_resources->createTexture(cachedAreaDesc);
        if (!m_cachedAreaTexture.valid()) {
            HBG_LOGE("shadow manager: failed to create cached area atlas {}x{}", m_areaAtlasSize,
                     m_areaAtlasSize);
            destroyTextures();
            return false;
        }
    }
    HBG_LOGI("shadow area atlas: {}x{} D24S8 (~{:.1f} MB), cached={} (~{:.1f} MB), resolution={}, filter={}",
             m_areaAtlasSize, m_areaAtlasSize,
             static_cast<double>(m_areaAtlasSize) * m_areaAtlasSize * 4.0 / (1024.0 * 1024.0),
             m_settings.cachedAreaShadows ? "true" : "false",
             m_settings.cachedAreaShadows
                 ? static_cast<double>(m_areaAtlasSize) * m_areaAtlasSize * 4.0 / (1024.0 * 1024.0)
                 : 0.0,
             m_settings.areaResolution, shadowFilterName(m_settings.areaFilter));

    gfx::VertexLayout contextLayout;
    contextLayout.add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();
    gfx::BufferDesc contextDesc;
    contextDesc.type = gfx::BufferType::StorageVertex;
    contextDesc.count = kMaxShadowRequests * kShadowContextTexels;
    contextDesc.layout = &contextLayout;
    contextDesc.flags = BGFX_BUFFER_COMPUTE_READ;
    contextDesc.name = "shadow.context";
    m_contextBuffer = m_resources->createBuffer(contextDesc);
    if (!m_contextBuffer.valid()) {
        HBG_LOGE("shadow manager: failed to create shadow context buffer");
        destroyTextures();
        return false;
    }

    gfx::BufferDesc contextAreaDesc = contextDesc;
    contextAreaDesc.name = "shadow.contextArea";
    m_contextAreaBuffer = m_resources->createBuffer(contextAreaDesc);
    if (!m_contextAreaBuffer.valid()) {
        HBG_LOGE("shadow manager: failed to create area shadow context buffer");
        destroyTextures();
        return false;
    }

    resetPacking();
    return true;
}

void ShadowManager::destroyTextures() {
    if (m_resources == nullptr) {
        return;
    }
    if (m_directionalTexture.valid()) {
        m_resources->destroyTexture(m_directionalTexture);
        m_directionalTexture = {};
    }
    if (m_cachedDirectionalTexture.valid()) {
        m_resources->destroyTexture(m_cachedDirectionalTexture);
        m_cachedDirectionalTexture = {};
    }
    if (m_directionalMomentTexture.valid()) {
        m_resources->destroyTexture(m_directionalMomentTexture);
        m_directionalMomentTexture = {};
    }
    if (m_directionalMomentSatTexture.valid()) {
        m_resources->destroyTexture(m_directionalMomentSatTexture);
        m_directionalMomentSatTexture = {};
    }
    m_imsActive = false;
    if (m_punctualTexture.valid()) {
        m_resources->destroyTexture(m_punctualTexture);
        m_punctualTexture = {};
    }
    if (m_cachedPunctualTexture.valid()) {
        m_resources->destroyTexture(m_cachedPunctualTexture);
        m_cachedPunctualTexture = {};
    }
    if (m_areaTexture.valid()) {
        m_resources->destroyTexture(m_areaTexture);
        m_areaTexture = {};
    }
    if (m_cachedAreaTexture.valid()) {
        m_resources->destroyTexture(m_cachedAreaTexture);
        m_cachedAreaTexture = {};
    }
    if (m_contextBuffer.valid()) {
        m_resources->destroyBuffer(m_contextBuffer);
        m_contextBuffer = {};
    }
    if (m_contextAreaBuffer.valid()) {
        m_resources->destroyBuffer(m_contextAreaBuffer);
        m_contextAreaBuffer = {};
    }
}

void ShadowManager::shutdown(gfx::ResourceManager& resources) {
    (void)resources;
    invalidate();
    destroyTextures();
    m_resources = nullptr;
}

void ShadowManager::invalidate() {
    resetFrameRequests();
    m_directionalRequestCacheValid.fill(false);
    m_directionalRequestHashes.fill(0);
    m_cacheHash = 0;
    m_areaCacheHash = 0;
    m_cachedDirectionalRequestCount = 0;
    m_cachedRequestCount = 0;
    m_cachedAreaRequestCount = 0;
    m_directionalCacheEligible = false;
    m_directionalCacheValid = false;
    m_directionalCacheDirty = false;
    m_directionalCacheMixed = false;
    m_cacheEligible = false;
    m_cacheValid = false;
    m_areaCacheEligible = false;
    m_areaCacheValid = false;
    m_areaCacheMixed = false;
    m_areaCacheMixedPrev = false;
    m_areaCacheDirty = false;
    m_areaUsesDynamicAtlas = false;
}

void ShadowManager::resetFrameRequests() {
    m_requests.clear();
    m_contextData = {};
    m_cascadeSplits = {};
    m_directionalRequestCount = 0;
    m_punctualRequestCount = 0;
    m_areaRequestCount = 0;
    m_shadowedLightCount = 0;
    m_cachedDirectionalRequestCount = 0;
    m_cachedRequestCount = 0;
    m_cachedAreaRequestCount = 0;
    resetPacking();
}

void ShadowManager::resetPacking() {
    m_punctualCursorX = 0;
    m_punctualCursorY = 0;
    m_punctualRowHeight = 0;
    m_areaCursorX = 0;
    m_areaCursorY = 0;
    m_areaRowHeight = 0;
}

bool ShadowManager::update(const RenderScene& scene, const ViewConstants& view, const Camera& camera,
                           LightBuilder& lightBuilder) {
    resetFrameRequests();
    const bool homogeneousDepth = bgfx::getCaps() != nullptr && bgfx::getCaps()->homogeneousDepth;
    const bool originBottomLeft = bgfx::getCaps() != nullptr && bgfx::getCaps()->originBottomLeft;
    const Vec3 cameraPosition = view.worldSpaceCameraPos;

    bool punctualCacheEligible = m_settings.cachedPunctualShadows && !m_cacheSuppressed;
    const bool areaCacheRequested = m_settings.cachedAreaShadows && !m_cacheSuppressed;
    bool hasPunctualCaster = false;
    const auto& selectedLights = lightBuilder.selectedLights();
    for (uint32_t index = 0; index < selectedLights.size(); ++index) {
        const Light& light = *selectedLights[index];
        if (!light.castShadows || light.intensity <= 0.0f) {
            continue;
        }
        const LightShadowSettings shadow = resolveLightShadow(light, m_settings);
        if (light.type == LightType::Area) {
            if (light.areaShape != LightAreaShape::Rectangle) {
                continue;
            }
            const Vec3 position = worldTranslation(light.world);
            const float distanceToCamera = length(position - cameraPosition);
            buildAreaRequest(light, shadow, index, lightBuilder, homogeneousDepth, originBottomLeft,
                             distanceToCamera);
            continue;
        }
        if (light.type == LightType::Directional) {
            buildDirectionalRequests(scene, view, camera, light, shadow, index, lightBuilder, homogeneousDepth,
                                     originBottomLeft);
        } else {
            hasPunctualCaster = true;
            if (light.shadowConfigured && shadow.updateMode == ShadowUpdateMode::EveryFrame) {
                punctualCacheEligible = false;
            }
            const Vec3 position = worldTranslation(light.world);
            const float distanceToCamera = length(position - cameraPosition);
            buildPunctualRequest(light, shadow, index, lightBuilder, homogeneousDepth, originBottomLeft,
                                 distanceToCamera);
        }
    }

    packPunctualSorted();
    repackAreaRequests();


    punctualCacheEligible = punctualCacheEligible && hasPunctualCaster;
    bool hasStaticArea = false;
    bool hasDynamicArea = false;
    for (const ShadowRequest& request : m_requests) {
        if (request.atlas != ShadowAtlasType::Area) {
            continue;
        }
        hasStaticArea = hasStaticArea || request.isStaticArea;
        hasDynamicArea = hasDynamicArea || !request.isStaticArea;
    }
    const bool areaCacheEligible = areaCacheRequested && hasStaticArea;
    m_cacheEligible = punctualCacheEligible;
    m_areaCacheEligible = areaCacheEligible;
    const bool areaCacheMixed = areaCacheEligible && hasDynamicArea;
    if (areaCacheMixed != m_areaCacheMixedPrev) {
        m_areaCacheValid = false;
        m_areaCacheHash = 0;
    }
    m_areaCacheMixedPrev = areaCacheMixed;
    m_areaCacheMixed = areaCacheMixed;
    m_areaUsesDynamicAtlas = hasDynamicArea;

    const bool directionalCacheEligible =
        m_settings.cachedDirectionalShadows && !m_cacheSuppressed && m_directionalRequestCount > 0;
    m_directionalCacheEligible = directionalCacheEligible;
    m_directionalCacheDirty = false;
    m_directionalCacheMixed = false;
    if (directionalCacheEligible) {
        bool anyCached = false;
        bool anyDynamic = false;
        for (ShadowRequest& request : m_requests) {
            if (request.atlas != ShadowAtlasType::Directional) {
                continue;
            }
            const uint64_t hash = buildDirectionalRequestHash(scene, request);
            const std::size_t cacheIndex = request.requestIndex;
            if (cacheIndex < m_directionalRequestCacheValid.size() &&
                m_directionalRequestCacheValid[cacheIndex] && m_directionalRequestHashes[cacheIndex] == hash) {
                request.isCached = true;
                ++m_cachedDirectionalRequestCount;
                anyCached = true;
            } else {
                if (cacheIndex < m_directionalRequestCacheValid.size()) {
                    m_directionalRequestHashes[cacheIndex] = hash;
                    m_directionalRequestCacheValid[cacheIndex] = true;
                }
                anyDynamic = true;
            }
        }
        m_directionalCacheValid = true;
        m_directionalCacheDirty = anyDynamic;
        m_directionalCacheMixed = anyCached && anyDynamic;
    } else {
        m_directionalRequestCacheValid.fill(false);
        m_directionalRequestHashes.fill(0);
        m_directionalCacheValid = false;
    }
    const uint64_t cacheHash =
        punctualCacheEligible ? buildCacheHash(scene, lightBuilder, ShadowAtlasType::Punctual) : 0;
    if (punctualCacheEligible && m_cacheValid && m_cacheHash == cacheHash) {
        for (ShadowRequest& request : m_requests) {
            if (request.atlas == ShadowAtlasType::Punctual) {
                request.isCached = true;
                ++m_cachedRequestCount;
            }
        }
    } else if (punctualCacheEligible) {
        m_cacheHash = cacheHash;
        m_cacheValid = true;
    } else {
        m_cacheHash = 0;
        m_cacheValid = false;
    }

    const uint64_t areaCacheHash =
        areaCacheEligible ? buildCacheHash(scene, lightBuilder, ShadowAtlasType::Area, true) : 0;
    const bool areaCacheHit = areaCacheEligible && m_areaCacheValid && m_areaCacheHash == areaCacheHash;
    m_areaCacheDirty = areaCacheEligible && !areaCacheHit;
    if (areaCacheHit) {
        for (ShadowRequest& request : m_requests) {
            if (request.atlas == ShadowAtlasType::Area && request.isStaticArea) {
                request.isCached = true;
                ++m_cachedAreaRequestCount;
            }
        }
    } else if (areaCacheEligible) {
        m_areaCacheHash = areaCacheHash;
        m_areaCacheValid = true;
    } else {
        m_areaCacheHash = 0;
        m_areaCacheValid = false;
    }

    buildContextData();
    uploadContext();
    lightBuilder.setShadowedCount(m_shadowedLightCount);
    return valid();
}

bool ShadowManager::buildDirectionalRequests(const RenderScene& scene, const ViewConstants& view, const Camera& camera,
                                             const Light& light, const LightShadowSettings& shadow,
                                             uint32_t lightIndex, LightBuilder& lightBuilder,
                                             bool homogeneousDepth, bool originBottomLeft) {
    (void)scene;
    if (shadow.dimmer <= 0.0f) {
        return false;
    }
    const uint32_t cascadeCount = std::clamp(m_settings.cascadeCount, 1u, kMaxDirectionalCascades);
    const float shadowDistance =
        std::min(camera.farClip, std::max(camera.nearClip + 0.01f, m_settings.maxDistance));
    if (shadowDistance <= camera.nearClip + 0.01f) {
        return false;
    }
    if (m_requests.size() + cascadeCount > m_settings.maxRequests) {
        return false;
    }

    const uint32_t grid = cascadeCount <= 1 ? 1u : 2u;
    const uint32_t maxCell = static_cast<uint32_t>(m_directionalAtlasSize) / grid;
    const uint32_t requestedResolution =
        shadow.resolution != 0 ? shadow.resolution : m_settings.directionalResolution;
    const uint32_t cascadeSize = std::clamp(requestedResolution, 1u, std::max(maxCell, 1u));
    if (cascadeSize == 0) {
        return false;
    }

    std::array<ShadowRequest, kMaxDirectionalCascades> requests{};
    std::array<float, kMaxDirectionalCascades> splits{};
    float fadeScale = 0.0f;
    float fadeBias = 0.0f;
    computeShadowFade(shadowDistance, m_settings.cascadeFade, fadeScale, fadeBias);
    float previousFar = camera.nearClip;
    for (uint32_t cascade = 0; cascade < cascadeCount; ++cascade) {
        const float split = cascade + 1 == cascadeCount ? 1.0f : m_settings.cascadeSplits[cascade];
        const float farZ = camera.nearClip + (shadowDistance - camera.nearClip) * split;
        ShadowRequest& request = requests[cascade];
        float lightNearPlane = 0.0f;
        float lightFarPlane = 1.0f;
        if (!fitDirectionalRequest(view, camera, light, previousFar, farZ, homogeneousDepth, originBottomLeft,
                                   request.view, request.projection, request.worldToShadow, lightNearPlane,
                                   lightFarPlane)) {
            return false;
        }
        request.enabled = true;
        request.type = ShadowRequestType::Directional;
        request.atlas = ShadowAtlasType::Directional;
        request.lightIndex = lightIndex;
        request.faceIndex = cascade;
        request.cascadeIndex = cascade;
        request.atlasSize = m_directionalAtlasSize;
        if (cascadeCount == 1) {
            request.viewportX = 0;
            request.viewportY = 0;
        } else if (cascadeCount == 2) {
            request.viewportX = static_cast<uint16_t>(cascade * cascadeSize);
            request.viewportY = 0;
        } else {
            request.viewportX = static_cast<uint16_t>((cascade % 2u) * cascadeSize);
            request.viewportY = static_cast<uint16_t>((cascade / 2u) * cascadeSize);
        }
        request.viewportWidth = static_cast<uint16_t>(cascadeSize);
        request.viewportHeight = static_cast<uint16_t>(cascadeSize);
        request.viewProj = request.projection * request.view;
        request.lightDirection = normalize(transformDirection(light.world, Vec3(0.0f, 0.0f, 1.0f)));
        request.depthBias = kDirectionalDepthBias;
        request.normalBias = m_settings.normalBias;
        request.softness = shadow.softness;
        request.nearPlane = lightNearPlane;
        request.farPlane = lightFarPlane;
        request.lightAngle = m_settings.directionalLightAngle * kPi / 180.0f;
        request.maxDepthBias = m_settings.directionalMaxDepthBias;
        request.imsKernelSize = m_settings.imsKernelSize;
        request.worldTexelSize = computeWorldTexelSize(request.projection, request.viewportWidth);
        request.slopeBias = m_settings.slopeBias * kReferenceSlopeBias;
        request.fadeScale = fadeScale;
        request.fadeBias = fadeBias;
        request.blockerSamples = shadow.blockerSamples;
        request.filterSamples = shadow.filterSamples;
        request.minFilterSize = m_settings.minFilterSize;
        request.filter = m_settings.directionalFilter;
        if (request.filter == ShadowFilter::Ims && !m_imsActive) {
            if (!m_imsWarningLogged) {
                HBG_LOGW("shadow manager: IMS is unavailable; directional shadows fall back to PCF7x7");
                m_imsWarningLogged = true;
            }
            request.filter = ShadowFilter::Pcf7x7;
        }
        updateFrustumPlanes(request, homogeneousDepth);
        splits[cascade] = farZ;
        previousFar = farZ;
    }

    const uint32_t base = static_cast<uint32_t>(m_requests.size());
    for (uint32_t cascade = 0; cascade < cascadeCount; ++cascade) {
        requests[cascade].requestIndex = base + cascade;
        m_requests.push_back(requests[cascade]);
    }
    for (uint32_t index = cascadeCount; index < kMaxDirectionalCascades; ++index) {
        splits[index] = shadowDistance;
    }
    m_cascadeSplits = Vec4(splits[0], splits[1], splits[2], splits[3]);
    m_directionalRequestCount = cascadeCount;
    ++m_shadowedLightCount;
    lightBuilder.setShadowIndex(lightIndex, static_cast<int32_t>(base));
    return true;
}

bool ShadowManager::buildPunctualRequest(const Light& light, const LightShadowSettings& shadow, uint32_t lightIndex,
                                         LightBuilder& lightBuilder, bool homogeneousDepth, bool originBottomLeft,
                                         float distanceToCamera) {
    const float distanceFade = computePunctualShadowFade(distanceToCamera, shadow.fadeDistance);
    if (shadow.dimmer <= 0.0f || distanceFade <= 0.0f) {
        return false;
    }
    lightBuilder.setShadowDimmer(lightIndex, shadow.dimmer * distanceFade);
    const Vec3 position = worldTranslation(light.world);
    const float range = std::max(light.range, 0.01f);
    const float nearPlane = std::clamp(shadow.nearPlane, 0.01f, std::max(0.01f, range * 0.99f));

    if (light.type == LightType::Point) {
        constexpr std::array<Vec3, 6> kDirections = {
            Vec3(1.0f, 0.0f, 0.0f),  Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f),
            Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f),  Vec3(0.0f, 0.0f, -1.0f),
        };
        constexpr std::array<Vec3, 6> kUps = {
            Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f),
            Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f),
        };
        if (m_requests.size() + kDirections.size() > m_settings.maxRequests) {
            return false;
        }

        const uint16_t savedX = m_punctualCursorX;
        const uint16_t savedY = m_punctualCursorY;
        const uint16_t savedRowHeight = m_punctualRowHeight;
        const std::size_t savedRequests = m_requests.size();
        const uint32_t base = static_cast<uint32_t>(m_requests.size());
        for (uint32_t face = 0; face < kDirections.size(); ++face) {
            ShadowRequest request;
            if (!makePerspectiveRequest(position, kDirections[face], kUps[face],
                                        kPi * 0.5f + kPointShadowGuardAngle, nearPlane, range, homogeneousDepth,
                                        originBottomLeft, request)) {
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
            request.normalBias = m_settings.normalBias;
            request.softness = shadow.softness;
            request.nearPlane = nearPlane;
            request.farPlane = range;
            request.slopeBias = m_settings.slopeBias * kReferenceSlopeBias;
            request.blockerSamples = shadow.blockerSamples;
            request.filterSamples = shadow.filterSamples;
            request.minFilterSize = m_settings.minFilterSize;
            request.filter = m_settings.punctualFilter;
            const uint32_t pointResolution = std::min(shadow.resolution != 0 ? shadow.resolution
                                                                            : m_settings.pointResolution,
                                                      static_cast<uint32_t>(m_punctualAtlasSize));
            if (!packPunctual(static_cast<uint16_t>(pointResolution), static_cast<uint16_t>(pointResolution),
                              request)) {
                m_punctualCursorX = savedX;
                m_punctualCursorY = savedY;
                m_punctualRowHeight = savedRowHeight;
                m_requests.resize(savedRequests);
                return false;
            }
            request.worldTexelSize = computeWorldTexelSize(request.projection, request.viewportWidth);
            request.requestIndex = static_cast<uint32_t>(m_requests.size());
            m_requests.push_back(request);
        }
        m_punctualRequestCount += static_cast<uint32_t>(kDirections.size());
        ++m_shadowedLightCount;
        lightBuilder.setShadowIndex(lightIndex, static_cast<int32_t>(base));
        return true;
    }

    if (light.type == LightType::Spot) {
        if (m_requests.size() + 1 > m_settings.maxRequests) {
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
        request.normalBias = m_settings.normalBias;
        request.softness = shadow.softness;
        request.nearPlane = nearPlane;
        request.farPlane = range;
        request.slopeBias = m_settings.slopeBias * kReferenceSlopeBias;
        request.blockerSamples = shadow.blockerSamples;
        request.filterSamples = shadow.filterSamples;
        request.minFilterSize = m_settings.minFilterSize;
        request.filter = m_settings.punctualFilter;
        const uint16_t savedX = m_punctualCursorX;
        const uint16_t savedY = m_punctualCursorY;
        const uint16_t savedRowHeight = m_punctualRowHeight;
        const uint32_t spotResolution =
            std::min(shadow.resolution != 0 ? shadow.resolution : m_settings.spotResolution,
                     static_cast<uint32_t>(m_punctualAtlasSize));
        if (!packPunctual(static_cast<uint16_t>(spotResolution), static_cast<uint16_t>(spotResolution), request)) {
            m_punctualCursorX = savedX;
            m_punctualCursorY = savedY;
            m_punctualRowHeight = savedRowHeight;
            return false;
        }
        request.worldTexelSize = computeWorldTexelSize(request.projection, request.viewportWidth);
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

bool ShadowManager::buildAreaRequest(const Light& light, const LightShadowSettings& shadow, uint32_t lightIndex,
                                     LightBuilder& lightBuilder, bool homogeneousDepth, bool originBottomLeft,
                                     float distanceToCamera) {
    if (light.areaShape != LightAreaShape::Rectangle || shadow.dimmer <= 0.0f) {
        return false;
    }
    const float distanceFade = computePunctualShadowFade(distanceToCamera, shadow.fadeDistance);
    if (distanceFade <= 0.0f) {
        return false;
    }
    if (m_requests.size() + 1 > m_settings.maxRequests) {
        return false;
    }

    const float width = std::max(light.areaWidth, 0.01f);
    const float height = std::max(light.areaHeight, 0.01f);
    const float coneDegrees =
        std::clamp(light.areaShadowCone > 0.0f ? light.areaShadowCone : kDefaultAreaShadowCone,
                   kMinAreaShadowCone, kMaxAreaShadowCone);
    const float cone = coneDegrees * (kPi / 180.0f);
    const float halfMinExtent = std::min(width, height) * 0.5f;
    const float forwardOffset = -halfMinExtent / std::max(std::tan(cone * 0.5f), 1.0e-4f);

    const Vec3 lightPosition = worldTranslation(light.world);
    const Vec3 forward = normalize(transformDirection(light.world, Vec3(0.0f, 0.0f, -1.0f)));
    Vec3 up = normalize(transformDirection(light.world, Vec3(0.0f, 1.0f, 0.0f)));
    if (std::abs(dot(forward, up)) > 0.99f) {
        up = lightUp(forward);
    }

    const float range = std::max(light.range, 0.01f);
    const float nearPlane = std::max(0.01f, shadow.nearPlane - forwardOffset);
    const float farPlane = std::max(nearPlane + 0.01f, range - forwardOffset);
    const float aspect = width / height;
    ShadowRequest request;
    if (!makeAreaPerspectiveRequest(lightPosition + forward * forwardOffset, forward, up, cone, aspect,
                                    nearPlane, farPlane, homogeneousDepth, originBottomLeft, request)) {
        return false;
    }
    request.type = ShadowRequestType::Rectangle;
    request.atlas = ShadowAtlasType::Area;
    request.isStaticArea = shadow.updateMode != ShadowUpdateMode::EveryFrame;
    request.lightIndex = lightIndex;
    request.faceIndex = 0;
    request.cascadeIndex = 0;
    request.atlasSize = m_areaAtlasSize;
    request.depthBias = kAreaDepthBias;
    request.normalBias = m_settings.normalBias;
    request.softness = shadow.softness;
    request.nearPlane = nearPlane;
    request.farPlane = farPlane;
    request.slopeBias = m_settings.slopeBias * kReferenceSlopeBias;
    request.blockerSamples = shadow.blockerSamples;
    request.filterSamples = shadow.filterSamples;
    request.minFilterSize = m_settings.minFilterSize;
    request.filter = m_settings.areaFilter;
    const uint32_t requestedResolution = shadow.resolution != 0 ? shadow.resolution : m_settings.areaResolution;
    const uint16_t resolution =
        static_cast<uint16_t>(std::min<uint32_t>(requestedResolution, m_areaAtlasSize));
    if (!packArea(resolution, resolution, request)) {
        return false;
    }
    request.worldTexelSize = computeWorldTexelSize(request.projection, request.viewportWidth);
    request.requestIndex = static_cast<uint32_t>(m_requests.size());
    const uint32_t base = request.requestIndex;
    m_requests.push_back(request);
    ++m_areaRequestCount;
    ++m_shadowedLightCount;
    lightBuilder.setShadowIndex(lightIndex, static_cast<int32_t>(base));
    lightBuilder.setShadowDimmer(lightIndex, shadow.dimmer * distanceFade);
    return true;
}

bool ShadowManager::packPunctual(uint16_t width, uint16_t height, ShadowRequest& request, bool allowResize) {
    if (width == 0 || height == 0) {
        return false;
    }
    const uint16_t savedX = m_punctualCursorX;
    const uint16_t savedY = m_punctualCursorY;
    const uint16_t savedRowHeight = m_punctualRowHeight;
    uint32_t attemptWidth = width;
    uint32_t attemptHeight = height;
    while (attemptWidth >= 64 && attemptHeight >= 64) {
        m_punctualCursorX = savedX;
        m_punctualCursorY = savedY;
        m_punctualRowHeight = savedRowHeight;
        if (attemptWidth <= m_punctualAtlasSize && attemptHeight <= m_punctualAtlasSize) {
            if (m_punctualCursorX + attemptWidth > m_punctualAtlasSize) {
                m_punctualCursorX = 0;
                m_punctualCursorY = static_cast<uint16_t>(m_punctualCursorY + m_punctualRowHeight);
                m_punctualRowHeight = 0;
            }
            if (m_punctualCursorY + attemptHeight <= m_punctualAtlasSize) {
                if (attemptWidth != width || attemptHeight != height) {
                    HBG_LOGW("shadow atlas: reduced punctual request from {}x{} to {}x{}", width, height,
                             attemptWidth, attemptHeight);
                }
                request.viewportX = m_punctualCursorX;
                request.viewportY = m_punctualCursorY;
                request.viewportWidth = static_cast<uint16_t>(attemptWidth);
                request.viewportHeight = static_cast<uint16_t>(attemptHeight);
                m_punctualCursorX = static_cast<uint16_t>(m_punctualCursorX + attemptWidth);
                m_punctualRowHeight = std::max<uint16_t>(m_punctualRowHeight, static_cast<uint16_t>(attemptHeight));
                return true;
            }
        }
        if (!allowResize) {
            break;
        }
        attemptWidth /= 2;
        attemptHeight /= 2;
    }
    m_punctualCursorX = savedX;
    m_punctualCursorY = savedY;
    m_punctualRowHeight = savedRowHeight;
    return false;
}

bool ShadowManager::packArea(uint16_t width, uint16_t height, ShadowRequest& request, bool allowResize) {
    if (width == 0 || height == 0) {
        return false;
    }
    const uint16_t savedX = m_areaCursorX;
    const uint16_t savedY = m_areaCursorY;
    const uint16_t savedRowHeight = m_areaRowHeight;
    uint32_t attemptWidth = width;
    uint32_t attemptHeight = height;
    while (attemptWidth >= 64 && attemptHeight >= 64) {
        m_areaCursorX = savedX;
        m_areaCursorY = savedY;
        m_areaRowHeight = savedRowHeight;
        if (attemptWidth <= m_areaAtlasSize && attemptHeight <= m_areaAtlasSize) {
            if (m_areaCursorX + attemptWidth > m_areaAtlasSize) {
                m_areaCursorX = 0;
                m_areaCursorY = static_cast<uint16_t>(m_areaCursorY + m_areaRowHeight);
                m_areaRowHeight = 0;
            }
            if (m_areaCursorY + attemptHeight <= m_areaAtlasSize) {
                if (attemptWidth != width || attemptHeight != height) {
                    HBG_LOGW("shadow atlas: reduced area request from {}x{} to {}x{}", width, height,
                             attemptWidth, attemptHeight);
                }
                request.viewportX = m_areaCursorX;
                request.viewportY = m_areaCursorY;
                request.viewportWidth = static_cast<uint16_t>(attemptWidth);
                request.viewportHeight = static_cast<uint16_t>(attemptHeight);
                m_areaCursorX = static_cast<uint16_t>(m_areaCursorX + attemptWidth);
                m_areaRowHeight = std::max<uint16_t>(m_areaRowHeight, static_cast<uint16_t>(attemptHeight));
                return true;
            }
        }
        if (!allowResize) {
            break;
        }
        attemptWidth /= 2;
        attemptHeight /= 2;
    }
    m_areaCursorX = savedX;
    m_areaCursorY = savedY;
    m_areaRowHeight = savedRowHeight;
    return false;
}

void ShadowManager::repackAreaRequests() {
    m_areaCursorX = 0;
    m_areaCursorY = 0;
    m_areaRowHeight = 0;
    if (m_areaRequestCount == 0) {
        return;
    }

    const auto packPass = [this](bool staticArea) {
        for (ShadowRequest& request : m_requests) {
            if (request.atlas != ShadowAtlasType::Area || request.isStaticArea != staticArea) {
                continue;
            }
            if (!packArea(request.viewportWidth, request.viewportHeight, request, true)) {
                HBG_LOGW("shadow atlas: area request did not fit after static-first repack");
            }
        }
    };
    packPass(true);
    packPass(false);

    for (ShadowRequest& request : m_requests) {
        if (request.atlas == ShadowAtlasType::Area) {
            request.worldTexelSize = computeWorldTexelSize(request.projection, request.viewportWidth);
        }
    }
}

bool ShadowManager::packPunctualSorted() {
    if (m_settings.atlasSize <= kDefaultShadowAtlasSize && m_settings.maxRequests <= kDefaultShadowRequests) {
        return false;
    }

    struct Group {
        uint32_t first = 0;
        uint32_t count = 0;
        uint16_t width = 0;
        uint16_t height = 0;
    };

    std::vector<Group> groups;
    groups.reserve(m_requests.size());
    for (uint32_t index = 0; index < m_requests.size();) {
        const ShadowRequest& request = m_requests[index];
        if (request.atlas != ShadowAtlasType::Punctual) {
            ++index;
            continue;
        }
        uint32_t end = index + 1;
        while (end < m_requests.size()) {
            const ShadowRequest& next = m_requests[end];
            if (next.atlas != ShadowAtlasType::Punctual || next.lightIndex != request.lightIndex ||
                next.type != request.type) {
                break;
            }
            ++end;
        }
        groups.push_back({index, end - index, request.viewportWidth, request.viewportHeight});
        index = end;
    }
    if (groups.size() < 2) {
        return false;
    }

    std::stable_sort(groups.begin(), groups.end(), [](const Group& lhs, const Group& rhs) {
        const uint32_t lhsArea = static_cast<uint32_t>(lhs.width) * lhs.height;
        const uint32_t rhsArea = static_cast<uint32_t>(rhs.width) * rhs.height;
        if (lhsArea != rhsArea) {
            return lhsArea > rhsArea;
        }
        if (lhs.width != rhs.width) {
            return lhs.width > rhs.width;
        }
        if (lhs.height != rhs.height) {
            return lhs.height > rhs.height;
        }
        return lhs.first < rhs.first;
    });

    const std::vector<ShadowRequest> backup = m_requests;
    const uint16_t savedX = m_punctualCursorX;
    const uint16_t savedY = m_punctualCursorY;
    const uint16_t savedRowHeight = m_punctualRowHeight;
    resetPacking();
    for (const Group& group : groups) {
        for (uint32_t offset = 0; offset < group.count; ++offset) {
            ShadowRequest& request = m_requests[group.first + offset];
            if (!packPunctual(group.width, group.height, request, false)) {
                m_requests = backup;
                m_punctualCursorX = savedX;
                m_punctualCursorY = savedY;
                m_punctualRowHeight = savedRowHeight;
                if (!m_sortedPackingWarningLogged) {
                    HBG_LOGW("shadow atlas: sorted punctual packing did not fit; keeping original layout");
                    m_sortedPackingWarningLogged = true;
                }
                return false;
            }
            request.worldTexelSize = computeWorldTexelSize(request.projection, request.viewportWidth);
        }
    }
    return true;
}

uint64_t ShadowManager::buildDirectionalRequestHash(const RenderScene& scene,
                                                      const ShadowRequest& request) const {
    uint64_t hash = 1469598103934665603ull;
    hashUint(hash, request.requestIndex);
    hashUint(hash, request.lightIndex);
    hashUint(hash, request.cascadeIndex);
    hashUint(hash, request.atlasSize);
    hashUint(hash, request.viewportX);
    hashUint(hash, request.viewportY);
    hashUint(hash, request.viewportWidth);
    hashUint(hash, request.viewportHeight);
    hashMat4(hash, request.viewProj);
    hashMat4(hash, request.worldToShadow);
    hashVec3(hash, request.lightDirection);
    hashFloat(hash, request.depthBias);
    hashFloat(hash, request.normalBias);
    hashFloat(hash, request.softness);
    hashFloat(hash, request.nearPlane);
    hashFloat(hash, request.farPlane);
    hashFloat(hash, request.worldTexelSize);
    hashFloat(hash, request.slopeBias);
    hashFloat(hash, request.fadeScale);
    hashFloat(hash, request.fadeBias);
    hashFloat(hash, request.lightAngle);
    hashFloat(hash, request.maxDepthBias);
    hashUint(hash, request.imsKernelSize);
    hashUint(hash, request.blockerSamples);
    hashUint(hash, request.filterSamples);
    hashFloat(hash, request.minFilterSize);
    hashUint(hash, static_cast<uint32_t>(request.filter));

    uint32_t intersectingObjects = 0;
    for (const RenderObject& object : scene.objects) {
        if (intersectsFrustum(request.frustumPlanes, object.worldBounds)) {
            ++intersectingObjects;
        }
    }
    hashUint(hash, intersectingObjects);
    for (const RenderObject& object : scene.objects) {
        if (!intersectsFrustum(request.frustumPlanes, object.worldBounds)) {
            continue;
        }
        hashMat4(hash, object.world);
        hashUint(hash, object.meshIndex);
        hashUint(hash, object.lightLayerMask);
    }

    hashUint(hash, static_cast<uint32_t>(scene.meshes.size()));
    for (const asset::Mesh& mesh : scene.meshes) {
        hashUint(hash, static_cast<uint32_t>(mesh.submeshes.size()));
        for (const asset::SubMesh& submesh : mesh.submeshes) {
            hashUint(hash, submesh.materialIndex);
            hashUint(hash, submesh.indexCount);
        }
    }
    hashUint(hash, static_cast<uint32_t>(scene.materials.size()));
    for (const asset::Material& material : scene.materials) {
        hashUint(hash, static_cast<uint32_t>(material.alphaMode));
        hashFloat(hash, material.alphaCutoff);
        hashFloat(hash, material.baseColorFactor.x);
        hashFloat(hash, material.baseColorFactor.y);
        hashFloat(hash, material.baseColorFactor.z);
        hashFloat(hash, material.baseColorFactor.w);
        hashUint(hash, static_cast<uint32_t>(material.renderQueue));
        hashUint(hash, material.doubleSided ? 1u : 0u);
        hashUint(hash, material.features);
        hashUint(hash, static_cast<uint32_t>(material.baseColorTexture.imageIndex));
        hashUint(hash, static_cast<uint32_t>(material.baseColorTexture.uvSet));
    }
    return hash;
}

uint64_t ShadowManager::buildCacheHash(const RenderScene& scene, const LightBuilder& lightBuilder,
                                       ShadowAtlasType atlas, bool staticAreaOnly) const {
    uint64_t hash = 1469598103934665603ull;
    hashUint(hash, static_cast<uint32_t>(scene.objects.size()));
    for (const RenderObject& object : scene.objects) {
        hashMat4(hash, object.world);
        hashUint(hash, object.meshIndex);
    }

    hashUint(hash, static_cast<uint32_t>(scene.meshes.size()));
    for (const asset::Mesh& mesh : scene.meshes) {
        hashUint(hash, static_cast<uint32_t>(mesh.submeshes.size()));
        for (const asset::SubMesh& submesh : mesh.submeshes) {
            hashUint(hash, submesh.materialIndex);
            hashUint(hash, submesh.indexCount);
        }
    }

    hashUint(hash, static_cast<uint32_t>(scene.materials.size()));
    for (const asset::Material& material : scene.materials) {
        hashUint(hash, static_cast<uint32_t>(material.alphaMode));
        hashFloat(hash, material.alphaCutoff);
        hashFloat(hash, material.baseColorFactor.x);
        hashFloat(hash, material.baseColorFactor.y);
        hashFloat(hash, material.baseColorFactor.z);
        hashFloat(hash, material.baseColorFactor.w);
        hashUint(hash, static_cast<uint32_t>(material.renderQueue));
        hashUint(hash, material.doubleSided ? 1u : 0u);
        hashUint(hash, material.features);
        hashUint(hash, static_cast<uint32_t>(material.baseColorTexture.imageIndex));
        hashUint(hash, static_cast<uint32_t>(material.baseColorTexture.uvSet));
    }

    const auto& selectedLights = lightBuilder.selectedLights();
    const auto lightMatchesAtlas = [this, atlas, staticAreaOnly](const Light& light) {
        if (atlas == ShadowAtlasType::Directional) {
            return light.type == LightType::Directional;
        }
        if (atlas == ShadowAtlasType::Area) {
            if (light.type != LightType::Area || light.areaShape != LightAreaShape::Rectangle) {
                return false;
            }
            if (staticAreaOnly) {
                const LightShadowSettings shadow = resolveLightShadow(light, m_settings);
                return shadow.updateMode != ShadowUpdateMode::EveryFrame;
            }
            return true;
        }
        return light.type == LightType::Point || light.type == LightType::Spot;
    };
    uint32_t matchingLightCount = 0;
    for (const Light* light : selectedLights) {
        if (light != nullptr && lightMatchesAtlas(*light)) {
            ++matchingLightCount;
        }
    }
    hashUint(hash, matchingLightCount);
    for (const Light* light : selectedLights) {
        if (light == nullptr || !lightMatchesAtlas(*light)) {
            continue;
        }
        hashUint(hash, static_cast<uint32_t>(light->type));
        hashMat4(hash, light->world);
        hashVec3(hash, light->color);
        hashFloat(hash, light->intensity);
        hashFloat(hash, light->range);
        hashFloat(hash, light->innerConeAngle);
        hashFloat(hash, light->outerConeAngle);
        hashFloat(hash, light->areaWidth);
        hashFloat(hash, light->areaHeight);
        hashFloat(hash, light->areaShadowCone);
        hashUint(hash, light->castShadows ? 1u : 0u);
        hashUint(hash, light->shadowConfigured ? 1u : 0u);
        const LightShadowSettings shadow = resolveLightShadow(*light, m_settings);
        hashFloat(hash, shadow.dimmer);
        hashFloat(hash, shadow.fadeDistance);
        hashFloat(hash, shadow.nearPlane);
        hashUint(hash, shadow.resolution);
        hashVec3(hash, shadow.tint);
        hashUint(hash, shadow.penumbraTint ? 1u : 0u);
        hashUint(hash, static_cast<uint32_t>(shadow.updateMode));
        hashUint(hash, shadow.contactShadows ? 1u : 0u);
        hashFloat(hash, shadow.softness);
        hashUint(hash, shadow.blockerSamples);
        hashUint(hash, shadow.filterSamples);
    }

    hashUint(hash, m_settings.maxRequests);
    hashFloat(hash, m_settings.normalBias);
    hashFloat(hash, m_settings.slopeBias);
    hashFloat(hash, m_settings.softness);
    hashUint(hash, m_settings.blockerSamples);
    hashUint(hash, m_settings.filterSamples);
        if (atlas == ShadowAtlasType::Directional) {
        hashUint(hash, m_settings.directionalResolution);
        hashUint(hash, m_settings.cascadeCount);
        hashUint(hash, static_cast<uint32_t>(m_settings.directionalFilter));
        for (float split : m_settings.cascadeSplits) {
            hashFloat(hash, split);
        }
        hashFloat(hash, m_settings.cascadeFade);
    } else if (atlas == ShadowAtlasType::Area) {
        hashUint(hash, m_settings.areaResolution);
        hashUint(hash, m_settings.areaAtlasSize);
        hashUint(hash, static_cast<uint32_t>(m_settings.areaFilter));
    } else {
        hashUint(hash, m_settings.pointResolution);
        hashUint(hash, m_settings.spotResolution);
        hashUint(hash, m_settings.atlasSize);
        hashUint(hash, static_cast<uint32_t>(m_settings.punctualFilter));
    }

    const auto requestMatchesAtlas = [atlas, staticAreaOnly](const ShadowRequest& request) {
        return request.atlas == atlas && (!staticAreaOnly || request.isStaticArea);
    };
    uint32_t requestCount = 0;
    for (const ShadowRequest& request : m_requests) {
        if (requestMatchesAtlas(request)) {
            ++requestCount;
        }
    }
    hashUint(hash, requestCount);
    for (const ShadowRequest& request : m_requests) {
        if (!requestMatchesAtlas(request)) {
            continue;
        }
        hashUint(hash, static_cast<uint32_t>(request.type));
        hashUint(hash, static_cast<uint32_t>(request.atlas));
        hashUint(hash, request.lightIndex);
        hashUint(hash, request.faceIndex);
        hashUint(hash, request.viewportX);
        hashUint(hash, request.viewportY);
        hashUint(hash, request.viewportWidth);
        hashUint(hash, request.viewportHeight);
        hashMat4(hash, request.viewProj);
        hashMat4(hash, request.worldToShadow);
    }
    return hash;
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
            Vec4(static_cast<float>(request.type), static_cast<float>(request.faceIndex),
                 static_cast<float>(shadowFilterTapMode(request.filter)), 1.0f);
        m_contextData.texels[base + 7] =
            Vec4(request.softness, request.nearPlane, request.worldTexelSize, request.slopeBias);
        m_contextData.texels[base + 8] =
            Vec4(request.fadeScale, request.fadeBias, static_cast<float>(request.cascadeIndex),
                 request.isCached ? 1.0f : 0.0f);
        // IMS uses the PCSS parameter texel for per-cascade soft-shadow
        // parameters; HDRP carries these in HDShadowData per request.
        m_contextData.texels[base + 9] =
            request.filter == ShadowFilter::Ims
                ? computeImsParams(request)
                : Vec4(static_cast<float>(request.blockerSamples), static_cast<float>(request.filterSamples),
                       request.minFilterSize, request.farPlane);
    }
    m_contextData.requestCount = static_cast<uint32_t>(std::min<std::size_t>(m_requests.size(), kMaxShadowRequests));
}

void ShadowManager::uploadContext() {
    if (m_resources == nullptr || !m_contextBuffer.valid()) {
        return;
    }
    m_resources->updateBuffer(m_contextBuffer, m_contextData.texels.data(),
                              static_cast<uint32_t>(sizeof(m_contextData.texels)));
    if (m_areaRequestCount != 0 && m_contextAreaBuffer.valid()) {
        m_resources->updateBuffer(m_contextAreaBuffer, m_contextData.texels.data(),
                                  static_cast<uint32_t>(sizeof(m_contextData.texels)));
    }
}

void ShadowManager::beginCachedPass(gfx::Encoder& encoder, bgfx::ViewId view,
                                    gfx::FrameBufferHandle frameBuffer, uint16_t atlasSize) const {
    if (!frameBuffer.valid()) {
        return;
    }
    encoder.setViewRect(view, 0, 0, atlasSize, atlasSize);
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_NONE, 0x000000ff, 1.0f, 0);
    encoder.touch(view);
}

void ShadowManager::beginPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                              const ShadowRequest& request) const {
    if (!request.enabled || !frameBuffer.valid()) {
        return;
    }
    encoder.setViewRect(view, request.viewportX, request.viewportY, request.viewportWidth, request.viewportHeight);
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewTransform(view, request.view.data(), request.projection.data());
    encoder.setViewDepthBias(view, 1, request.slopeBias, 0.0f);
    encoder.setViewClear(view, BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
    encoder.touch(view);
}

} // namespace hbg::render
