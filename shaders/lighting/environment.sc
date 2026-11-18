#ifndef HBG_ENVIRONMENT_SC
#define HBG_ENVIRONMENT_SC

#include "common/common.sc"

SAMPLER2D(s_environmentMap, 10);
SAMPLER2DARRAY(s_probeMaps, 14);

vec2 hbgEquirectUV(vec3 direction)
{
    const vec3 dir = normalize(direction);
    const float u = atan2(dir.x, dir.z) * (0.5 / HBG_PI) + 0.5;
    const float v = acos(clamp(dir.y, -1.0, 1.0)) / HBG_PI;
    return vec2(u, v);
}

vec3 hbgSampleSH9(vec3 normal)
{
    const vec4 vA = vec4(normal, 1.0);
    vec3 result;
    result.r = dot(u_environment_shAr, vA);
    result.g = dot(u_environment_shAg, vA);
    result.b = dot(u_environment_shAb, vA);

    const vec4 vB = normal.xyzz * normal.yzzx;
    vec3 quadratic;
    quadratic.r = dot(u_environment_shBr, vB);
    quadratic.g = dot(u_environment_shBg, vB);
    quadratic.b = dot(u_environment_shBb, vB);

    const float vC = normal.x * normal.x - normal.y * normal.y;
    result += quadratic + u_environment_shCr.xyz * vC;
    return max(result, vec3(0.0, 0.0, 0.0));
}

vec3 hbgSampleEnvironment(vec3 direction, float perceptualRoughness)
{
    const float maxMip = max(u_environment_skyParams.y, 0.0);
    const float lod = clamp(perceptualRoughness * (1.7 - 0.7 * perceptualRoughness) * maxMip, 0.0, maxMip);
    return texture2DLod(s_environmentMap, hbgEquirectUV(direction), lod).rgb;
}

float hbgProbeFaceDistance(vec3 localPosition, vec3 extents)
{
    const vec3 remaining = extents - abs(localPosition);
    return min(remaining.x, min(remaining.y, remaining.z));
}

float hbgProbeInfluenceWeight(vec3 localPosition, vec3 extents, float blendDistance)
{
    const float faceDistance = hbgProbeFaceDistance(localPosition, extents);
    return hbgSaturate(faceDistance / max(blendDistance, 1.0e-4));
}

float hbgProbeIntersectionDistance(vec3 localPosition, vec3 localDirection, vec3 extents)
{
    vec3 direction = localDirection;
    direction.x = abs(direction.x) < 1.0e-6 ? (direction.x < 0.0 ? -1.0e-6 : 1.0e-6) : direction.x;
    direction.y = abs(direction.y) < 1.0e-6 ? (direction.y < 0.0 ? -1.0e-6 : 1.0e-6) : direction.y;
    direction.z = abs(direction.z) < 1.0e-6 ? (direction.z < 0.0 ? -1.0e-6 : 1.0e-6) : direction.z;

    const vec3 inverseDirection = 1.0 / direction;
    const vec3 t0 = (-extents - localPosition) * inverseDirection;
    const vec3 t1 = (extents - localPosition) * inverseDirection;
    const vec3 tMax = max(t0, t1);
    return min(tMax.x, min(tMax.y, tMax.z));
}

vec3 hbgSampleProbeTexture(int index, vec3 direction, float lod)
{
    return texture2DArrayLod(s_probeMaps, vec3(hbgEquirectUV(direction), float(index)), lod).rgb;
}

vec3 hbgSampleProbe(int index, vec3 worldPosition, vec3 reflectionDirection, float perceptualRoughness,
                    out float weight)
{
    weight = 0.0;
    mat4 worldToProbe = u_probe_worldToProbe0;
    vec4 probeExtents = u_probe_extents0;
    vec4 probeParams = u_probe_params0;
    if (index == 1)
    {
        worldToProbe = u_probe_worldToProbe1;
        probeExtents = u_probe_extents1;
        probeParams = u_probe_params1;
    }
    if (probeParams.x < 0.5)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const vec3 extents = max(probeExtents.xyz, vec3(1.0e-4, 1.0e-4, 1.0e-4));
    const vec3 localPosition = mul(worldToProbe, vec4(worldPosition, 1.0)).xyz;
    const vec3 localDirection = normalize(mul(worldToProbe, vec4(reflectionDirection, 0.0)).xyz);
    const float influence = hbgProbeInfluenceWeight(localPosition, extents, probeExtents.w);
    if (influence <= 0.0)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const float intersectionDistance = hbgProbeIntersectionDistance(localPosition, localDirection, extents);
    if (intersectionDistance <= 0.0)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const vec3 hitLocal = localPosition + localDirection * intersectionDistance;
    const vec3 sampleDirection = normalize(hitLocal);
    const float maxMip = max(probeParams.z, 0.0);
    const float lod = clamp(perceptualRoughness * (1.7 - 0.7 * perceptualRoughness) * maxMip, 0.0, maxMip);
    weight = influence;
    return hbgSampleProbeTexture(index, sampleDirection, lod) * probeParams.y;
}

vec3 hbgSampleProbeReflection(vec3 worldPosition, vec3 reflectionDirection, float perceptualRoughness,
                              out float probeWeight)
{
    probeWeight = 0.0;
    if (u_probe_global.x < 0.5)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    float weight0 = 0.0;
    float weight1 = 0.0;
    const vec3 color0 = hbgSampleProbe(0, worldPosition, reflectionDirection, perceptualRoughness, weight0);
    const vec3 color1 = hbgSampleProbe(1, worldPosition, reflectionDirection, perceptualRoughness, weight1);
    const float totalWeight = weight0 + weight1;
    if (totalWeight <= 1.0e-5)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    probeWeight = hbgSaturate(totalWeight);
    return (color0 + color1) / totalWeight;
}

vec3 hbgSampleReflection(vec3 worldPosition, vec3 reflectionDirection, float perceptualRoughness,
                         out float probeWeight)
{
    probeWeight = 0.0;
    const vec3 probeColor = hbgSampleProbeReflection(worldPosition, reflectionDirection, perceptualRoughness,
                                                     probeWeight);
    const float skyReflection = u_environment_skyParams.w > 0.5 ? u_environment_skyParams.x : 0.0;
    const vec3 skyColor = hbgSampleEnvironment(reflectionDirection, perceptualRoughness) * skyReflection;
    return probeColor + skyColor * (1.0 - probeWeight);
}

vec2 hbgEnvBRDFApprox(float nDotV, float perceptualRoughness)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    const vec4 r = perceptualRoughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

#endif // HBG_ENVIRONMENT_SC
