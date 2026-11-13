#ifndef HBG_SHADOW_DEBUG_SC
#define HBG_SHADOW_DEBUG_SC

#include "common/common.sc"

int hbgShadowDebugCascade(float viewDepth)
{
    const int cascadeCount = int(u_pass_shadowParams.w + 0.5);
    if (u_pass_shadowParams.z < 0.5 || cascadeCount <= 0)
    {
        return -1;
    }

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
        return -1;
    }
    return cascade;
}

vec3 hbgShadowDebugCascadeColor(int cascade)
{
    if (cascade == 0)
    {
        return vec3(0.4, 0.4, 0.9);
    }
    if (cascade == 1)
    {
        return vec3(0.4, 0.9, 0.4);
    }
    if (cascade == 2)
    {
        return vec3(0.9, 0.9, 0.4);
    }
    if (cascade == 3)
    {
        return vec3(0.9, 0.4, 0.4);
    }
    return vec3(1.0, 1.0, 1.0);
}

#endif // HBG_SHADOW_DEBUG_SC
