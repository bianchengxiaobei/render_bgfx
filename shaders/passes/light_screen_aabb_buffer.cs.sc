#include "bgfx_compute.sh"
#include "common/common.sc"
#include "common/light_data.sc"

BUFFER_WO(s_lightScreenBounds, vec4, 1);
SAMPLER2D(s_lightViewRows, 6);

NUM_THREADS(64, 1, 1)

void main()
{
    const uint lightIndex = gl_GlobalInvocationID.x;
    if (lightIndex >= 32u)
    {
        return;
    }

    const uint lightCount = min(uint(max(u_cluster_lightCounts.x, 0.0) + 0.5), 32u);
    const uint directionalCount = min(uint(max(u_cluster_lightCounts.y, 0.0) + 0.5), lightCount);
    const uint minIndex = lightIndex * 2u;
    const uint maxIndex = minIndex + 1u;

    if (lightIndex < directionalCount || lightIndex >= lightCount)
    {
        s_lightScreenBounds[minIndex] = vec4(0.0, 0.0, 0.0, 0.0);
        s_lightScreenBounds[maxIndex] = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const HbgLightData light = hbgLoadLightData(int(lightIndex));
    if (hbgLightType(light.shadowFlags) == 0)
    {
        s_lightScreenBounds[minIndex] = vec4(0.0, 0.0, 0.0, 0.0);
        s_lightScreenBounds[maxIndex] = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const vec3 position = light.positionRange.xyz;
    const float range = max(light.positionRange.w, 0.0);
    const float extent = max(range, 0.001);
    const vec2 screenSize = max(u_cluster_screenSize.xy, vec2(1.0, 1.0));
    const vec4 viewProj0 = texelFetch(s_lightViewRows, ivec2(0, 0), 0);
    const vec4 viewProj1 = texelFetch(s_lightViewRows, ivec2(1, 0), 0);
    const vec4 viewProj2 = texelFetch(s_lightViewRows, ivec2(2, 0), 0);
    const vec4 viewProj3 = texelFetch(s_lightViewRows, ivec2(3, 0), 0);
    const vec4 viewRow2 = texelFetch(s_lightViewRows, ivec2(4, 0), 0);

    float minX = 3.402823466e+38;
    float maxX = -3.402823466e+38;
    float minY = 3.402823466e+38;
    float maxY = -3.402823466e+38;
    bool anyBehind = false;
    bool anyInFront = false;

    for (int corner = 0; corner < 8; ++corner)
    {
        const vec3 cornerPosition = position + vec3((corner & 1) != 0 ? extent : -extent,
                                                    (corner & 2) != 0 ? extent : -extent,
                                                    (corner & 4) != 0 ? extent : -extent);
        const vec4 corner = vec4(cornerPosition, 1.0);
        const vec4 clip = vec4(dot(viewProj0, corner), dot(viewProj1, corner), dot(viewProj2, corner),
                               dot(viewProj3, corner));
        if (clip.w <= 0.0)
        {
            anyBehind = true;
            continue;
        }
        anyInFront = true;

        const vec2 pixel = (clip.xy / clip.w * 0.5 + 0.5) * screenSize;
        if (!any(isnan(pixel)) && !any(isinf(pixel)))
        {
            minX = min(minX, pixel.x);
            maxX = max(maxX, pixel.x);
            minY = min(minY, pixel.y);
            maxY = max(maxY, pixel.y);
        }
    }

    if (anyBehind || !anyInFront)
    {
        minX = 0.0;
        maxX = screenSize.x;
        minY = 0.0;
        maxY = screenSize.y;
    }

    minX = clamp(minX, 0.0, screenSize.x);
    maxX = clamp(maxX, 0.0, screenSize.x);
    minY = clamp(minY, 0.0, screenSize.y);
    maxY = clamp(maxY, 0.0, screenSize.y);

    if (maxX < minX || maxY < minY)
    {
        s_lightScreenBounds[minIndex] = vec4(0.0, 0.0, 0.0, 0.0);
        s_lightScreenBounds[maxIndex] = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const float centerDepth = -dot(viewRow2, vec4(position, 1.0));
    const float minDepth = max(centerDepth - extent, 0.0);
    const float maxDepth = centerDepth + extent;

    s_lightScreenBounds[minIndex] = vec4(minX, minY, minDepth, maxDepth);
    s_lightScreenBounds[maxIndex] = vec4(maxX, maxY, 1.0, 0.0);
}
