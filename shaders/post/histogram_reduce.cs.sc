#include "bgfx_compute.sh"
#include "postprocess/exposure_common.sc"

UIMAGE2D_RO(s_histogram, r32ui, 0);
SAMPLER2D(s_previousExposure, 1);
IMAGE2D_WO(s_exposureOut, rgba16f, 2);

NUM_THREADS(1, 1, 1)

void main()
{
    float bins[128];
    float total = 0.0;
    for (int i = 0; i < 128; ++i) {
        bins[i] = float(imageLoad(s_histogram, ivec2(i, 0)).x) / 2048.0;
        total += bins[i];
    }

    float lowTrim = total * u_postprocess_histogramParams.z;
    for (int lowIndex = 0; lowIndex < 128 && lowTrim > 0.0; ++lowIndex) {
        float cut = min(bins[lowIndex], lowTrim);
        bins[lowIndex] -= cut;
        lowTrim -= cut;
    }

    float highTrim = total * (1.0 - u_postprocess_histogramParams.w);
    for (int highIndex = 127; highIndex >= 0 && highTrim > 0.0; --highIndex) {
        float cut = min(bins[highIndex], highTrim);
        bins[highIndex] -= cut;
        highTrim -= cut;
    }

    float weightedEv = 0.0;
    float weightSum = 0.0;
    for (int binIndex = 0; binIndex < 128; ++binIndex) {
        weightedEv += hbgBinToEv100(float(binIndex)) * bins[binIndex];
        weightSum += bins[binIndex];
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
