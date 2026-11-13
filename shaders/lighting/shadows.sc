#ifndef HBG_SHADOWS_SC
#define HBG_SHADOWS_SC

#include "common/common.sc"

SAMPLER2DSHADOW(s_shadowMap, 6);
SAMPLER2D(s_shadowContext, 8);
SAMPLER2DSHADOW(s_shadowMapPunctual, 9);
#ifdef HBG_SHADOW_POINT_SAMPLER
SAMPLER2D(s_shadowMapPoint, 12);
SAMPLER2D(s_shadowMapPointPunctual, 13);
#endif

mat4 hbgLoadShadowMatrix(int request)
{
    return mat4(texelFetch(s_shadowContext, ivec2(0, request), 0).xyzw,
                texelFetch(s_shadowContext, ivec2(1, request), 0).xyzw,
                texelFetch(s_shadowContext, ivec2(2, request), 0).xyzw,
                texelFetch(s_shadowContext, ivec2(3, request), 0).xyzw);
}

vec4 hbgLoadShadowAtlasRect(int request)
{
    return texelFetch(s_shadowContext, ivec2(4, request), 0);
}

vec4 hbgLoadShadowRequestParams(int request)
{
    return texelFetch(s_shadowContext, ivec2(5, request), 0);
}

vec4 hbgLoadShadowRequestInfo(int request)
{
    return texelFetch(s_shadowContext, ivec2(6, request), 0);
}

vec4 hbgLoadShadowFilterParams(int request)
{
    return texelFetch(s_shadowContext, ivec2(7, request), 0);
}

vec4 hbgLoadShadowFade(int request)
{
    return texelFetch(s_shadowContext, ivec2(8, request), 0);
}

vec4 hbgLoadShadowPcssParams(int request)
{
    return texelFetch(s_shadowContext, ivec2(9, request), 0);
}

void hbgTentAreas3(float offset, out vec4 area, out vec4 uncut)
{
    const float offset01SquaredHalved = (offset + 0.5) * (offset + 0.5) * 0.5;
    uncut.x = area.x = offset01SquaredHalved - offset;
    uncut.w = area.w = offset01SquaredHalved;

    uncut.y = 1.5 - offset - 0.5;
    const float clampedOffsetLeft = min(offset, 0.0);
    area.y = uncut.y - clampedOffsetLeft * clampedOffsetLeft;

    uncut.z = 1.5 + offset - 0.5;
    const float clampedOffsetRight = max(offset, 0.0);
    area.z = uncut.z - clampedOffsetRight * clampedOffsetRight;
}

void hbgTentWeights3(float offset, out vec4 weights)
{
    vec4 area;
    vec4 uncut;
    hbgTentAreas3(offset, area, uncut);
    weights = area * 0.44444;
}

void hbgTentWeights5(float offset, out vec3 weightsA, out vec3 weightsB)
{
    vec4 area;
    vec4 uncut;
    hbgTentAreas3(offset, area, uncut);
    weightsA.x = 0.16 * area.x;
    weightsA.y = 0.16 * uncut.y;
    weightsA.z = 0.16 * (area.y + 1.0);
    weightsB.x = 0.16 * (area.z + 1.0);
    weightsB.y = 0.16 * uncut.z;
    weightsB.z = 0.16 * area.w;
}

void hbgTentWeights7(float offset, out vec4 weightsA, out vec4 weightsB)
{
    vec4 area;
    vec4 uncut;
    hbgTentAreas3(offset, area, uncut);
    weightsA.x = 0.081632 * area.x;
    weightsA.y = 0.081632 * uncut.y;
    weightsA.z = 0.081632 * (uncut.y + 1.0);
    weightsA.w = 0.081632 * (area.y + 2.0);
    weightsB.x = 0.081632 * (area.z + 2.0);
    weightsB.y = 0.081632 * (uncut.z + 1.0);
    weightsB.z = 0.081632 * uncut.z;
    weightsB.w = 0.081632 * area.w;
}

