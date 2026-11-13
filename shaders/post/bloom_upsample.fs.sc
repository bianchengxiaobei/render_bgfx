$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_source, 0);
SAMPLER2D(s_source2, 1);

float hbgCubicWeight(float value)
{
    value = abs(value);
    if (value <= 1.0) {
        return (1.5 * value - 2.5) * value * value + 1.0;
    }
    if (value < 2.0) {
        return ((-0.5 * value + 2.5) * value - 4.0) * value + 2.0;
    }
    return 0.0;
}

void main()
{
    vec2 uv = v_texcoord0;
    vec3 high = max(texture2D(s_source2, uv).rgb, vec3(0.0, 0.0, 0.0));
    vec3 low;

#ifdef BLOOM_BICUBIC
    vec2 size = max(u_postprocess_bloomTexelSize.xy, vec2(1.0, 1.0));
    vec2 coord = uv * size - vec2(0.5, 0.5);
    vec2 base = floor(coord);
    vec2 fraction = coord - base;
    float wx[4] = {
        hbgCubicWeight(1.0 + fraction.x),
        hbgCubicWeight(fraction.x),
        hbgCubicWeight(1.0 - fraction.x),
        hbgCubicWeight(2.0 - fraction.x)
    };
    float wy[4] = {
        hbgCubicWeight(1.0 + fraction.y),
        hbgCubicWeight(fraction.y),
        hbgCubicWeight(1.0 - fraction.y),
        hbgCubicWeight(2.0 - fraction.y)
    };
    low = vec3(0.0, 0.0, 0.0);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            vec2 sampleUv = (base + vec2(float(x), float(y)) - vec2(1.0, 1.0) + vec2(0.5, 0.5)) / size;
            low += texture2D(s_source, sampleUv).rgb * (wx[x] * wy[y]);
        }
    }
#else
    vec2 texel = u_postprocess_bloomTexelSize.zw;
    low = vec3(0.0, 0.0, 0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float weight = (x == 0 ? 0.5 : 0.25) * (y == 0 ? 0.5 : 0.25);
            low += texture2D(s_source, uv + vec2(float(x), float(y)) * texel).rgb * weight;
        }
    }
#endif

    gl_FragColor = vec4(max(mix(high, low, u_postprocess_bloomParams.w), vec3(0.0, 0.0, 0.0)), 1.0);
}
