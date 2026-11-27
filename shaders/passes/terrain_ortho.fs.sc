$input v_texcoord0

#include "common/common.sc"

SAMPLER2DARRAY(s_orthoParent, 0);
SAMPLER2D(s_orthoSource, 1);
SAMPLER2DARRAY(s_orthoNoise, 2);

uniform vec4 u_orthoTransform; // origin x/z, texture world size, world size
uniform vec4 u_orthoCoarse;    // parent origin u/v, parent texel step, parent layer
uniform vec4 u_orthoInfo;      // level, strength * scale, noise amplitude, tile width
uniform vec4 u_orthoFlags;     // has parent, has source, procedural, hsv
uniform vec4 u_orthoColor;     // noise color
uniform vec4 u_orthoRoot;      // root color
uniform vec4 u_orthoNoise;     // noise u orientation, v orientation, layer, unused

vec3 hbgRgbToHsv(vec3 rgb)
{
    vec3 hsv = vec3(0.0, 0.0, 0.0);
    float minVal = min(rgb.x, min(rgb.y, rgb.z));
    float maxVal = max(rgb.x, max(rgb.y, rgb.z));
    float delta = maxVal - minVal;
    hsv.z = maxVal;
    if (delta != 0.0)
    {
        hsv.y = delta / maxVal;
        vec3 delRgb = (((vec3(maxVal, maxVal, maxVal) - rgb) / 6.0) + delta / 2.0) / delta;
        if (rgb.x == maxVal)
        {
            hsv.x = delRgb.z - delRgb.y;
        }
        else if (rgb.y == maxVal)
        {
            hsv.x = (1.0 / 3.0) + delRgb.x - delRgb.z;
        }
        else if (rgb.z == maxVal)
        {
            hsv.x = (2.0 / 3.0) + delRgb.y - delRgb.x;
        }
        if (hsv.x < 0.0)
        {
            hsv.x += 1.0;
        }
        if (hsv.x > 1.0)
        {
            hsv.x -= 1.0;
        }
    }
    return hsv;
}

vec3 hbgHsvToRgb(vec3 hsv)
{
    vec3 rgb = hsv.zzz;
    if (hsv.y != 0.0)
    {
        float varH = hsv.x * 6.0;
        float varI = floor(varH);
        float var1 = hsv.z * (1.0 - hsv.y);
        float var2 = hsv.z * (1.0 - hsv.y * (varH - varI));
        float var3 = hsv.z * (1.0 - hsv.y * (1.0 - (varH - varI)));
        if (varI == 0.0)
        {
            rgb = vec3(hsv.z, var3, var1);
        }
        else if (varI == 1.0)
        {
            rgb = vec3(var2, hsv.z, var1);
        }
        else if (varI == 2.0)
        {
            rgb = vec3(var1, hsv.z, var3);
        }
        else if (varI == 3.0)
        {
            rgb = vec3(var1, var2, hsv.z);
        }
        else if (varI == 4.0)
        {
            rgb = vec3(var3, var1, hsv.z);
        }
        else
        {
            rgb = vec3(hsv.z, var1, var2);
        }
    }
    return rgb;
}

vec4 hbgOrthoMask(float index)
{
    if (index < 0.5)
    {
        return vec4(1.0, 3.0, 3.0, 9.0);
    }
    if (index < 1.5)
    {
        return vec4(3.0, 1.0, 9.0, 3.0);
    }
    if (index < 2.5)
    {
        return vec4(3.0, 9.0, 1.0, 3.0);
    }
    return vec4(9.0, 3.0, 3.0, 1.0);
}

void main()
{
    const vec2 worldXz = u_orthoTransform.xy + v_texcoord0 * u_orthoTransform.z;
    const float worldSize = max(u_orthoTransform.w, 1.0e-6);
    const vec2 sourceUv = (worldXz + vec2(0.5 * worldSize, 0.5 * worldSize)) / worldSize;
    const float tileWidth = max(u_orthoInfo.w, 1.0);

    vec4 result = u_orthoRoot;
    if (u_orthoFlags.y > 0.5)
    {
        result = texture2D(s_orthoSource, clamp(sourceUv, vec2(0.0, 0.0), vec2(1.0, 1.0)));
    }

    const vec2 uv = floor(v_texcoord0 * tileWidth);
    if (u_orthoFlags.x > 0.5)
    {
        const vec4 mask = hbgOrthoMask(mod(uv.x, 2.0) + 2.0 * mod(uv.y, 2.0));
        const vec3 offset = vec3(
            floor((uv + vec2(1.0, 1.0)) * 0.5) * u_orthoCoarse.z + u_orthoCoarse.xy,
            u_orthoCoarse.w);
        const vec4 c0 = texture2DArrayLod(s_orthoParent, offset, 0.0);
        const vec4 c1 = texture2DArrayLod(s_orthoParent,
                                          offset + vec3(u_orthoCoarse.z, 0.0, 0.0), 0.0);
        const vec4 c2 = texture2DArrayLod(s_orthoParent,
                                          offset + vec3(0.0, u_orthoCoarse.z, 0.0), 0.0);
        const vec4 c3 = texture2DArrayLod(s_orthoParent,
                                          offset + vec3(u_orthoCoarse.z, u_orthoCoarse.z, 0.0), 0.0);
        result = floor((mask.x * c0 + mask.y * c1 + mask.z * c2 + mask.w * c3) * 255.0 / 16.0) / 255.0;
    }

    const vec2 noiseNuv = (uv + vec2(0.5, 0.5)) / tileWidth;
    const vec4 noiseUv = vec4(noiseNuv.x, noiseNuv.y, 1.0 - noiseNuv.x, 1.0 - noiseNuv.y);
    const vec2 noiseCoord = vec2(noiseUv[int(u_orthoNoise.x + 0.5)], noiseUv[int(u_orthoNoise.y + 0.5)]);
    const vec3 noise = texture2DArrayLod(s_orthoNoise, vec3(noiseCoord, u_orthoNoise.z), 0.0).rgb;
    const vec3 noiseDelta = (noise - vec3(0.5, 0.5, 0.5)) * (u_orthoInfo.y * u_orthoInfo.z);

    if (u_orthoFlags.w > 0.5)
    {
        vec3 hsv = hbgRgbToHsv(clamp(result.rgb, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0)));
        const float brightness = smoothstep(0.4, 0.8, hsv.z);
        hsv *= vec3(1.0, 1.0, 1.0) +
               (vec3(1.0, 1.0, 1.0) - vec3(brightness, brightness, brightness)) * u_orthoColor.rgb * noiseDelta;
        hsv.x = fract(hsv.x);
        hsv.y = clamp(hsv.y, 0.0, 1.0);
        hsv.z = clamp(hsv.z, 0.0, 1.0);
        result.rgb = hbgHsvToRgb(hsv);
    }
    else
    {
        result.rgb += u_orthoColor.rgb * noiseDelta;
    }

    gl_FragColor = vec4(clamp(result.rgb, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0)), 1.0);
}