void hbgTentSamples3(vec2 coord, vec2 texel, out float weights[4], out vec2 uvs[4])
{
    const vec2 tentCenterInTexelSpace = coord / texel;
    const vec2 centerOfFetchesInTexelSpace = floor(tentCenterInTexelSpace + 0.5);
    const vec2 offsetFromTentCenter = tentCenterInTexelSpace - centerOfFetchesInTexelSpace;

    vec4 weightsU;
    vec4 weightsV;
    hbgTentWeights3(offsetFromTentCenter.x, weightsU);
    hbgTentWeights3(offsetFromTentCenter.y, weightsV);

    const vec2 fetchesWeightsU = weightsU.xz + weightsU.yw;
    const vec2 fetchesWeightsV = weightsV.xz + weightsV.yw;
    vec2 fetchesOffsetsU = weightsU.yw / fetchesWeightsU + vec2(-1.5, 0.5);
    vec2 fetchesOffsetsV = weightsV.yw / fetchesWeightsV + vec2(-1.5, 0.5);
    fetchesOffsetsU *= texel.xx;
    fetchesOffsetsV *= texel.yy;

    const vec2 bilinearFetchOrigin = centerOfFetchesInTexelSpace * texel;
    uvs[0] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.x);
    uvs[1] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.x);
    uvs[2] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.y);
    uvs[3] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.y);

    weights[0] = fetchesWeightsU.x * fetchesWeightsV.x;
    weights[1] = fetchesWeightsU.y * fetchesWeightsV.x;
    weights[2] = fetchesWeightsU.x * fetchesWeightsV.y;
    weights[3] = fetchesWeightsU.y * fetchesWeightsV.y;
}

void hbgTentSamples5(vec2 coord, vec2 texel, out float weights[9], out vec2 uvs[9])
{
    const vec2 tentCenterInTexelSpace = coord / texel;
    const vec2 centerOfFetchesInTexelSpace = floor(tentCenterInTexelSpace + 0.5);
    const vec2 offsetFromTentCenter = tentCenterInTexelSpace - centerOfFetchesInTexelSpace;

    vec3 weightsU_A;
    vec3 weightsU_B;
    vec3 weightsV_A;
    vec3 weightsV_B;
    hbgTentWeights5(offsetFromTentCenter.x, weightsU_A, weightsU_B);
    hbgTentWeights5(offsetFromTentCenter.y, weightsV_A, weightsV_B);

    const vec3 fetchesWeightsU = vec3(weightsU_A.xz, weightsU_B.y) + vec3(weightsU_A.y, weightsU_B.xz);
    const vec3 fetchesWeightsV = vec3(weightsV_A.xz, weightsV_B.y) + vec3(weightsV_A.y, weightsV_B.xz);
    vec3 fetchesOffsetsU = vec3(weightsU_A.y, weightsU_B.xz) / fetchesWeightsU + vec3(-2.5, -0.5, 1.5);
    vec3 fetchesOffsetsV = vec3(weightsV_A.y, weightsV_B.xz) / fetchesWeightsV + vec3(-2.5, -0.5, 1.5);
    fetchesOffsetsU *= texel.xxx;
    fetchesOffsetsV *= texel.yyy;

    const vec2 bilinearFetchOrigin = centerOfFetchesInTexelSpace * texel;
    uvs[0] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.x);
    uvs[1] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.x);
    uvs[2] = bilinearFetchOrigin + vec2(fetchesOffsetsU.z, fetchesOffsetsV.x);
    uvs[3] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.y);
    uvs[4] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.y);
    uvs[5] = bilinearFetchOrigin + vec2(fetchesOffsetsU.z, fetchesOffsetsV.y);
    uvs[6] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.z);
    uvs[7] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.z);
    uvs[8] = bilinearFetchOrigin + vec2(fetchesOffsetsU.z, fetchesOffsetsV.z);

    weights[0] = fetchesWeightsU.x * fetchesWeightsV.x;
    weights[1] = fetchesWeightsU.y * fetchesWeightsV.x;
    weights[2] = fetchesWeightsU.z * fetchesWeightsV.x;
    weights[3] = fetchesWeightsU.x * fetchesWeightsV.y;
    weights[4] = fetchesWeightsU.y * fetchesWeightsV.y;
    weights[5] = fetchesWeightsU.z * fetchesWeightsV.y;
    weights[6] = fetchesWeightsU.x * fetchesWeightsV.z;
    weights[7] = fetchesWeightsU.y * fetchesWeightsV.z;
    weights[8] = fetchesWeightsU.z * fetchesWeightsV.z;
}

