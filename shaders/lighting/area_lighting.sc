#ifndef HBG_AREA_LIGHTING_SC
#define HBG_AREA_LIGHTING_SC

#include "common/common.sc"
#include "common/light_data.sc"

vec3 hbgLtcTransform(vec4 ltc, vec3 value)
{
    return vec3(ltc.x * value.x + ltc.y * value.z, ltc.z * value.y,
                ltc.w * value.x + value.z);
}

vec3 hbgComputeEdgeFactor(vec3 v1, vec3 v2)
{
    const float v1oV2 = dot(v1, v2);
    const vec3 v1xV2 = cross(v1, v2);
    const float sqLen = hbgSaturate(1.0 - v1oV2 * v1oV2);
    const float rcpLen = inversesqrt(max(1.0e-6, sqLen));
    const float x = abs(v1oV2);
    float y = HBG_PI * 0.5 + x * (-0.99991 + x * (0.783393 + x * (-0.649178 +
              x * (0.510589 + x * (-0.326137 + x * (0.137528 + x * -0.0270813))))));
    if (v1oV2 < 0.0)
    {
        y = rcpLen * HBG_PI - y;
    }
    return v1xV2 * y;
}

vec3 hbgSafeNormalize(vec3 value)
{
    return value * inversesqrt(max(dot(value, value), 1.0e-12));
}

vec3 hbgPolygonFormFactor(vec3 v0, vec3 v1, vec3 v2, vec3 v3)
{
    v0 = hbgSafeNormalize(v0);
    v1 = hbgSafeNormalize(v1);
    v2 = hbgSafeNormalize(v2);
    v3 = hbgSafeNormalize(v3);

    vec3 F = hbgComputeEdgeFactor(v0, v3);
    F += hbgComputeEdgeFactor(v3, v2);
    F += hbgComputeEdgeFactor(v2, v1);
    F += hbgComputeEdgeFactor(v1, v0);
    return F * 0.15915494309189535;
}

float hbgPolygonIrradiance(vec3 formFactor)
{
    const float lengthF = length(formFactor);
    return max(0.0, (lengthF * lengthF + formFactor.z) / (lengthF + 1.0));
}

// HDRP core AreaLighting.hlsl: I_diffuse_line, with the singular A.z case
// handled explicitly so GLSL never evaluates an infinite h.
float hbgDiffuseLine(vec3 C, vec3 A, float halfLength)
{
    float h1;
    float h2;
    if (abs(A.z) <= 1.0e-7)
    {
        h1 = -halfLength;
        h2 = halfLength;
    }
    else
    {
        const float h = -C.z / A.z;
        if (A.z >= 0.0)
        {
            h2 = max(halfLength, h);
            h1 = max(-halfLength, h);
        }
        else
        {
            h2 = min(halfLength, h);
            h1 = min(-halfLength, h);
        }
    }

    const float as = dot(A, A);
    if (as <= 1.0e-12)
    {
        return 0.0;
    }
    const float ar = inversesqrt(as);
    const float a = as * ar;
    const vec3 T = A * ar;

    const float tc = dot(T, C);
    const vec3 P0 = C - tc * T;
    const float ns = dot(P0, P0);
    if (ns <= 1.0e-12)
    {
        return 0.0;
    }

    const float nr = inversesqrt(ns);
    const float n = ns * nr;
    const float Nz = P0.z * nr;

    const float t2 = tc + h2 * a;
    const float t1 = tc + h1 * a;
    const float s2 = ns + t2 * t2;
    const float s1 = ns + t1 * t1;
    const float s1s2 = s1 * s2;
    if (s1s2 <= 1.0e-12)
    {
        return 0.0;
    }

    const float mr = inversesqrt(s1s2);
    const float r2 = s1 * (mr * mr);
    const float r1 = s2 * (mr * mr);
    const float i12 = (Nz * t2 - T.z * n) * r2 - (Nz * t1 - T.z * n) * r1;
    const float dt = clamp((ns + t1 * t2) * mr, -1.0, 1.0);
    const float i3 = acos(dt) * (Nz * nr);
    return HBG_INV_PI * max(0.0, i12 + i3);
}

// HDRP core AreaLighting.hlsl: ComputeLineWidthFactor.  The sparse LTC
// matrix is {{ltc.x, 0, ltc.y}, {0, ltc.z, 0}, {ltc.w, 0, 1}}.
float hbgLtcLineWidthFactor(vec4 ltc, vec3 ortho, float orthoSq)
{
    const float a = ltc.x;
    const float b = ltc.y;
    const float c = ltc.z;
    const float d = ltc.w;
    const float det = c * (a - b * d);
    const vec3 X = vec3(c * (ortho.x - d * ortho.z), ortho.y * (a - b * d),
                        c * (-b * ortho.x + a * ortho.z));
    const float denom = dot(X, X) * orthoSq;
    if (denom <= 1.0e-12)
    {
        return 0.0;
    }
    return abs(det) * inversesqrt(denom) * orthoSq;
}

