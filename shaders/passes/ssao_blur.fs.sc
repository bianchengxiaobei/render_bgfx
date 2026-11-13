$input v_texcoord0

#include "common/common.sc"
#include "material/normal_buffer.sc"

SAMPLER2D(s_ssaoInput, 0);
SAMPLER2D(s_depthPyramid, 4);
SAMPLER2D(s_gbuffer2, 2);

void main()
{
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    const bool halfResolution = u_ssao_params2.z > 0.5;
    const vec2 aoSize = max(vec2(1.0, 1.0), ceil(screenSize * (halfResolution ? 0.5 : 1.0)));
    const vec2 texel = 1.0 / aoSize;
    const vec2 uv = clamp(v_texcoord0, texel * 0.5, vec2(1.0, 1.0) - texel * 0.5);
    const float centerDepth = texture2D(s_depthPyramid, uv).r;
    const vec3 centerNormal = hbgDecodeNormalBuffer(texture2D(s_gbuffer2, vec2(uv.x, 1.0 - uv.y)));
    const float depthTolerance = max(centerDepth * 0.02, 0.0005);

    float total = 0.0;
    float totalWeight = 0.0;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            const vec2 sampleUV = clamp(uv + vec2(float(x), float(y)) * texel,
                                        texel * 0.5, vec2(1.0, 1.0) - texel * 0.5);
            const float sampleDepth = texture2D(s_depthPyramid, sampleUV).r;
            const vec3 sampleNormal =
                hbgDecodeNormalBuffer(texture2D(s_gbuffer2, vec2(sampleUV.x, 1.0 - sampleUV.y)));
            const float normalWeight = smoothstep(0.80, 0.98, dot(centerNormal, sampleNormal));
            const float depthWeight = 1.0 - hbgSaturate(abs(sampleDepth - centerDepth) / depthTolerance);
            const float spatialWeight = 1.0 / (1.0 + 0.30 * float(x * x + y * y));
            const float weight = normalWeight * depthWeight * spatialWeight;
            total += texture2D(s_ssaoInput, sampleUV).r * weight;
            totalWeight += weight;
        }
    }

    const float result = totalWeight > 1.0e-4 ? total / totalWeight : texture2D(s_ssaoInput, uv).r;
    gl_FragColor = vec4(result, result, result, 1.0);
}