void hbgTentSamples7(vec2 coord, vec2 texel, out float weights[16], out vec2 uvs[16])
{
    const vec2 tentCenterInTexelSpace = coord / texel;
    const vec2 centerOfFetchesInTexelSpace = floor(tentCenterInTexelSpace + 0.5);
    const vec2 offsetFromTentCenter = tentCenterInTexelSpace - centerOfFetchesInTexelSpace;

    vec4 weightsU_A;
    vec4 weightsU_B;
    vec4 weightsV_A;
    vec4 weightsV_B;
    hbgTentWeights7(offsetFromTentCenter.x, weightsU_A, weightsU_B);
    hbgTentWeights7(offsetFromTentCenter.y, weightsV_A, weightsV_B);

    const vec4 fetchesWeightsU = vec4(weightsU_A.xz, weightsU_B.xz) + vec4(weightsU_A.yw, weightsU_B.yw);
    const vec4 fetchesWeightsV = vec4(weightsV_A.xz, weightsV_B.xz) + vec4(weightsV_A.yw, weightsV_B.yw);
    vec4 fetchesOffsetsU = vec4(weightsU_A.yw, weightsU_B.yw) / fetchesWeightsU + vec4(-3.5, -1.5, 0.5, 2.5);
    vec4 fetchesOffsetsV = vec4(weightsV_A.yw, weightsV_B.yw) / fetchesWeightsV + vec4(-3.5, -1.5, 0.5, 2.5);
    fetchesOffsetsU *= texel.xxxx;
    fetchesOffsetsV *= texel.yyyy;

    const vec2 bilinearFetchOrigin = centerOfFetchesInTexelSpace * texel;
    uvs[0] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.x);
    uvs[1] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.x);
    uvs[2] = bilinearFetchOrigin + vec2(fetchesOffsetsU.z, fetchesOffsetsV.x);
    uvs[3] = bilinearFetchOrigin + vec2(fetchesOffsetsU.w, fetchesOffsetsV.x);
    uvs[4] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.y);
    uvs[5] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.y);
    uvs[6] = bilinearFetchOrigin + vec2(fetchesOffsetsU.z, fetchesOffsetsV.y);
    uvs[7] = bilinearFetchOrigin + vec2(fetchesOffsetsU.w, fetchesOffsetsV.y);
    uvs[8] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.z);
    uvs[9] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.z);
    uvs[10] = bilinearFetchOrigin + vec2(fetchesOffsetsU.z, fetchesOffsetsV.z);
    uvs[11] = bilinearFetchOrigin + vec2(fetchesOffsetsU.w, fetchesOffsetsV.z);
    uvs[12] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.w);
    uvs[13] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.w);
    uvs[14] = bilinearFetchOrigin + vec2(fetchesOffsetsU.z, fetchesOffsetsV.w);
    uvs[15] = bilinearFetchOrigin + vec2(fetchesOffsetsU.w, fetchesOffsetsV.w);

    weights[0] = fetchesWeightsU.x * fetchesWeightsV.x;
    weights[1] = fetchesWeightsU.y * fetchesWeightsV.x;
    weights[2] = fetchesWeightsU.z * fetchesWeightsV.x;
    weights[3] = fetchesWeightsU.w * fetchesWeightsV.x;
    weights[4] = fetchesWeightsU.x * fetchesWeightsV.y;
    weights[5] = fetchesWeightsU.y * fetchesWeightsV.y;
    weights[6] = fetchesWeightsU.z * fetchesWeightsV.y;
    weights[7] = fetchesWeightsU.w * fetchesWeightsV.y;
    weights[8] = fetchesWeightsU.x * fetchesWeightsV.z;
    weights[9] = fetchesWeightsU.y * fetchesWeightsV.z;
    weights[10] = fetchesWeightsU.z * fetchesWeightsV.z;
    weights[11] = fetchesWeightsU.w * fetchesWeightsV.z;
    weights[12] = fetchesWeightsU.x * fetchesWeightsV.w;
    weights[13] = fetchesWeightsU.y * fetchesWeightsV.w;
    weights[14] = fetchesWeightsU.z * fetchesWeightsV.w;
    weights[15] = fetchesWeightsU.w * fetchesWeightsV.w;
}

