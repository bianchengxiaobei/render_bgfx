$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_depthSource, 0);

void main()
{
    const vec2 halfTexel = u_prepass_depthPyramidParams.xy * 0.5;
    const vec2 uv = v_texcoord0;

    const float a = texture2D(s_depthSource, uv + vec2(-halfTexel.x, -halfTexel.y)).r;
    const float b = texture2D(s_depthSource, uv + vec2(halfTexel.x, -halfTexel.y)).r;
    const float c = texture2D(s_depthSource, uv + vec2(-halfTexel.x, halfTexel.y)).r;
    const float d = texture2D(s_depthSource, uv + vec2(halfTexel.x, halfTexel.y)).r;

    const float minDepth = min(min(a, b), min(c, d));
    gl_FragColor = vec4(minDepth, minDepth, minDepth, 1.0);
}
