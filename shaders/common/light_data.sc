#ifndef HBG_LIGHT_DATA_SC
#define HBG_LIGHT_DATA_SC


// LightData texture layout: RGBA32F, 19 texels per light (11 base + 8 IES).
// Cookies live in a 64x128 atlas region at x=19..82, y=0..127. The LTC GGX table
// is appended after the light rows and fetched through the same texture.
// texel0: position.xyz, range
// texel1: color.rgb, intensity
// texel2: direction.xyz, cosOuterAngle
// texel3: shadowIndex, cosInnerAngle, contactShadowMask, flags
// texel4: shadowTint.rgb, shadowDimmer
// texel5: shadowFadeDistance, penumbraTint, iesCut, areaShadowCone
// texel6: lightLayerMask, category, featureFlags, unused
// texel7: cookieMode, cookieType, cookieAtlasTileIndex+1, areaShape | hasIes<<3
// texel8: right.xyz, halfWidth
// texel9: up.xyz, halfHeight
// texel10: cookieScale.xy, cookieOffset.xy
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

#define HBG_LIGHT_LIST_MAX_LIGHTS 128
#define HBG_LIGHT_LIST_COUNT_MASK 0xffffu
#define HBG_LIGHT_LIST_HALFWORD_MASK 0xffffu
#define HBG_LIGHT_CATEGORY_COUNT 4
#define HBG_LIGHT_CLUSTER_COUNT_BITS 8
#define HBG_LIGHT_CLUSTER_OFFSET_BITS 24
#define HBG_LIGHT_CLUSTER_COUNT_MASK 0xffu
#define HBG_LIGHT_CLUSTER_OFFSET_MASK 0x00ffffffu
#define HBG_LIGHT_CLUSTER_TEXTURE_WIDTH 4096
#define HBG_LIGHT_CLUSTER_TEXTURE_SHIFT 12
#define HBG_LIGHT_CLUSTER_SLICE_COUNT 32.0
#define HBG_LIGHT_TEXELS 19
#define HBG_LIGHT_IES_BASE 11
#define HBG_LIGHT_IES_SAMPLES 8
#define HBG_COOKIE_ATLAS_ORIGIN_X 19
#define HBG_COOKIE_ATLAS_WIDTH 64
#define HBG_COOKIE_ATLAS_HEIGHT 128
#define HBG_COOKIE_TILE_SIZE 8
#define HBG_COOKIE_TILES_X 8
#define HBG_COOKIE_TILES_X_SHIFT 3
#define HBG_COOKIE_TILES_Y 16
#define HBG_LTC_LUT_SIZE 64
#define HBG_LTC_TEXEL_ROWS 64
#define HBG_LTC_BASE_ROW 128
#define HBG_LIGHT_COOKIE_MODE_NONE 0
#define HBG_LIGHT_COOKIE_MODE_CLAMP 1
#define HBG_LIGHT_COOKIE_MODE_REPEAT 2
#define HBG_LIGHT_COOKIE_TYPE_2D 1
#define HBG_LIGHT_COOKIE_TYPE_OCTA 2
#define HBG_LIGHT_CATEGORY_PUNCTUAL 0
#define HBG_LIGHT_CATEGORY_AREA 1
#define HBG_LIGHT_LIST_CATEGORY_COUNT 2
#define HBG_LIGHT_FEATURE_PUNCTUAL 1
#define HBG_LIGHT_FEATURE_AREA 2
#define HBG_LIGHT_FEATURE_DIRECTIONAL 4
#define HBG_LIGHT_FEATURE_FLAGS_MASK 0xffffu



int hbgLightListStride()
{
    return max(int(u_pass_lightListParams2.x + 0.5), 1);
}

int hbgLightListCapacity()
{
    return clamp(int(u_pass_lightListParams2.y + 0.5), 1, HBG_LIGHT_LIST_MAX_LIGHTS);
}

