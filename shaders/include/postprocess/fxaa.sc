#ifndef HBG_POSTPROCESS_FXAA_SC
#define HBG_POSTPROCESS_FXAA_SC

#include "common/common.sc"

SAMPLER2D(s_ldrColor, 0);

vec3 hbgFxaaFetch(vec2 uv, vec2 offset)
{
    return max(texture2D(s_ldrColor, clamp(uv + offset, vec2(0.0, 0.0), vec2(1.0, 1.0))).rgb,
               vec3(0.0, 0.0, 0.0));
}

float hbgFxaaLuma(vec3 color)
{
    return hbgLuminance(hbgSaturate(color));
}

vec2 hbgFxaaDirection(vec2 uv, vec2 texel, out float strength)
{
    const vec2 diagonal = vec2(texel.x, texel.y);
    const float lumaNW = hbgFxaaLuma(hbgFxaaFetch(uv, vec2(-diagonal.x, -diagonal.y)));
    const float lumaNE = hbgFxaaLuma(hbgFxaaFetch(uv, vec2( diagonal.x, -diagonal.y)));
    const float lumaSW = hbgFxaaLuma(hbgFxaaFetch(uv, vec2(-diagonal.x,  diagonal.y)));
    const float lumaSE = hbgFxaaLuma(hbgFxaaFetch(uv, vec2( diagonal.x,  diagonal.y)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    const float lumaSum = lumaNW + lumaNE + lumaSW + lumaSE;
    const float dirReduce = max(lumaSum * (0.25 * (1.0 / 8.0)), (1.0 / 128.0));
    const float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    strength = hbgSaturate(length(dir) * 4.0);
    return clamp(dir * rcpDirMin, vec2(-8.0, -8.0), vec2(8.0, 8.0)) * texel;
}

float hbgFxaaEdgeStrength(vec2 uv, vec2 texel)
{
    float strength;
    hbgFxaaDirection(uv, texel, strength);
    return strength;
}

vec3 hbgFxaaApply(vec2 uv, vec2 texel, vec3 center)
{
    const vec2 diagonal = vec2(texel.x, texel.y);
    const vec3 cornerNW = hbgFxaaFetch(uv, vec2(-diagonal.x, -diagonal.y));
    const vec3 cornerNE = hbgFxaaFetch(uv, vec2( diagonal.x, -diagonal.y));
    const vec3 cornerSW = hbgFxaaFetch(uv, vec2(-diagonal.x,  diagonal.y));
    const vec3 cornerSE = hbgFxaaFetch(uv, vec2( diagonal.x,  diagonal.y));

    const float lumaNW = hbgFxaaLuma(cornerNW);
    const float lumaNE = hbgFxaaLuma(cornerNE);
    const float lumaSW = hbgFxaaLuma(cornerSW);
    const float lumaSE = hbgFxaaLuma(cornerSE);
    const float lumaM = hbgFxaaLuma(center);

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    const float lumaSum = lumaNW + lumaNE + lumaSW + lumaSE;
    const float dirReduce = max(lumaSum * (0.25 * (1.0 / 8.0)), (1.0 / 128.0));
    const float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0, -8.0), vec2(8.0, 8.0)) * texel;

    const vec3 rgb03 = hbgSaturate(hbgFxaaFetch(uv, dir * (-0.5)));
    const vec3 rgb13 = hbgSaturate(hbgFxaaFetch(uv, dir * (-1.0 / 6.0)));
    const vec3 rgb23 = hbgSaturate(hbgFxaaFetch(uv, dir * ( 1.0 / 6.0)));
    const vec3 rgb33 = hbgSaturate(hbgFxaaFetch(uv, dir * ( 0.5)));

    const vec3 rgbA = 0.5 * (rgb13 + rgb23);
    const vec3 rgbB = rgbA * 0.5 + 0.25 * (rgb03 + rgb33);
    const float lumaB = hbgLuminance(rgbB);

    const float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    const float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    return (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
}

#endif // HBG_POSTPROCESS_FXAA_SC
