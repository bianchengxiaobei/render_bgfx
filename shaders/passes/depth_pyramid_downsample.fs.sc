$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_depthSource, 0);

void main()
{
    const vec2 halfTexel = u_prepass_depthPyramidParams.xy * 0.5;
    const vec2 uv = v_texcoord0;

    const vec2 a = texture2D(s_depthSource, uv + vec2(-halfTexel.x, -halfTexel.y)).xy;
    const vec2 b = texture2D(s_depthSource, uv + vec2( halfTexel.x, -halfTexel.y)).xy;
    const vec2 c = texture2D(s_depthSource, uv + vec2(-halfTexel.x,  halfTexel.y)).xy;
    const vec2 d = texture2D(s_depthSource, uv + vec2( halfTexel.x,  halfTexel.y)).xy;

    gl_FragColor = vec4(min(min(a, b), min(c, d)), 0.0, 1.0);
}