struct HbgLightData
{
    vec4 positionRange;
    vec4 colorIntensity;
    vec4 directionCone;
    vec4 shadowFlags;
    vec4 shadowTintDimmer;
    vec4 shadowFadeFlags;
    vec4 layerCategoryFlags;
    vec4 cookieInfo;
    vec4 rightHalfWidth;
    vec4 upHalfHeight;
    vec4 cookieTransform;
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
    light.layerCategoryFlags = texelFetch(s_lightData, ivec2(6, index), 0);
    light.cookieInfo = texelFetch(s_lightData, ivec2(7, index), 0);
    light.rightHalfWidth = texelFetch(s_lightData, ivec2(8, index), 0);
    light.upHalfHeight = texelFetch(s_lightData, ivec2(9, index), 0);
    light.cookieTransform = texelFetch(s_lightData, ivec2(10, index), 0);
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

uint hbgLightFeatureFlags(ivec2 tile)
{
    const int tileCountX = max(int(u_pass_lightListParams.y + 0.5), 1);
    const int tileCountY = max(int(u_pass_lightListParams.z + 0.5), 1);
    const int stride = hbgLightListStride();
#ifdef HBG_LIGHT_LIST_BUFFER
    const int base = ((tile.y * tileCountX) + tile.x) * stride;
    return (s_lightList[base + stride - 1] >> 16u) & HBG_LIGHT_FEATURE_FLAGS_MASK;
#else
    return (texelFetch(s_lightList, ivec2(tile.x * stride + stride - 1, tile.y), 0).x >> 16u) &
           HBG_LIGHT_FEATURE_FLAGS_MASK;
#endif
}

uint hbgClusterFeatureFlags(ivec2 tile)
{
    const int clusterCountX = max(int(u_cluster_params.y + 0.5), 1);
    const int offsetBase = int(max(u_cluster_params2.y, 0.0) + 0.5);
    const int indexCapacity = int(max(u_cluster_params2.z, 0.0) + 0.5);
    const int position = offsetBase + indexCapacity + tile.y * clusterCountX + tile.x;
#ifdef HBG_LIGHT_LIST_BUFFER
    return s_lightList[position] & HBG_LIGHT_FEATURE_FLAGS_MASK;
#else
    return texelFetch(s_lightList, ivec2(position & (HBG_LIGHT_CLUSTER_TEXTURE_WIDTH - 1),
                                         position >> HBG_LIGHT_CLUSTER_TEXTURE_SHIFT), 0).x &
           HBG_LIGHT_FEATURE_FLAGS_MASK;
#endif
}

uint hbgLightListCount(ivec2 tile, int category)
{
#ifdef HBG_LIGHT_LIST_BUFFER
    const int tileCountX = max(int(u_pass_lightListParams.y + 0.5), 1);
    const int tileCountY = max(int(u_pass_lightListParams.z + 0.5), 1);
    const int base = ((category * tileCountY + tile.y) * tileCountX + tile.x) * hbgLightListStride();
    return s_lightList[base] & HBG_LIGHT_LIST_COUNT_MASK;
#else
    const int tileCountY = max(int(u_pass_lightListParams.z + 0.5), 1);
    return texelFetch(s_lightList, ivec2(tile.x * hbgLightListStride(), category * tileCountY + tile.y), 0).x &
           HBG_LIGHT_LIST_COUNT_MASK;
#endif
}

uint hbgLightListIndex(ivec2 tile, int lightOffset, int category)
{
    const int halfword = lightOffset + 1;
    const int wordIndex = halfword >> 1;
    const int shift = (halfword & 1) * 16;
#ifdef HBG_LIGHT_LIST_BUFFER
    const int tileCountX = max(int(u_pass_lightListParams.y + 0.5), 1);
    const int tileCountY = max(int(u_pass_lightListParams.z + 0.5), 1);
    const int base = ((category * tileCountY + tile.y) * tileCountX + tile.x) * hbgLightListStride();
    return (s_lightList[base + wordIndex] >> shift) & HBG_LIGHT_LIST_HALFWORD_MASK;
#else
    const int tileCountY = max(int(u_pass_lightListParams.z + 0.5), 1);
    return (texelFetch(s_lightList, ivec2(tile.x * hbgLightListStride() + wordIndex, category * tileCountY + tile.y), 0).x >> shift) &
           HBG_LIGHT_LIST_HALFWORD_MASK;
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

uint hbgClusterOffsetWord(ivec2 tile, int slice, int category)
{
    const int tileCountX = max(int(u_cluster_params.y + 0.5), 1);
    const int tileCountY = max(int(u_cluster_params.z + 0.5), 1);
    const int row = clamp((category * int(HBG_LIGHT_CLUSTER_SLICE_COUNT) + slice) * tileCountY + tile.y, 0,
                          32767);
    const uint position = uint(row * tileCountX + tile.x);
#ifdef HBG_LIGHT_LIST_BUFFER
    return s_lightList[position];
#else
    return texelFetch(s_lightList, ivec2(int(position) & (HBG_LIGHT_CLUSTER_TEXTURE_WIDTH - 1), int(position) >> HBG_LIGHT_CLUSTER_TEXTURE_SHIFT), 0).x;
#endif
}

uint hbgClusterLightListCount(ivec2 tile, int slice, int category)
{
    return (hbgClusterOffsetWord(tile, slice, category) >> HBG_LIGHT_CLUSTER_OFFSET_BITS) &
           HBG_LIGHT_CLUSTER_COUNT_MASK;
}

uint hbgClusterLightListOffset(ivec2 tile, int slice, int category)
{
    return hbgClusterOffsetWord(tile, slice, category) & HBG_LIGHT_CLUSTER_OFFSET_MASK;
}

uint hbgClusterLightListIndex(ivec2 tile, int slice, int lightOffset, int category)
{
    const uint position =
        uint(max(u_cluster_params2.y, 0.0) + 0.5) + hbgClusterLightListOffset(tile, slice, category) +
        uint(lightOffset);
#ifdef HBG_LIGHT_LIST_BUFFER
    return s_lightList[position];
#else
    return texelFetch(s_lightList, ivec2(int(position) & (HBG_LIGHT_CLUSTER_TEXTURE_WIDTH - 1), int(position) >> HBG_LIGHT_CLUSTER_TEXTURE_SHIFT), 0).x;
#endif
}

bool hbgLightHasIes(vec4 cookieInfo)
{
    return (int(cookieInfo.w + 0.5) & 8) != 0;
}

float hbgSampleLightCookieScaled(int lightIndex, vec2 uv, int mode, vec2 scale, vec2 offset)
{
    if (mode == HBG_LIGHT_COOKIE_MODE_NONE)
    {
        return 1.0;
    }
    uv = uv * scale + offset;
    if (mode == HBG_LIGHT_COOKIE_MODE_REPEAT)
    {
        uv = fract(uv);
    }
    else
    {
        uv = clamp(uv, vec2(0.0, 0.0), vec2(1.0, 1.0));
    }

    const int tileIndex =
        int(texelFetch(s_lightData, ivec2(7, lightIndex), 0).z + 0.5) - 1;
    if (tileIndex < 0)
    {
        return 1.0;
    }

    const int tileX = tileIndex & (HBG_COOKIE_TILES_X - 1);
    const int tileY = tileIndex >> HBG_COOKIE_TILES_X_SHIFT;
    const int x = min(int(uv.x * float(HBG_COOKIE_TILE_SIZE)), HBG_COOKIE_TILE_SIZE - 1);
    const int y = min(int(uv.y * float(HBG_COOKIE_TILE_SIZE)), HBG_COOKIE_TILE_SIZE - 1);
    return texelFetch(s_lightData,
                      ivec2(HBG_COOKIE_ATLAS_ORIGIN_X + tileX * HBG_COOKIE_TILE_SIZE + x,
                            tileY * HBG_COOKIE_TILE_SIZE + y),
                      0).x;
}

float hbgSampleLightCookie(int lightIndex, vec2 uv, int mode)
{
    return hbgSampleLightCookieScaled(lightIndex, uv, mode, vec2(1.0, 1.0), vec2(0.0, 0.0));
}

vec4 hbgFetchLtcTexel(int row, int column)
{
    return texelFetch(s_lightData, ivec2(column, HBG_LTC_BASE_ROW + row), 0);
}

vec4 hbgSampleLtc(int index)
{
    const uint value = uint(max(index, 0));
    const uint row = value >> 6u;
    const uint column = value & 63u;
    return hbgFetchLtcTexel(int(row), int(column));
}

vec4 hbgSampleLtcBilinear(vec2 coordinate)
{
    const vec2 position = clamp(coordinate, vec2(0.0, 0.0), vec2(1.0, 1.0)) * float(HBG_LTC_LUT_SIZE - 1);
    const ivec2 base = ivec2(floor(position));
    const ivec2 next = min(base + ivec2(1, 1), ivec2(HBG_LTC_LUT_SIZE - 1, HBG_LTC_LUT_SIZE - 1));
    const vec2 t = position - vec2(float(base.x), float(base.y));
    const vec4 low = mix(hbgFetchLtcTexel(base.y, base.x), hbgFetchLtcTexel(base.y, next.x), t.x);
    const vec4 high = mix(hbgFetchLtcTexel(next.y, base.x), hbgFetchLtcTexel(next.y, next.x), t.x);
    return mix(low, high, t.y);
}

float hbgSampleLightIes(int lightIndex, float angle)
{
    const float normalized = clamp(angle * (2.0 / HBG_PI), 0.0, 1.0);
    const float position = normalized * float(HBG_LIGHT_IES_SAMPLES - 1);
    const int lower = min(int(floor(position)), HBG_LIGHT_IES_SAMPLES - 1);
    const int upper = min(lower + 1, HBG_LIGHT_IES_SAMPLES - 1);
    const float t = position - floor(position);
    const float lowValue = texelFetch(s_lightData, ivec2(HBG_LIGHT_IES_BASE + lower, lightIndex), 0).x;
    const float highValue = texelFetch(s_lightData, ivec2(HBG_LIGHT_IES_BASE + upper, lightIndex), 0).x;
    return mix(lowValue, highValue, t);
}

vec2 hbgOctahedralEncode(vec3 direction)
{
    const vec3 n = normalize(direction);
    vec2 encoded = n.xy * (1.0 / max(abs(n.x) + abs(n.y) + abs(n.z), 1.0e-6));
    if (n.z < 0.0)
    {
        encoded = (vec2(1.0, 1.0) - abs(encoded.yx)) * vec2(encoded.x >= 0.0 ? 1.0 : -1.0,
                                                          encoded.y >= 0.0 ? 1.0 : -1.0);
    }
    return encoded * 0.5 + 0.5;
}
int hbgLightType(vec4 shadowFlags)
{
    return int(shadowFlags.w + 0.5) & 7;
}

bool hbgLightCastShadow(vec4 shadowFlags)
{
    return (int(shadowFlags.w + 0.5) & 8) != 0;
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