float hbgSampleTent3(vec2 atlasUV, float depth, vec2 texel, vec2 uvMin, vec2 uvMax, bool punctual)
{
    float weights[4];
    vec2 uvs[4];
    hbgTentSamples3(atlasUV, texel, weights, uvs);
    float visibility = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        const vec2 uv = clamp(uvs[i], uvMin, uvMax);
        visibility += weights[i] * (punctual ? shadow2D(s_shadowMapPunctual, vec3(uv, depth))
                                             : shadow2D(s_shadowMap, vec3(uv, depth)));
    }
    return visibility;
}

float hbgSampleTent5(vec2 atlasUV, float depth, vec2 texel, vec2 uvMin, vec2 uvMax, bool punctual)
{
    float weights[9];
    vec2 uvs[9];
    hbgTentSamples5(atlasUV, texel, weights, uvs);
    float visibility = 0.0;
    for (int i = 0; i < 9; ++i)
    {
        const vec2 uv = clamp(uvs[i], uvMin, uvMax);
        visibility += weights[i] * (punctual ? shadow2D(s_shadowMapPunctual, vec3(uv, depth))
                                             : shadow2D(s_shadowMap, vec3(uv, depth)));
    }
    return visibility;
}

float hbgSampleTent7(vec2 atlasUV, float depth, vec2 texel, vec2 uvMin, vec2 uvMax, bool punctual)
{
    float weights[16];
    vec2 uvs[16];
    hbgTentSamples7(atlasUV, texel, weights, uvs);
    float visibility = 0.0;
    for (int i = 0; i < 16; ++i)
    {
        const vec2 uv = clamp(uvs[i], uvMin, uvMax);
        visibility += weights[i] * (punctual ? shadow2D(s_shadowMapPunctual, vec3(uv, depth))
                                             : shadow2D(s_shadowMap, vec3(uv, depth)));
    }
    return visibility;
}

#ifdef HBG_SHADOW_POINT_SAMPLER
float hbgPcssNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

vec2 hbgPcssDiskSample(int index, int count, float radius, float jitter)
{
    const float sampleIndex = float(index) + 0.5;
    const float sampleRadius = radius * sqrt(sampleIndex / float(count));
    const float angle = sampleIndex * 2.39996323 + jitter;
    return vec2(cos(angle), sin(angle)) * sampleRadius;
}

float hbgPcssRawDepth(vec2 uv, bool punctual)
{
    if (punctual)
    {
        return texture2DLod(s_shadowMapPointPunctual, uv, 0.0).x;
    }
    return texture2DLod(s_shadowMapPoint, uv, 0.0).x;
}

float hbgSamplePcss(vec2 atlasUV, float depth, vec2 texel, vec2 uvMin, vec2 uvMax, bool punctual,
                    float softness, float minFilterSize, int blockerSampleCount, int filterSampleCount,
                    float worldTexelSize, float nearPlane, float farPlane)
{
    const int blockers = clamp(blockerSampleCount, 1, 64);
    const int filters = clamp(filterSampleCount, 1, 64);
    const float jitter = hbgPcssNoise(atlasUV * 4096.0 +
                                      u_global_taaFrameInfo.z * 17.0 * u_global_taaFrameInfo.w) * 6.2831853;
    const float blockerRadius = clamp(softness * 32.0, 1.0, 16.0);
    float blockerSum = 0.0;
    float blockerCount = 0.0;
    for (int blockerIndex = 0; blockerIndex < blockers; ++blockerIndex)
    {
        const vec2 offset = hbgPcssDiskSample(blockerIndex, blockers, blockerRadius, jitter) * texel;
        const vec2 uv = atlasUV + offset;
        if (all(greaterThanEqual(uv, uvMin)) && all(lessThanEqual(uv, uvMax)))
        {
            const float blockerDepth = hbgPcssRawDepth(uv, punctual);
            if (blockerDepth < depth)
            {
                blockerSum += blockerDepth;
                blockerCount += 1.0;
            }
        }
    }
    if (blockerCount < 1.0)
    {
        return 1.0;
    }

    const float averageBlockerDepth = blockerSum / blockerCount;
    float filterRadiusTexels = 1.0;
    const float minimumRadius = max(1.0, minFilterSize * 64.0);
    if (punctual)
    {
        const float nearZ = max(nearPlane, 1.0e-4);
        const float farZ = max(farPlane, nearZ + 1.0e-3);
        const float zScale = (farZ - nearZ) / (nearZ * farZ);
        const float zBias = 1.0 / farZ;
        const float receiverDistance = 1.0 / max(zScale * (1.0 - depth) + zBias, 1.0e-6);
        const float blockerDistance = 1.0 / max(zScale * (1.0 - averageBlockerDepth) + zBias, 1.0e-6);
        const float penumbra = abs(receiverDistance - blockerDistance) / max(blockerDistance, 1.0e-4);
        filterRadiusTexels = max(minimumRadius, penumbra * softness * 128.0);
    }
    else
    {
        const float depthRange = max(farPlane - nearPlane, 1.0e-3);
        const float receiverDistance = depth * depthRange;
        const float blockerDistance = averageBlockerDepth * depthRange;
        filterRadiusTexels = max(minimumRadius, abs(receiverDistance - blockerDistance) * softness /
                                                   max(worldTexelSize, 1.0e-5));
    }
    filterRadiusTexels = clamp(filterRadiusTexels, 1.0, 24.0);

    float visibility = 0.0;
    float validSamples = 0.0;
    for (int filterIndex = 0; filterIndex < filters; ++filterIndex)
    {
        const vec2 offset = hbgPcssDiskSample(filterIndex, filters, filterRadiusTexels, jitter) * texel;
        const vec2 uv = atlasUV + offset;
        if (all(greaterThanEqual(uv, uvMin)) && all(lessThanEqual(uv, uvMax)))
        {
            if (punctual)
            {
                visibility += shadow2D(s_shadowMapPunctual, vec3(uv, depth));
            }
            else
            {
                visibility += shadow2D(s_shadowMap, vec3(uv, depth));
            }
            validSamples += 1.0;
        }
    }
    return validSamples > 0.0 ? visibility / validSamples : 1.0;
}
#endif

