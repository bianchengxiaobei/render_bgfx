#ifndef HBG_ENVIRONMENT_SC
#define HBG_ENVIRONMENT_SC

#include "common/common.sc"

SAMPLER2D(s_environmentMap, 10);

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

vec2 hbgEnvBRDFApprox(float nDotV, float perceptualRoughness)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    const vec4 r = perceptualRoughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

#endif // HBG_ENVIRONMENT_SC
