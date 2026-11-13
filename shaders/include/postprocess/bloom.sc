#ifndef HBG_POSTPROCESS_BLOOM_SC
#define HBG_POSTPROCESS_BLOOM_SC

#include "common/common.sc"

vec3 hbgBloomThreshold(vec3 color, float threshold)
{
    float knee = threshold * 0.5 + 1.0e-5;
    float brightness = max(color.x, max(color.y, color.z));
    float response = clamp(brightness - (threshold - knee), 0.0, knee * 2.0);
    response = (0.25 / knee) * response * response;
    float scale = max(response, brightness - threshold) / max(brightness, 1.0e-4);
    return color * max(scale, 0.0);
}

float hbgBloomWeight(vec3 color)
{
    return 1.0 / (max(hbgLuminance(color), 0.0) + 1.0);
}

#endif // HBG_POSTPROCESS_BLOOM_SC
