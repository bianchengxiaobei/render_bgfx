$input v_texcoord0

#include "common/common.sc"
#include "postprocess/bloom.sc"

SAMPLER2D(s_source, 0);

vec3 hbgBloomSample(vec2 uv, vec2 offset, out float weight)
{
    vec3 color = max(texture2D(s_source, uv + offset * u_postprocess_bloomTexelSize.zw).rgb, vec3(0.0, 0.0, 0.0));
    color = hbgBloomThreshold(color, u_postprocess_bloomParams.y);
    weight = hbgBloomWeight(color);
    return color;
}

void main()
{
    vec2 uv = v_texcoord0;
    vec3 sum = vec3(0.0, 0.0, 0.0);
    float weightSum = 0.0;

#ifdef BLOOM_HQ
    const vec2 taps[13] = {
        vec2(-1.0, -1.0), vec2(0.0, -1.0), vec2(1.0, -1.0),
        vec2(-0.5, -0.5), vec2(0.5, -0.5),
        vec2(-1.0, 0.0), vec2(0.0, 0.0), vec2(1.0, 0.0),
        vec2(-0.5, 0.5), vec2(0.5, 0.5),
        vec2(-1.0, 1.0), vec2(0.0, 1.0), vec2(1.0, 1.0)
    };
    for (int i = 0; i < 13; ++i) {
        float weight;
        vec3 color = hbgBloomSample(uv, taps[i], weight);
        sum += color * weight;
        weightSum += weight;
    }
#else
    const vec2 taps[5] = {
        vec2(0.0, 0.0), vec2(0.9, -0.4), vec2(-0.9, 0.4), vec2(0.4, 0.9), vec2(-0.4, -0.9)
    };
    for (int i = 0; i < 5; ++i) {
        float weight;
        vec3 color = hbgBloomSample(uv, taps[i], weight);
        sum += color * weight;
        weightSum += weight;
    }
#endif

    gl_FragColor = vec4(max(sum / max(weightSum, 1.0e-4), vec3(0.0, 0.0, 0.0)), 1.0);
}