float hbgSampleDirectionalRequest(int request, vec3 worldPosition, vec3 normalWS)
{
    const vec4 requestParams = hbgLoadShadowRequestParams(request);
    const vec4 requestInfo = hbgLoadShadowRequestInfo(request);
    const vec4 filterParams = hbgLoadShadowFilterParams(request);
    if (requestInfo.w < 0.5)
    {
        return 1.0;
    }

    const vec4 atlasRect = hbgLoadShadowAtlasRect(request);
    const mat4 shadowMatrix = transpose(hbgLoadShadowMatrix(request));
    const vec4 rawShadowCoord = mul(shadowMatrix, vec4(worldPosition, 1.0));
    if (rawShadowCoord.w <= 0.0)
    {
        return 1.0;
    }

    const vec3 biasedPosition =
        worldPosition + normalWS * requestParams.y * filterParams.z;
    const vec4 shadowCoord = mul(shadowMatrix, vec4(biasedPosition, 1.0));
    if (shadowCoord.w <= 0.0)
    {
        return 1.0;
    }

    const float inverseW = 1.0 / shadowCoord.w;
    const vec2 tileUV = shadowCoord.xy * inverseW;
    const float depth = shadowCoord.z * inverseW - requestParams.x;
    if (depth < 0.0 || depth > 1.0)
    {
        return 1.0;
    }

    const vec2 atlasUV = tileUV * atlasRect.xy + atlasRect.zw;
    if (any(lessThan(atlasUV, atlasRect.zw)) ||
        any(greaterThan(atlasUV, atlasRect.zw + atlasRect.xy)))
    {
        return 1.0;
    }

    const vec2 texel = requestParams.zw;
    const vec2 uvMin = atlasRect.zw + texel * 0.5;
    const vec2 uvMax = atlasRect.zw + atlasRect.xy - texel * 0.5;
    const int filterMode = int(requestInfo.z + 0.5);
    if (filterMode == 4)
    {
#ifdef HBG_SHADOW_POINT_SAMPLER
        const vec4 pcssParams = hbgLoadShadowPcssParams(request);
        return hbgSamplePcss(atlasUV, depth, texel, uvMin, uvMax, false, filterParams.x, pcssParams.z,
                             int(pcssParams.x + 0.5), int(pcssParams.y + 0.5), filterParams.z,
                             filterParams.y, pcssParams.w);
#else
        return hbgSampleTent7(atlasUV, depth, texel, uvMin, uvMax, false);
#endif
    }
    if (filterMode == 3)
    {
        return hbgSampleTent7(atlasUV, depth, texel, uvMin, uvMax, false);
    }
    if (filterMode == 2)
    {
        return hbgSampleTent5(atlasUV, depth, texel, uvMin, uvMax, false);
    }
    if (filterMode == 1)
    {
        return hbgSampleTent3(atlasUV, depth, texel, uvMin, uvMax, false);
    }
    return shadow2D(s_shadowMap, vec3(atlasUV, depth));
}