float hbgLineDiffuseIrradiance(vec3 center, vec3 axis, float halfLength)
{
    if (halfLength <= 0.0 || center.z + halfLength * abs(axis.z) <= 0.0)
    {
        return 0.0;
    }
    const vec3 ortho = cross(center, axis);
    if (dot(ortho, ortho) <= 1.0e-12)
    {
        return 0.0;
    }
    return hbgDiffuseLine(center, axis, halfLength);
}

float hbgLtcLineIrradiance(vec4 ltc, vec3 center, vec3 axis, float halfLength)
{
    if (halfLength <= 0.0)
    {
        return 0.0;
    }
    const vec3 ortho = cross(center, axis);
    const float orthoSq = dot(ortho, ortho);
    if (orthoSq <= 1.0e-12 || center.z + halfLength * abs(axis.z) <= 0.0)
    {
        return 0.0;
    }

    const vec3 C = hbgLtcTransform(ltc, center);
    const vec3 A = hbgLtcTransform(ltc, axis);
    if (C.z + halfLength * abs(A.z) <= 0.0)
    {
        return 0.0;
    }
    return hbgDiffuseLine(C, A, halfLength) * hbgLtcLineWidthFactor(ltc, ortho, orthoSq);
}

vec3 hbgLtcRectFormFactor(vec4 ltc, vec3 center, vec3 right, vec3 up, float halfWidth, float halfHeight)
{
    const vec3 c = hbgLtcTransform(ltc, center);
    const vec3 a = hbgLtcTransform(ltc, right);
    const vec3 b = hbgLtcTransform(ltc, up);
    const vec3 v0 = c - halfWidth * a - halfHeight * b;
    const vec3 v1 = v0 + (2.0 * halfHeight) * b;
    const vec3 v2 = v1 + (2.0 * halfWidth) * a;
    const vec3 v3 = v2 - (2.0 * halfHeight) * b;
    return hbgPolygonFormFactor(v0, v1, v2, v3);
}

vec4 hbgSampleLtcGgx(float perceptualRoughness, float ndotV)
{
    const float theta = sqrt(max(1.0 - min(ndotV, 1.0), 0.0));
    return hbgSampleLtcBilinear(vec2(perceptualRoughness, theta));
}

vec3 hbgLtcRectCookie(vec3 v0, vec3 v1, vec3 v2, vec3 v3, vec3 formFactor, int lightIndex,
                      int cookieMode, vec2 scale, vec2 offset, float perceptualRoughness)
{
    if (cookieMode == HBG_LIGHT_COOKIE_MODE_NONE)
    {
        return vec3(1.0, 1.0, 1.0);
    }

    const vec3 origin = v0;
    const vec3 right = v3 - origin;
    const vec3 up = v1 - origin;
    vec3 normal = cross(right, up);
    const float sqArea = dot(normal, normal);
    if (sqArea < 1.0e-10)
    {
        return vec3(1.0, 1.0, 1.0);
    }
    normal *= inversesqrt(sqArea);

    const float denominator = dot(formFactor, normal);
    if (abs(denominator) < 1.0e-6)
    {
        return vec3(1.0, 1.0, 1.0);
    }

    const float hitDistance = dot(origin, normal) / denominator;
    vec3 hitPosition = hitDistance * normal - origin;

    const float recSqLengthRight = 1.0 / max(dot(right, right), 1.0e-8);
    const float upRightMixing = dot(up, right);
    const vec3 ortho = up - upRightMixing * right * recSqLengthRight;
    const float v = dot(hitPosition, ortho) / max(dot(ortho, ortho), 1.0e-8);
    const float u = (dot(hitPosition, right) - upRightMixing * v) * recSqLengthRight;
    const vec2 hitUV = vec2(1.0 - u, v);

    const vec4 atlasTransform = hbgLightCookieAtlasTransform(lightIndex);
    const float cookieWidth = max(atlasTransform.x * u_pass_cookieAtlasParams.x, 1.0);
    const float cookieMipCount = max(log2(cookieWidth), 0.0);
    float mipLevel =
        0.5 * log2(1.0e-8 + HBG_PI * hitDistance * hitDistance * rsqrt(sqArea)) + cookieMipCount;
    const float mipTrimming = hbgSaturate((0.3 - perceptualRoughness) / 0.3);
    mipLevel = clamp(mipLevel, 0.0, mix(cookieMipCount, 3.0, mipTrimming));
    const float sample =
        hbgSampleLightCookieScaledLod(lightIndex, hitUV, cookieMode, scale, offset, mipLevel);
    return vec3(sample, sample, sample);
}

#endif // HBG_AREA_LIGHTING_SC
