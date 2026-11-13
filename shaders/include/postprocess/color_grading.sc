#ifndef HBG_POSTPROCESS_COLOR_GRADING_SC
#define HBG_POSTPROCESS_COLOR_GRADING_SC

#include "common/common.sc"

// Alexa LogC (EI 1000), matching HDRP/Core Color.hlsl.
#define HBG_LOGC_CUT 0.011361
#define HBG_LOGC_A 5.555556
#define HBG_LOGC_B 0.047996
#define HBG_LOGC_C 0.244161
#define HBG_LOGC_D 0.386036
#define HBG_LOGC_E 5.301883
#define HBG_LOGC_F 0.092819
#define HBG_LOG10_2 0.30102999566398119521

SAMPLER2D(s_colorGradingLut, 1);

float hbgLinearToLogCChannel(float value)
{
    return HBG_LOGC_C * log2(max(HBG_LOGC_A * value + HBG_LOGC_B, 0.0)) * HBG_LOG10_2 + HBG_LOGC_D;
}

vec3 hbgLinearToLogC(vec3 value)
{
    return vec3(hbgLinearToLogCChannel(value.x), hbgLinearToLogCChannel(value.y),
                hbgLinearToLogCChannel(value.z));
}

float hbgLogCToLinearChannel(float value)
{
    return (exp2((value - HBG_LOGC_D) / HBG_LOGC_C / HBG_LOG10_2) - HBG_LOGC_B) / HBG_LOGC_A;
}

vec3 hbgLogCToLinear(vec3 value)
{
    return vec3(hbgLogCToLinearChannel(value.x), hbgLogCToLinearChannel(value.y),
                hbgLogCToLinearChannel(value.z));
}

// The LUT is stored as a vertical 32x1024 strip: one 32x32 slice per blue value.
// Bilinear sampling happens inside a slice; the two neighboring slices are
// blended manually because 2D strip textures have no hardware trilinear lookup.
vec3 hbgSampleColorGradingLut(vec3 logColor)
{
    float lutSize = max(u_postprocess_gradingParams.y, 2.0);
    float denominator = lutSize - 1.0;
    vec3 value = clamp(logColor, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0));

    float z = value.z * denominator;
    float slice = floor(z);
    float sliceWeight = z - slice;
    float slice0 = slice;
    float slice1 = min(slice + 1.0, denominator);

    vec2 uv0;
    uv0.x = (value.x * denominator + 0.5) / lutSize;
    uv0.y = (slice0 * lutSize + value.y * denominator + 0.5) / (lutSize * lutSize);
    vec2 uv1;
    uv1.x = uv0.x;
    uv1.y = (slice1 * lutSize + value.y * denominator + 0.5) / (lutSize * lutSize);

    vec3 color0 = texture2D(s_colorGradingLut, uv0).rgb;
    vec3 color1 = texture2D(s_colorGradingLut, uv1).rgb;
    return mix(color0, color1, sliceWeight);
}

vec3 hbgApplyVignette(vec3 color, vec2 uv)
{
    if (u_postprocess_gradingParams.w < 0.5) {
        return color;
    }

    vec2 delta = abs(uv - u_postprocess_vignetteParams.xy) * u_postprocess_vignetteParams.z;
    float aspect = u_postprocess_screenParams.x / max(u_postprocess_screenParams.y, 1.0);
    delta.x *= mix(1.0, aspect, u_postprocess_vignetteParams2.y);
    delta = pow(saturate(delta), vec2(u_postprocess_vignetteParams2.x, u_postprocess_vignetteParams2.x));
    float factor = pow(saturate(1.0 - dot(delta, delta)), u_postprocess_vignetteParams.w);
    return color * mix(u_postprocess_vignetteColor.xyz, vec3(1.0, 1.0, 1.0), factor);
}

#endif // HBG_POSTPROCESS_COLOR_GRADING_SC