float hbgSamplePunctualRequest(int request, vec3 worldPosition, vec3 normalWS)
{
    const vec4 requestParams = hbgLoadShadowRequestParams(request);
    const vec4 requestInfo = hbgLoadShadowRequestInfo(request);
    const vec4 filterParams = hbgLoadShadowFilterParams(request);
    if (requestInfo.w < 0.5)
    {
        return 1.0;
    }

    const vec4 atlasRect = hbgLoadShadowAtlasRect(request);
    const mat4 shadowMatrix = transpose(hbgLoadShadowMatrix(request));
    const vec4 rawShadowCoord = mul(shadowMatrix, vec4(worldPosition, 1.0));
    if (rawShadowCoord.w <= 0.0)
    {
        return 1.0;
    }

    const bool punctual = true;
    const float distanceScale = max(rawShadowCoord.w, 1.0e-4);
    const vec3 biasedPosition =
        worldPosition + normalWS * requestParams.y * filterParams.z * distanceScale;
    const vec4 shadowCoord = mul(shadowMatrix, vec4(biasedPosition, 1.0));
    if (shadowCoord.w <= 0.0)
    {
        return 1.0;
    }

    const float inverseW = 1.0 / shadowCoord.w;
    const vec2 tileUV = shadowCoord.xy * inverseW;
    const float depth = shadowCoord.z * inverseW - requestParams.x;
    if (depth < 0.0 || depth > 1.0)
    {
        return 1.0;
    }

    const vec2 atlasUV = tileUV * atlasRect.xy + atlasRect.zw;
    if (any(lessThan(atlasUV, atlasRect.zw)) ||
        any(greaterThan(atlasUV, atlasRect.zw + atlasRect.xy)))
    {
        return 1.0;
    }

    const vec2 texel = requestParams.zw;
    const vec2 uvMin = atlasRect.zw + texel * 0.5;
    const vec2 uvMax = atlasRect.zw + atlasRect.xy - texel * 0.5;
    const int filterMode = int(requestInfo.z + 0.5);
    if (filterMode == 4)
    {
#ifdef HBG_SHADOW_POINT_SAMPLER
        const vec4 pcssParams = hbgLoadShadowPcssParams(request);
        return hbgSamplePcss(atlasUV, depth, texel, uvMin, uvMax, punctual, filterParams.x, pcssParams.z,
                             int(pcssParams.x + 0.5), int(pcssParams.y + 0.5), filterParams.z,
                             filterParams.y, pcssParams.w);
#else
        return hbgSampleTent5(atlasUV, depth, texel, uvMin, uvMax, punctual);
#endif
    }
    if (filterMode == 3)
    {
        return hbgSampleTent7(atlasUV, depth, texel, uvMin, uvMax, punctual);
    }
    if (filterMode == 2)
    {
        return hbgSampleTent5(atlasUV, depth, texel, uvMin, uvMax, punctual);
    }
    if (filterMode == 1)
    {
        return hbgSampleTent3(atlasUV, depth, texel, uvMin, uvMax, punctual);
    }
    return shadow2D(s_shadowMapPunctual, vec3(atlasUV, depth));
}

int hbgShadowPointFace(vec3 direction)
{
    const vec3 magnitude = abs(direction);
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
    {
        return direction.x >= 0.0 ? 0 : 1;
    }
    if (magnitude.y >= magnitude.z)
    {
        return direction.y >= 0.0 ? 2 : 3;
    }
    return direction.z >= 0.0 ? 4 : 5;
}

float hbgSamplePunctualShadow(int requestBase, int lightType, vec3 lightPosition, vec3 lightDirection,
                              vec3 worldPosition, vec3 normalWS)
{
    if (requestBase < 0)
    {
        return 1.0;
    }
    int request = requestBase;
    if (lightType == 1)
    {
        request += hbgShadowPointFace(worldPosition - lightPosition);
    }
    else if (lightType != 2)
    {
        return 1.0;
    }
    return hbgSamplePunctualRequest(request, worldPosition, normalWS);
}

