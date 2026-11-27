#ifndef HBG_POSTPROCESS_DOF_SC
#define HBG_POSTPROCESS_DOF_SC

#include "common/common.sc"

float hbgDofLinearEyeDepth(float depth, vec2 sourceUv)
{
    vec4 worldPosition = mul(u_global_invViewProj, vec4(sourceUv * 2.0 - 1.0, depth, 1.0));
    worldPosition.xyz /= max(worldPosition.w, 1.0e-6);
    vec4 viewPosition = mul(u_global_view, vec4(worldPosition.xyz, 1.0));
    return max(-viewPosition.z, 0.0);
}

float hbgDofNormalizedCoc(float linearEyeDepth)
{
    const float nearEnd = max(u_postprocess_dofParams.y, 0.0);
    const float nearStart = min(u_postprocess_dofParams.x, nearEnd - 1.0e-5);
    const float farStart = max(u_postprocess_dofParams.z, nearEnd);
    const float farEnd = max(u_postprocess_dofParams.w, farStart + 1.0e-5);

    const float nearCoc = hbgSaturate((linearEyeDepth - nearEnd) / min(nearStart - nearEnd, -1.0e-5));
    const float farCoc = hbgSaturate((linearEyeDepth - farStart) / max(farEnd - farStart, 1.0e-5));
    return farCoc - nearCoc;
}

float hbgDofMaxBlurPixels(float coc)
{
    return coc < 0.0 ? u_postprocess_dofParams2.x : u_postprocess_dofParams2.y;
}

float hbgDofRadiusPixels(float coc, float resolutionScale)
{
    return abs(coc) * hbgDofMaxBlurPixels(coc) * resolutionScale;
}

#endif
