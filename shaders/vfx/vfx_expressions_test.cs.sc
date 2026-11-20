#define VFX_ATTRIBUTE_BUFFER_STAGE 0

#include "vfx/vfx_attributes.sc"

uniform vec4 u_vfxTime;

static const float vfxProp_Size = 1.0;
static const vec4 vfxProp_Tint = vec4(1.0, 0.8, 0.4, 1.0);
static const float vfxProp_SizeCurve = 1.0;
static const vec4 vfxProp_ColorRamp = vec4(1.0, 1.0, 1.0, 1.0);

#define VFX_ATTR(name) attributes.Load_##name()
#define VFX_PROP(name) vfxProp_##name
#define VFX_TIME (u_vfxTime.x)
#define VFX_DT (u_vfxTime.y)
#define VFX_LIFETIME_RATIO (attributes.Load_age() / max(attributes.Load_lifetime(), 1e-6))

float vfxRand(uint seed)
{
    return float(seed & 0xffffu) / 65535.0;
}

vec4 vfxRand4(uint seed)
{
    return vec4(vfxRand(seed), vfxRand(seed + 1u), vfxRand(seed + 2u), vfxRand(seed + 3u));
}

float vfxNoise(float position, float frequency, uint seed)
{
    return fract(position * frequency + float(seed));
}

float vfxNoise(vec2 position, float frequency, uint seed)
{
    return fract(position.x * frequency + position.y + float(seed));
}

float vfxNoise(vec3 position, float frequency, uint seed)
{
    return fract(position.x * frequency + position.y + position.z + float(seed));
}

float vfxSampleCurve(float t)
{
    return saturate(t);
}

#define VFX_RAND(seed) vfxRand(seed)
#define VFX_RAND4(seed) vfxRand4(seed)
#define VFX_NOISE(position, frequency, seed) vfxNoise(position, frequency, seed)
#define VFX_SAMPLE_CURVE(name, t) vfxSampleCurve(t)
#define VFX_SAMPLE_GRADIENT(name, t) vec4(vfxSampleCurve(t), 0.0, 0.0, 1.0)

#include "vfx/vfx_expressions.sc"

BUFFER_RAW_RW(s_vfxExpressionResult, 1);

NUM_THREADS(64, 1, 1)

void main()
{
    const uint index = gl_GlobalInvocationID.x;
    VfxAttributes attributes;
    attributes.Init(index);
    float result = 0.0;
    vfxTestExpressions(attributes, result);
    if (index == 0u)
    {
        rawStoreFloat(s_vfxExpressionResult, 0u, result);
    }
}
