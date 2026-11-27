$input v_texcoord0

#include "common/common.sc"
#include "postprocess/dof.sc"

SAMPLER2D(s_source, 0);
SAMPLER2D(s_dofCoc, 1);

void main()
{
    const vec2 sourceUv = vec2(v_texcoord0.x, 1.0 - v_texcoord0.y);
    const vec2 cocUv = v_texcoord0;
    const vec2 cocTexel = u_postprocess_dofCocSize.zw;

    const vec4 centerData = texture2D(s_dofCoc, cocUv);
    const float centerCoc = centerData.x;
    const float centerDepth = centerData.y;
    const vec3 centerColor = max(texture2D(s_source, sourceUv).rgb, vec3(0.0, 0.0, 0.0));
    const float radiusPixels = hbgDofRadiusPixels(centerCoc, u_postprocess_dofParams2.z);

    if (radiusPixels < 0.5) {
        gl_FragColor = vec4(centerColor, 1.0);
        return;
    }

#ifdef DOF_HQ
    const int tapCount = 12;
#else
    const int tapCount = 8;
#endif

    const float depthTolerance = max(0.1, centerDepth * 0.05);
    const vec2 uvMin = cocTexel * 0.5;
    const vec2 uvMax = vec2(1.0, 1.0) - uvMin;

    vec3 sum = centerColor;
    float weightSum = 1.0;

    for (int i = 0; i < 12; ++i) {
        if (i >= tapCount) {
            break;
        }

        const float fi = float(i);
        const float radius = sqrt((fi + 0.5) / float(tapCount)) * radiusPixels;
        const float angle = fi * 2.39996322973;
        const vec2 offset = vec2(cos(angle), sin(angle)) * radius;
        const vec2 tapCocUv = clamp(cocUv + offset * cocTexel, uvMin, uvMax);
        const vec2 tapSourceUv = clamp(sourceUv + offset * cocTexel, uvMin, uvMax);
        const vec4 tapData = texture2DLod(s_dofCoc, tapCocUv, 0.0);
        const vec3 tapColor = max(texture2DLod(s_source, tapSourceUv, 0.0).rgb, vec3(0.0, 0.0, 0.0));

        float weight = 0.0;
        if (centerCoc > 0.0) {
            const float layerWeight = hbgSaturate(tapData.x * 2.0);
            const float depthWeight = hbgSaturate(1.0 - (centerDepth - tapData.y) / depthTolerance);
            weight = layerWeight * depthWeight;
        } else {
            const float layerWeight = hbgSaturate(-tapData.x * 2.0);
            const float depthWeight = hbgSaturate(1.0 - (tapData.y - centerDepth) / depthTolerance);
            weight = layerWeight * depthWeight;
        }

        sum += tapColor * weight;
        weightSum += weight;
    }

    gl_FragColor = vec4(sum / max(weightSum, 1.0e-4), 1.0);
}
