#include "bgfx_compute.sh"
#include "common/common.sc"
#include "common/light_data.sc"

BUFFER_WO(s_lightScreenBounds, vec4, 1);
SAMPLER2D(s_lightViewRows, 6);

#define HBG_LIGHT_LIST_MAX_LIGHTS 128u

bool hbgUsesOrientedBounds(const HbgLightData light, int lightType)
{
    return (lightType == int(3) || lightType == int(4)) &&
           dot(light.directionCone.xyz, light.directionCone.xyz) > 1.0e-8 &&
           dot(light.rightHalfWidth.xyz, light.rightHalfWidth.xyz) > 1.0e-8 &&
           dot(light.upHalfHeight.xyz, light.upHalfHeight.xyz) > 1.0e-8;
}

vec3 hbgLightBoundsCorner(const HbgLightData light, int corner)
{
    const int lightType = hbgLightType(light.shadowFlags);
    const vec3 position = light.positionRange.xyz;
    const float range = max(light.positionRange.w, 0.0);
    if (lightType == int(3) && dot(light.directionCone.xyz, light.directionCone.xyz) > 1.0e-8 && dot(light.rightHalfWidth.xyz, light.rightHalfWidth.xyz) > 1.0e-8 && dot(light.upHalfHeight.xyz, light.upHalfHeight.xyz) > 1.0e-8)
    {
        const vec3 forward = normalize(light.directionCone.xyz);
        const vec3 right = normalize(light.rightHalfWidth.xyz);
        const vec3 up = normalize(light.upHalfHeight.xyz);
        const float extentX = max(light.rightHalfWidth.w, 0.0) + range;
        const float extentY = max(light.upHalfHeight.w, 0.0) + range;
        const float extentZ = range * 0.5;
        const vec3 center = position + forward * extentZ;
        return center +
               right * (((corner & 1) != 0) ? extentX : -extentX) +
               up * (((corner & 2) != 0) ? extentY : -extentY) +
               forward * (((corner & 4) != 0) ? extentZ : -extentZ);
    }
    if (lightType == int(4) && dot(light.directionCone.xyz, light.directionCone.xyz) > 1.0e-8 && dot(light.rightHalfWidth.xyz, light.rightHalfWidth.xyz) > 1.0e-8 && dot(light.upHalfHeight.xyz, light.upHalfHeight.xyz) > 1.0e-8)
    {
        const vec3 forward = normalize(light.directionCone.xyz);
        const vec3 right = normalize(light.rightHalfWidth.xyz);
        const vec3 up = normalize(light.upHalfHeight.xyz);
        const float extentX = max(light.rightHalfWidth.w, 0.0) + range;
        return position +
               right * (((corner & 1) != 0) ? extentX : -extentX) +
               up * (((corner & 2) != 0) ? range : -range) +
               forward * (((corner & 4) != 0) ? range : -range);
    }
    const float extent = max(range, 0.001);
    return position + vec3((corner & 1) != 0 ? extent : -extent,
                           (corner & 2) != 0 ? extent : -extent,
                           (corner & 4) != 0 ? extent : -extent);
}

void hbgStoreZeroBounds(uint lightIndex)
{
    s_lightScreenBounds[lightIndex * 2u] = vec4(0.0, 0.0, 0.0, 0.0);
    s_lightScreenBounds[lightIndex * 2u + 1u] = vec4(0.0, 0.0, 0.0, 0.0);
}

NUM_THREADS(64, 1, 1)

