#ifndef HBG_SSGI_FILTER_SC
#define HBG_SSGI_FILTER_SC

#include "common/common.sc"

SAMPLER2D(s_ssgi, 0);
SAMPLER2D(s_depthPyramid, 4);

vec3 hbgSampleSsgiFiltered(vec2 uv, out float centerDepth)
{
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    const bool halfResolution = u_ssgi_params2.z > 0.5;
    const vec2 lowSize = max(vec2(1.0, 1.0), ceil(screenSize * (halfResolution ? 0.5 : 1.0)));
    const vec2 lowTexel = 1.0 / lowSize;
    uv = clamp(uv, lowTexel * 0.5, vec2(1.0, 1.0) - lowTexel * 0.5);

    centerDepth = texture2DLod(s_depthPyramid, uv, 0.0).r;
    const float depthTolerance = max(centerDepth * 0.05, 0.001);
    vec3 total = vec3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;
    for (int y = -4; y <= 4; ++y)
    {
        for (int x = -4; x <= 4; ++x)
        {
            const vec2 sampleUV =
                clamp(uv + vec2(float(x), float(y)) * lowTexel, lowTexel * 0.5,
                      vec2(1.0, 1.0) - lowTexel * 0.5);
            const float sampleDepth = texture2DLod(s_depthPyramid, sampleUV, 0.0).r;
            const float depthWeight =
                sampleDepth >= 0.999999
                    ? 0.0
                    : 1.0 - hbgSaturate(abs(sampleDepth - centerDepth) / depthTolerance);
            const float spatialWeight = 1.0 / (1.0 + 0.15 * float(x * x + y * y));
            const float weight = depthWeight * spatialWeight;
            total += max(texture2DLod(s_ssgi, sampleUV, 0.0).rgb, vec3(0.0, 0.0, 0.0)) * weight;
            totalWeight += weight;
        }
    }

    vec3 result = max(texture2DLod(s_ssgi, uv, 0.0).rgb, vec3(0.0, 0.0, 0.0));
    if (totalWeight > 1.0e-4)
    {
        result = total / totalWeight;
    }
    return result;
}

#endif // HBG_SSGI_FILTER_SC
