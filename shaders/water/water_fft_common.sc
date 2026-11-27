#ifndef HBG_WATER_FFT_COMMON_SC
#define HBG_WATER_FFT_COMMON_SC

#include "bgfx_compute.sh"

#define HBG_WATER_FFT_SIZE 128u
#define HBG_WATER_FFT_WORK_LAYERS 5u
#define HBG_WATER_FFT_LAYERS 5u

#define HBG_WATER_PI 3.14159265358979323846
#define HBG_WATER_GRAVITY 9.81
#define HBG_WATER_KM 370.0

vec2 hbgWaterComplexMul(vec2 a, vec2 b)
{
    return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

float hbgWaterOmega(float k)
{
    return sqrt(HBG_WATER_GRAVITY * k * (1.0 + k * k / (HBG_WATER_KM * HBG_WATER_KM)));
}

vec2 hbgWaterSpectrum(vec2 k, vec2 spectrum, float t)
{
    float magnitude = length(k);
    float omega = hbgWaterOmega(magnitude);
    float direction = (k.x > 0.0 || (k.x == 0.0 && k.y > 0.0)) ? 1.0 : -1.0;
    float phase = direction * omega * t;
    return hbgWaterComplexMul(spectrum, vec2(cos(phase), sin(phase)));
}

vec2 hbgWaterSlopeSpectrum(vec2 k, vec2 h)
{
    return hbgWaterComplexMul(h, vec2(-k.y, k.x));
}

vec2 hbgWaterChoppySpectrum(vec2 k, vec2 h)
{
    float magnitude = length(k);
    if (magnitude <= 1.0e-8)
    {
        return vec2(0.0, 0.0);
    }
    return hbgWaterSlopeSpectrum(k, h) * (1.0 / magnitude);
}

#endif