float hbgSampleDirectionalShadow(vec3 worldPosition, vec3 normalWS)
{
    if (u_pass_shadowParams.z < 0.5)
    {
        return 1.0;
    }

    const int cascadeCount = int(u_pass_shadowParams.w + 0.5);
    if (cascadeCount <= 0)
    {
        return 1.0;
    }

    const float viewDepth = -mul(u_global_view, vec4(worldPosition, 1.0)).z;
    const vec4 splits = u_pass_shadowCascadeSplits;
    int cascade = 0;
    if (viewDepth > splits.x)
    {
        cascade = 1;
    }
    if (viewDepth > splits.y)
    {
        cascade = 2;
    }
    if (viewDepth > splits.z)
    {
        cascade = 3;
    }
    const float lastSplit = cascadeCount == 1 ? splits.x : (cascadeCount == 2 ? splits.y : (cascadeCount == 3 ? splits.z : splits.w));
    if (cascade >= cascadeCount || viewDepth > lastSplit)
    {
        return 1.0;
    }

    float visibility = hbgSampleDirectionalRequest(cascade, worldPosition, normalWS);
    if (cascade == cascadeCount - 1)
    {
        const vec4 fade = hbgLoadShadowFade(cascade);
        const vec3 cameraDelta = worldPosition - u_global_cameraPos.xyz;
        const float alpha = hbgSaturate(dot(cameraDelta, cameraDelta) * fade.x + fade.y);
        visibility = mix(1.0, visibility, 1.0 - alpha);
    }
    return visibility;
}

float hbgSampleDirectionalRequestSingle(int request, vec3 worldPosition, vec3 normalWS)
{
    const vec4 requestParams = hbgLoadShadowRequestParams(request);
    const vec4 requestInfo = hbgLoadShadowRequestInfo(request);
    const vec4 filterParams = hbgLoadShadowFilterParams(request);
    if (requestInfo.w < 0.5)
    {
        return 1.0;
    }

    const vec4 atlasRect = hbgLoadShadowAtlasRect(request);
    const vec3 biasedPosition =
        worldPosition + normalWS * requestParams.y * filterParams.z;
    const vec4 shadowCoord = mul(transpose(hbgLoadShadowMatrix(request)), vec4(biasedPosition, 1.0));
    if (shadowCoord.w <= 0.0)
    {
        return 1.0;
    }

    const float inverseW = 1.0 / shadowCoord.w;
    const vec2 tileUV = shadowCoord.xy * inverseW;
    const float depth = shadowCoord.z * inverseW - requestParams.x;
    if (depth < 0.0 || depth > 1.0)
    {
        return 1.0;
    }

    const vec2 atlasUV = tileUV * atlasRect.xy + atlasRect.zw;
    if (any(lessThan(atlasUV, atlasRect.zw)) ||
        any(greaterThan(atlasUV, atlasRect.zw + atlasRect.xy)))
    {
        return 1.0;
    }
    return shadow2D(s_shadowMap, vec3(atlasUV, depth));
}

float hbgSampleDirectionalShadowVolumetric(vec3 worldPosition, vec3 normalWS)
{
    if (u_pass_shadowParams.z < 0.5)
    {
        return 1.0;
    }

    const int cascadeCount = int(u_pass_shadowParams.w + 0.5);
    if (cascadeCount <= 0)
    {
        return 1.0;
    }

    const float viewDepth = -mul(u_global_view, vec4(worldPosition, 1.0)).z;
    const vec4 splits = u_pass_shadowCascadeSplits;
    int cascade = 0;
    if (viewDepth > splits.x)
    {
        cascade = 1;
    }
    if (viewDepth > splits.y)
    {
        cascade = 2;
    }
    if (viewDepth > splits.z)
    {
        cascade = 3;
    }
    const float lastSplit = cascadeCount == 1 ? splits.x : (cascadeCount == 2 ? splits.y : (cascadeCount == 3 ? splits.z : splits.w));
    if (cascade >= cascadeCount || viewDepth > lastSplit)
    {
        return 1.0;
    }
    return hbgSampleDirectionalRequestSingle(cascade, worldPosition, normalWS);
}

#endif // HBG_SHADOWS_SC
