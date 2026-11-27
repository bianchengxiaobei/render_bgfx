$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_colorGradingLut, 1);

void main()
{
    const float lutSize = 64.0;
    const float denominator = lutSize - 1.0;
    float slice = 0.5 * denominator;
    float slice0 = floor(slice);
    float slice1 = min(slice0 + 1.0, denominator);
    float sliceWeight = slice - slice0;

    float displayY = 1.0 - v_texcoord0.y;
    vec2 uv0;
    uv0.x = (v_texcoord0.x * denominator + 0.5) / lutSize;
    uv0.y = (slice0 * lutSize + displayY * denominator + 0.5) / (lutSize * lutSize);
    vec2 uv1 = uv0;
    uv1.y = (slice1 * lutSize + displayY * denominator + 0.5) / (lutSize * lutSize);

    vec3 color = mix(texture2D(s_colorGradingLut, uv0).rgb, texture2D(s_colorGradingLut, uv1).rgb, sliceWeight);
    gl_FragColor = vec4(hbgLinearToSrgb(max(color, vec3(0.0, 0.0, 0.0))), 1.0);
}
