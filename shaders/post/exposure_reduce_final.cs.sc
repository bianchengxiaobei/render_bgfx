#include "bgfx_compute.sh"
#include "postprocess/exposure_common.sc"

SAMPLER2D(s_luminance, 0);
SAMPLER2D(s_previousExposure, 1);
IMAGE2D_WO(s_exposureOut, rgba16f, 2);

NUM_THREADS(1, 1, 1)

void main()
{
    float weightedEv = 0.0;
    float weightSum = 0.0;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            vec2 sampleValue = texelFetch(s_luminance, ivec2(x, y), 0).rg;
            weightedEv += sampleValue.x * sampleValue.y;
            weightSum += sampleValue.y;
        }
    }

    float averageEv = weightSum > 0.0 ? weightedEv / weightSum : 0.0;
    float targetEv = averageEv - u_postprocess_exposureParams.z;
    float exposureEv = targetEv;
    if (u_postprocess_tonemapParams.y > 0.5) {
        float previousEv = texelFetch(s_previousExposure, ivec2(0, 0), 0).g;
        exposureEv = hbgAdaptExposureEv100(previousEv, targetEv);
    }
    exposureEv = clamp(exposureEv, u_postprocess_autoExposureParams.x, u_postprocess_autoExposureParams.y);
    imageStore(s_exposureOut, ivec2(0, 0),
               vec4(hbgExposureFromEv100(exposureEv), exposureEv, 0.0, 1.0));
}
