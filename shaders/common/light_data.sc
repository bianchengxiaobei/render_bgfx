#ifndef HBG_LIGHT_DATA_SC
#define HBG_LIGHT_DATA_SC


// LightData texture layout: RGBA32F, 6 texels per light.
// texel0: position.xyz, range
// texel1: color.rgb, intensity
// texel2: direction.xyz, cosOuterAngle
// texel3: shadowIndex, cosInnerAngle, contactShadowMask, flags
// texel4: shadowTint.rgb, shadowDimmer
// texel5: shadowFadeDistance, penumbraTint, flags2, unused
SAMPLER2D(s_lightData, 5);
#ifdef HBG_LIGHT_LIST_BUFFER
#if BGFX_SHADER_LANGUAGE_GLSL
layout(std430, binding = 7) readonly buffer HbgLightListBuffer
{
    uint s_lightList[];
};
#elif BGFX_SHADER_LANGUAGE_METAL || BGFX_SHADER_LANGUAGE_SPIRV || BGFX_SHADER_LANGUAGE_WGSL
StructuredBuffer<uint> s_lightList : register(t7);
#else
Buffer<uint> s_lightList : register(t7);
#endif
#else
USAMPLER2D(s_lightList, 7);
#endif

#define HBG_LIGHT_LIST_STRIDE 33
#define HBG_LIGHT_CLUSTER_STRIDE 33
#define HBG_LIGHT_CLUSTER_MAX_LIGHTS 32
#define HBG_LIGHT_CLUSTER_SLICE_COUNT 32.0

struct HbgLightData
{
    vec4 positionRange;
    vec4 colorIntensity;
    vec4 directionCone;
    vec4 shadowFlags;
    vec4 shadowTintDimmer;
    vec4 shadowFadeFlags;
};

HbgLightData hbgLoadLightData(int index)
{
    HbgLightData light;
    light.positionRange = texelFetch(s_lightData, ivec2(0, index), 0);
    light.colorIntensity = texelFetch(s_lightData, ivec2(1, index), 0);
    light.directionCone = texelFetch(s_lightData, ivec2(2, index), 0);
    light.shadowFlags = texelFetch(s_lightData, ivec2(3, index), 0);
    light.shadowTintDimmer = texelFetch(s_lightData, ivec2(4, index), 0);
    light.shadowFadeFlags = texelFetch(s_lightData, ivec2(5, index), 0);
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
#ifdef HBG_LIGHT_LIST_BUFFER
    const int tileCountX = max(int(u_pass_lightListParams.y + 0.5), 1);
    const int base = (tile.y * tileCountX + tile.x) * HBG_LIGHT_LIST_STRIDE;
    return s_lightList[base];
#else
    return texelFetch(s_lightList, ivec2(tile.x * HBG_LIGHT_LIST_STRIDE, tile.y), 0).x;
#endif
}

uint hbgLightListIndex(ivec2 tile, int lightOffset)
{
#ifdef HBG_LIGHT_LIST_BUFFER
    const int tileCountX = max(int(u_pass_lightListParams.y + 0.5), 1);
    const int base = (tile.y * tileCountX + tile.x) * HBG_LIGHT_LIST_STRIDE;
    return s_lightList[base + lightOffset + 1];
#else
    return texelFetch(s_lightList, ivec2(tile.x * HBG_LIGHT_LIST_STRIDE + lightOffset + 1, tile.y), 0).x;
#endif
}

float hbgLinearDepth(vec3 worldPosition)
{
    return max(-mul(u_global_view, vec4(worldPosition, 1.0)).z, 0.0);
}

int hbgClusterSliceForDepth(float linearDepth)
{
    const float nearPlane = max(u_cluster_depthParams.x, 0.0001);
    const float farPlane = max(u_cluster_depthParams.y, nearPlane + 0.0001);
    const float rangeFittedDistance = hbgSaturate((linearDepth - nearPlane) / (farPlane - nearPlane));
    const float clusterCount = HBG_LIGHT_CLUSTER_SLICE_COUNT;
    const float logBase = max(u_cluster_depthParams.z, 1.0001);
    const float scale = pow(logBase, clusterCount);
    const float slice = log(1.0 + rangeFittedDistance * (scale - 1.0)) / log(logBase);
    return clamp(int(floor(slice)), 0, int(clusterCount) - 1);
}

ivec2 hbgLightClusterTileCoord(vec3 worldPosition)
{
    const int tileSize = max(int(u_cluster_params.x + 0.5), 1);
    const int tileCountX = max(int(u_cluster_params.y + 0.5), 1);
    const int tileCountY = max(int(u_cluster_params.z + 0.5), 1);
    const vec4 clip = mul(u_global_viewProj, vec4(worldPosition, 1.0));
    const float invW = 1.0 / max(clip.w, 1.0e-6);
    const vec2 pixel = (clip.xy * invW * 0.5 + 0.5) * u_global_screenSize.xy;
    const ivec2 tile = ivec2(pixel / float(tileSize));
    return clamp(tile, ivec2(0, 0), ivec2(tileCountX - 1, tileCountY - 1));
}

uint hbgClusterLightListCount(ivec2 tile, int slice)
{
#ifdef HBG_LIGHT_LIST_BUFFER
    const int tileCountX = max(int(u_cluster_params.y + 0.5), 1);
    const int row = clamp(tile.y * int(HBG_LIGHT_CLUSTER_SLICE_COUNT) + slice, 0, 32767);
    return s_lightList[(row * tileCountX + tile.x) * HBG_LIGHT_CLUSTER_STRIDE];
#else
    const int sliceCount = int(HBG_LIGHT_CLUSTER_SLICE_COUNT);
    const int row = clamp(tile.y * sliceCount + slice, 0, 32767);
    return texelFetch(s_lightList, ivec2(tile.x * HBG_LIGHT_CLUSTER_STRIDE, row), 0).x;
#endif
}

uint hbgClusterLightListIndex(ivec2 tile, int slice, int lightOffset)
{
#ifdef HBG_LIGHT_LIST_BUFFER
    const int tileCountX = max(int(u_cluster_params.y + 0.5), 1);
    const int row = clamp(tile.y * int(HBG_LIGHT_CLUSTER_SLICE_COUNT) + slice, 0, 32767);
    return s_lightList[(row * tileCountX + tile.x) * HBG_LIGHT_CLUSTER_STRIDE + lightOffset + 1];
#else
    const int sliceCount = int(HBG_LIGHT_CLUSTER_SLICE_COUNT);
    const int row = clamp(tile.y * sliceCount + slice, 0, 32767);
    return texelFetch(s_lightList, ivec2(tile.x * HBG_LIGHT_CLUSTER_STRIDE + lightOffset + 1, row), 0).x;
#endif
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
