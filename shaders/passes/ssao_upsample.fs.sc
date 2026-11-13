$input v_texcoord0

#include "common/common.sc"
#include "material/normal_buffer.sc"

SAMPLER2D(s_ssaoInput, 0);
SAMPLER2D(s_depthPyramid, 4);
SAMPLER2D(s_gbuffer2, 2);

void hbgAccumulateSsaoTap(vec2 lowPixel, vec2 lowSize, float bilinear, float centerDepth, vec3 centerNormal,
                          inout float total, inout float totalWeight)
{
    const vec2 sampleUV = (lowPixel + vec2(0.5, 0.5)) / lowSize;
    const float sampleDepth = texture2D(s_depthPyramid, sampleUV).r;
    const vec3 sampleNormal = hbgDecodeNormalBuffer(texture2D(s_gbuffer2, vec2(sampleUV.x, 1.0 - sampleUV.y)));
    const float normalWeight = smoothstep(0.80, 0.98, dot(centerNormal, sampleNormal));
    const float depthTolerance = max(centerDepth * 0.02, 0.0005);
    const float depthWeight = 1.0 - hbgSaturate(abs(sampleDepth - centerDepth) / depthTolerance);
    const float weight = bilinear * normalWeight * depthWeight;
    total += texture2D(s_ssaoInput, sampleUV).r * weight;
    totalWeight += weight;
}

void main()
{
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    const bool halfResolution = u_ssao_params2.z > 0.5;
    const vec2 lowSize = max(vec2(1.0, 1.0), ceil(screenSize * (halfResolution ? 0.5 : 1.0)));
    const vec2 uv = clamp(v_texcoord0, vec2(0.0, 0.0), vec2(1.0, 1.0));
    const float centerDepth = texture2D(s_depthPyramid, uv).r;
    const vec3 centerNormal = hbgDecodeNormalBuffer(texture2D(s_gbuffer2, vec2(uv.x, 1.0 - uv.y)));

    const vec2 lowPosition = uv * lowSize - 0.5;
    const vec2 base = floor(lowPosition);
    const vec2 fraction = lowPosition - base;
    const vec2 lowMax = lowSize - vec2(1.0, 1.0);

    float total = 0.0;
    float totalWeight = 0.0;
    hbgAccumulateSsaoTap(clamp(base + vec2(0.0, 0.0), vec2(0.0, 0.0), lowMax),
                         lowSize, (1.0 - fraction.x) * (1.0 - fraction.y), centerDepth, centerNormal, total,
                         totalWeight);
    hbgAccumulateSsaoTap(clamp(base + vec2(1.0, 0.0), vec2(0.0, 0.0), lowMax), lowSize,
                         fraction.x * (1.0 - fraction.y), centerDepth, centerNormal, total, totalWeight);
    hbgAccumulateSsaoTap(clamp(base + vec2(0.0, 1.0), vec2(0.0, 0.0), lowMax), lowSize,
                         (1.0 - fraction.x) * fraction.y, centerDepth, centerNormal, total, totalWeight);
    hbgAccumulateSsaoTap(clamp(base + vec2(1.0, 1.0), vec2(0.0, 0.0), lowMax), lowSize,
                         fraction.x * fraction.y, centerDepth, centerNormal, total, totalWeight);

    const float result = totalWeight > 1.0e-4 ? total / totalWeight : texture2D(s_ssaoInput, uv).r;
    gl_FragColor = vec4(result, result, result, 1.0);
}
