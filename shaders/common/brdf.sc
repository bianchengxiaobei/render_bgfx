#ifndef HBG_BRDF_SC
#define HBG_BRDF_SC

#include "common/common.sc"

// Reference: Unity render-pipelines.core/ShaderLibrary/BSDF.hlsl
float hbgD_GGX(float NdotH, float roughness)
{
    float a2 = roughness * roughness;
    float s = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return HBG_INV_PI * (a2 / max(s * s, 1.0e-7));
}

float hbgV_SmithJointGGX(float NdotL, float NdotV, float roughness)
{
    float a2 = roughness * roughness;
    float lambdaV = NdotL * sqrt((-NdotV * a2 + NdotV) * NdotV + a2);
    float lambdaL = NdotV * sqrt((-NdotL * a2 + NdotL) * NdotL + a2);
    return 0.5 / max(lambdaV + lambdaL, 1.0e-7);
}

vec3 hbgF_Schlick(vec3 f0, float LdotH)
{
    float x = 1.0 - LdotH;
    float x2 = x * x;
    float x5 = x * x2 * x2;
    return f0 + (vec3(1.0, 1.0, 1.0) - f0) * x5;
}

float hbgDisneyDiffuse(float NdotV, float NdotL, float LdotV, float perceptualRoughness)
{
    float fd90 = 0.5 + (perceptualRoughness + perceptualRoughness * LdotV);
    float lightScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotL, 5.0);
    float viewScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotV, 5.0);
    return (1.0 / 1.03571) * lightScatter * viewScatter * HBG_INV_PI;
}

float hbgLambert()
{
    return HBG_INV_PI;
}

#endif // HBG_BRDF_SC
