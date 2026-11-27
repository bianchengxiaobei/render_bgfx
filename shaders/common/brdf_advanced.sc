#ifndef HBG_BRDF_ADVANCED_SC
#define HBG_BRDF_ADVANCED_SC

#include "common/common.sc"

float hbgDV_SmithJointGGXAniso(float TdotH, float BdotH, float NdotH,
                                float TdotV, float BdotV, float NdotV,
                                float TdotL, float BdotL, float NdotL,
                                float roughnessT, float roughnessB)
{
    const float a2 = roughnessT * roughnessB;
    const vec3 v = vec3(roughnessB * TdotH, roughnessT * BdotH, a2 * NdotH);
    const float s = dot(v, v);
    const float partLambdaV = length(vec3(roughnessT * TdotV, roughnessB * BdotV, NdotV));
    const float lambdaV = NdotL * partLambdaV;
    const float lambdaL = NdotV * length(vec3(roughnessT * TdotL, roughnessB * BdotL, NdotL));
    const float d = a2 * a2 * a2;
    return (HBG_INV_PI * 0.5) * d / max(s * s * (lambdaV + lambdaL), 1.0e-7);
}

vec3 hbgEvalIridescence(float eta1, float cosTheta1, float thicknessMicrons, vec3 baseLayerFresnel0)
{
    const float path = 2.0 * max(eta1, 1.0) * max(thicknessMicrons, 0.0) * 1000.0 * cosTheta1;
    const vec3 phase = (2.0 * HBG_PI * path) / vec3(680.0, 550.0, 440.0);
    const vec3 tint = 0.5 + 0.5 * cos(phase);
    return mix(baseLayerFresnel0, tint, 0.35);
}

#endif // HBG_BRDF_ADVANCED_SC