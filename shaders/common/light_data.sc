#ifndef HBG_LIGHT_DATA_SC
#define HBG_LIGHT_DATA_SC

#include "common/common.sc"

// LightData texture layout: RGBA32F, 4 texels per light.
// texel0: position.xyz, range
// texel1: color.rgb, intensity
// texel2: direction.xyz, cosOuterAngle
// texel3: shadowIndex, cosInnerAngle, layerMask, flags
SAMPLER2D(s_lightData, 5);
USAMPLER2D(s_lightList, 7);

#define HBG_LIGHT_LIST_STRIDE 33

struct HbgLightData
{
    vec4 positionRange;
    vec4 colorIntensity;
    vec4 directionCone;
    vec4 shadowFlags;
};

HbgLightData hbgLoadLightData(int index)
{
    HbgLightData light;
    light.positionRange = texelFetch(s_lightData, ivec2(0, index), 0);
    light.colorIntensity = texelFetch(s_lightData, ivec2(1, index), 0);
    light.directionCone = texelFetch(s_lightData, ivec2(2, index), 0);
    light.shadowFlags = texelFetch(s_lightData, ivec2(3, index), 0);
    return light;
}

ivec2 hbgLightTileCoord(vec3 worldPosition)
{
    const int tileSize = max(int(u_pass_lightListParams.x + 0.5), 1);
    const int tileCountX = max(int(u_pass_lightListParams.y + 0.5), 1);
    const int tileCountY = max(int(u_pass_lightListParams.z + 0.5), 1);
    const vec4 clip = mul(u_global_viewProj, vec4(worldPosition, 1.0));
    const float invW = 1.0 / max(clip.w, 1.0e-6);
    const vec2 pixel = (clip.xy * invW * 0.5 + 0.5) * u_global_screenSize.xy;
    const ivec2 tile = ivec2(pixel / float(tileSize));
    return clamp(tile, ivec2(0, 0), ivec2(tileCountX - 1, tileCountY - 1));
}

uint hbgLightListCount(ivec2 tile)
{
    return texelFetch(s_lightList, ivec2(tile.x * HBG_LIGHT_LIST_STRIDE, tile.y), 0).x;
}

uint hbgLightListIndex(ivec2 tile, int lightOffset)
{
    return texelFetch(s_lightList, ivec2(tile.x * HBG_LIGHT_LIST_STRIDE + lightOffset + 1, tile.y), 0).x;
}

int hbgLightType(vec4 shadowFlags)
{
    return int(shadowFlags.w + 0.5) & 3;
}

bool hbgLightCastShadow(vec4 shadowFlags)
{
    return (int(shadowFlags.w + 0.5) & 4) != 0;
}

float hbgDistanceAttenuation(float distanceSq, float range)
{
    const float minDistanceSq = 1.0e-4;
    const float rangeSq = max(range * range, minDistanceSq);
    const float distanceRatioSq = max(distanceSq, minDistanceSq) / rangeSq;
    const float window = hbgSaturate(1.0 - distanceRatioSq * distanceRatioSq);
    return (window * window) / max(distanceSq, minDistanceSq);
}

float hbgSpotAttenuation(float cosTheta, float cosOuter, float cosInner)
{
    const float denominator = max(cosInner - cosOuter, 1.0e-4);
    const float t = hbgSaturate((cosTheta - cosOuter) / denominator);
    return t * t * (3.0 - 2.0 * t);
}

#endif // HBG_LIGHT_DATA_SC
