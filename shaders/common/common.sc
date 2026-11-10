#ifndef HBG_COMMON_SC
#define HBG_COMMON_SC

#include <bgfx_shader.sh>
#include "generated/uniforms.sc"

#define HBG_PI      3.14159265358979323846
#define HBG_INV_PI  0.31830988618379067154

float hbgSaturate(float value)
{
    return clamp(value, 0.0, 1.0);
}

vec3 hbgSaturate(vec3 value)
{
    return clamp(value, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0));
}

vec2 hbgSaturate(vec2 value)
{
    return clamp(value, vec2(0.0, 0.0), vec2(1.0, 1.0));
}

vec3 hbgLinearToSrgb(vec3 value)
{
    vec3 low = value * 12.92;
    vec3 high = 1.055 * pow(max(value, vec3(0.0, 0.0, 0.0)), vec3(1.0 / 2.4, 1.0 / 2.4, 1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308, 0.0031308, 0.0031308), value));
}

float hbgLuminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

#endif // HBG_COMMON_SC
