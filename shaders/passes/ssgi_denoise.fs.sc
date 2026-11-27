$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_ssgiRaw, 0);
SAMPLER2D(s_depthPyramid, 4);

void main()
{
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    const bool halfResolution = u_ssgi_params2.z > 0.5;
    const vec2 traceSize = max(vec2(1.0, 1.0), ceil(screenSize * (halfResolution ? 0.5 : 1.0)));
    const vec2 texel = 1.0 / traceSize;
    const vec2 uv = clamp(v_texcoord0, texel * 0.5, vec2(1.0, 1.0) - texel * 0.5);
    const float centerDepth = texture2DLod(s_depthPyramid, uv, 0.0).r;
    if (centerDepth >= 0.999999)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    vec3 total = vec3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;
    for (int y = -4; y <= 4; ++y)
    {
        for (int x = -4; x <= 4; ++x)
        {
            const vec2 sampleUV =
                clamp(uv + vec2(float(x), float(y)) * texel, texel * 0.5,
                      vec2(1.0, 1.0) - texel * 0.5);
            const float sampleDepth = texture2DLod(s_depthPyramid, sampleUV, 0.0).r;
            const float depthWeight =
                sampleDepth >= 0.999999
                    ? 0.0
                    : 1.0 - hbgSaturate(abs(sampleDepth - centerDepth) /
                                        max(centerDepth * 0.05, 0.001));
            const float spatialWeight = 1.0 / (1.0 + 0.15 * float(x * x + y * y));
            const float weight = depthWeight * spatialWeight;
            total += max(texture2DLod(s_ssgiRaw, sampleUV, 0.0).rgb, vec3(0.0, 0.0, 0.0)) * weight;
            totalWeight += weight;
        }
    }

    const vec3 result =
        totalWeight > 1.0e-4 ? total / totalWeight
                             : max(texture2DLod(s_ssgiRaw, uv, 0.0).rgb, vec3(0.0, 0.0, 0.0));
    gl_FragColor = vec4(min(result, vec3(8.0, 8.0, 8.0)), 1.0);
}