void main()
{
    const uint thread = gl_LocalInvocationIndex;
    const uint lightCount =
        min(uint(max(u_pass_lightCounts.x, 0.0) + 0.5), HBG_LIGHT_LIST_MAX_LIGHTS);
    const uint directionalCount = min(uint(max(u_pass_lightCounts.y, 0.0) + 0.5), lightCount);
    const uint capacity =
        min(uint(max(u_pass_lightListParams2.y, 0.0) + 0.5), HBG_LIGHT_LIST_MAX_LIGHTS);
    const vec2 screenSize = max(u_pass_lightScreenSize.xy, vec2(1.0, 1.0));
    const vec4 viewProj0 = texelFetch(s_lightViewRows, ivec2(0, 0), 0);
    const vec4 viewProj1 = texelFetch(s_lightViewRows, ivec2(1, 0), 0);
    const vec4 viewProj2 = texelFetch(s_lightViewRows, ivec2(2, 0), 0);
    const vec4 viewProj3 = texelFetch(s_lightViewRows, ivec2(3, 0), 0);
    const vec4 viewRow2 = texelFetch(s_lightViewRows, ivec2(4, 0), 0);

    for (uint lightIndex = thread; lightIndex < HBG_LIGHT_LIST_MAX_LIGHTS; lightIndex += 64u)
    {
        if (lightIndex < directionalCount || lightIndex >= lightCount || lightIndex >= capacity)
        {
            hbgStoreZeroBounds(lightIndex);
            continue;
        }

        const HbgLightData light = hbgLoadLightData(int(lightIndex));
        if (hbgLightType(light.shadowFlags) == 0)
        {
            hbgStoreZeroBounds(lightIndex);
            continue;
        }

        const int lightType = hbgLightType(light.shadowFlags);
        const bool oriented = hbgUsesOrientedBounds(light, lightType);
        const vec3 position = light.positionRange.xyz;
        const float range = max(light.positionRange.w, 0.0);
        const float extent = max(range, 0.001);

        float minX = 3.402823466e+38;
        float maxX = -3.402823466e+38;
        float minY = 3.402823466e+38;
        float maxY = -3.402823466e+38;
        float nearestDepth = 3.402823466e+38;
        float farthestDepth = -3.402823466e+38;
        bool anyBehind = false;
        bool anyInFront = false;

        for (int corner = 0; corner < 8; ++corner)
        {
            const vec3 cornerPosition = hbgLightBoundsCorner(light, corner);
            const vec4 corner = vec4(cornerPosition, 1.0);
            const vec4 clip = vec4(dot(viewProj0, corner), dot(viewProj1, corner),
                                   dot(viewProj2, corner), dot(viewProj3, corner));
            if (clip.w <= 0.0)
            {
                anyBehind = true;
                continue;
            }
            anyInFront = true;

            if (oriented)
            {
                const float cornerDepth = -dot(viewRow2, corner);
                nearestDepth = min(nearestDepth, cornerDepth);
                farthestDepth = max(farthestDepth, cornerDepth);
            }

            const vec2 pixel = (clip.xy / clip.w * 0.5 + 0.5) * screenSize;
            if (!any(isnan(pixel)) && !any(isinf(pixel)))
            {
                minX = min(minX, pixel.x);
                maxX = max(maxX, pixel.x);
                minY = min(minY, pixel.y);
                maxY = max(maxY, pixel.y);
            }
        }

        if (!anyInFront && oriented)
        {
            hbgStoreZeroBounds(lightIndex);
            continue;
        }
        if (anyBehind || !anyInFront)
        {
            minX = 0.0;
            maxX = screenSize.x;
            minY = 0.0;
            maxY = screenSize.y;
        }
        else if (oriented && (maxX <= 0.0 || minX >= screenSize.x || maxY <= 0.0 || minY >= screenSize.y))
        {
            hbgStoreZeroBounds(lightIndex);
            continue;
        }

        minX = clamp(minX, 0.0, screenSize.x);
        maxX = clamp(maxX, 0.0, screenSize.x);
        minY = clamp(minY, 0.0, screenSize.y);
        maxY = clamp(maxY, 0.0, screenSize.y);

        if (maxX < minX || maxY < minY)
        {
            hbgStoreZeroBounds(lightIndex);
            continue;
        }

        float minDepth;
        float maxDepth;
        if (oriented)
        {
            const float nearClip = max(u_cluster_depthParams.x, 0.0);
            const float farClip = max(u_cluster_depthParams.y, nearClip);
            if (anyBehind || !anyInFront)
            {
                minDepth = nearClip;
                maxDepth = farClip;
            }
            else
            {
                minDepth = clamp(nearestDepth, nearClip, farClip);
                maxDepth = clamp(farthestDepth, nearClip, farClip);
            }
            if (maxDepth < minDepth)
            {
                hbgStoreZeroBounds(lightIndex);
                continue;
            }
        }
        else
        {
            const float centerDepth = -dot(viewRow2, vec4(position, 1.0));
            minDepth = max(centerDepth - extent, 0.0);
            maxDepth = centerDepth + extent;
        }

        s_lightScreenBounds[lightIndex * 2u] = vec4(minX, minY, minDepth, maxDepth);
        s_lightScreenBounds[lightIndex * 2u + 1u] = vec4(maxX, maxY, 1.0, light.layerCategoryFlags.y);
    }
}
