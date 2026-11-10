#ifndef HBG_SHADOWS_SC
#define HBG_SHADOWS_SC

#include "common/common.sc"

SAMPLER2DSHADOW(s_shadowMap, 6);
SAMPLER2D(s_shadowContext, 8);
SAMPLER2DSHADOW(s_shadowMapPunctual, 9);

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

float hbgSampleDirectionalRequest(int request, vec3 worldPosition, vec3 normalWS)
{
    const vec4 requestParams = hbgLoadShadowRequestParams(request);
    const vec4 requestInfo = hbgLoadShadowRequestInfo(request);
    if (requestInfo.w < 0.5)
    {
        return 1.0;
    }

    const vec4 atlasRect = hbgLoadShadowAtlasRect(request);
    const vec3 biasedPosition = worldPosition + normalWS * requestParams.y;
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

    const vec2 texel = requestParams.zw;
    const vec2 uvMin = atlasRect.zw + texel * 0.5;
    const vec2 uvMax = atlasRect.zw + atlasRect.xy - texel * 0.5;
    float visibility = 0.0;
    float sampleCount = 0.0;
    const float filterMode = u_pass_shadowParams.x;
    if (filterMode > 1.5)
    {
        for (int y = -2; y <= 2; ++y)
        {
            for (int x = -2; x <= 2; ++x)
            {
                const vec2 uv = clamp(atlasUV + vec2(float(x), float(y)) * texel, uvMin, uvMax);
                visibility += shadow2D(s_shadowMap, vec3(uv, depth));
                sampleCount += 1.0;
            }
        }
    }
    else if (filterMode > 0.5)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                const vec2 uv = clamp(atlasUV + vec2(float(x), float(y)) * texel, uvMin, uvMax);
                visibility += shadow2D(s_shadowMap, vec3(uv, depth));
                sampleCount += 1.0;
            }
        }
    }
    else
    {
        visibility = shadow2D(s_shadowMap, vec3(atlasUV, depth));
        sampleCount = 1.0;
    }
    return visibility / sampleCount;
}

float hbgSamplePunctualRequest(int request, vec3 worldPosition, vec3 normalWS)
{
    const vec4 requestParams = hbgLoadShadowRequestParams(request);
    const vec4 requestInfo = hbgLoadShadowRequestInfo(request);
    if (requestInfo.w < 0.5)
    {
        return 1.0;
    }

    const vec4 atlasRect = hbgLoadShadowAtlasRect(request);
    const vec3 biasedPosition = worldPosition + normalWS * requestParams.y;
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

    const vec2 texel = requestParams.zw;
    const vec2 uvMin = atlasRect.zw + texel * 0.5;
    const vec2 uvMax = atlasRect.zw + atlasRect.xy - texel * 0.5;
    float visibility = 0.0;
    float sampleCount = 0.0;
    const float filterMode = u_pass_shadowParams.x;
    if (filterMode > 1.5)
    {
        for (int y = -2; y <= 2; ++y)
        {
            for (int x = -2; x <= 2; ++x)
            {
                const vec2 uv = clamp(atlasUV + vec2(float(x), float(y)) * texel, uvMin, uvMax);
                visibility += shadow2D(s_shadowMapPunctual, vec3(uv, depth));
                sampleCount += 1.0;
            }
        }
    }
    else if (filterMode > 0.5)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                const vec2 uv = clamp(atlasUV + vec2(float(x), float(y)) * texel, uvMin, uvMax);
                visibility += shadow2D(s_shadowMapPunctual, vec3(uv, depth));
                sampleCount += 1.0;
            }
        }
    }
    else
    {
        visibility = shadow2D(s_shadowMapPunctual, vec3(atlasUV, depth));
        sampleCount = 1.0;
    }
    return visibility / sampleCount;
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
    if (viewDepth > splits.w)
    {
        return 1.0;
    }
    return hbgSampleDirectionalRequest(cascade, worldPosition, normalWS);
}

#endif // HBG_SHADOWS_SC
