#ifndef HBG_POSTPROCESS_EXPOSURE_COMMON_SC
#define HBG_POSTPROCESS_EXPOSURE_COMMON_SC

#include "common/common.sc"

#define HBG_EXPOSURE_CALIBRATION 12.5

float hbgEv100FromLuminance(float luminance)
{
    return log2(max(luminance, 1.0e-4) * 100.0 / HBG_EXPOSURE_CALIBRATION);
}

float hbgExposureFromEv100(float ev100)
{
    return 1.0 / max(u_postprocess_exposureParams.w * exp2(ev100), 1.0e-6);
}

float hbgAdaptExposureEv100(float previousEv100, float targetEv100)
{
    if (u_postprocess_tonemapParams.y < 0.5) {
        return targetEv100;
    }
    float delta = targetEv100 - previousEv100;
    float speed = delta > 0.0 ? u_postprocess_autoExposureParams.z : u_postprocess_autoExposureParams.w;
    float time = max(u_postprocess_tonemapParams.w, 0.0);
    return previousEv100 + delta * (1.0 - exp2(-time * speed));
}

float hbgMeteringWeight(vec2 pixel, vec2 size)
{
    if (u_postprocess_tonemapParams.z < 0.5) {
        return 1.0;
    }
    vec2 center = size * 0.5;
    float diagonal = 0.5 * (size.x + size.y);
    return 1.0 - hbgSaturate(length(pixel - center) / max(diagonal, 1.0));
}

float hbgBinToEv100(float bin)
{
    return (bin / 127.0 - u_postprocess_histogramParams.y) / max(u_postprocess_histogramParams.x, 1.0e-6);
}

#endif
