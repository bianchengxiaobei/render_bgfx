$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_bloom, 0);

void main()
{
    vec2 uv = vec2(v_texcoord0.x, 1.0 - v_texcoord0.y);
    vec3 color = max(texture2D(s_bloom, uv).rgb, vec3(0.0, 0.0, 0.0));
    gl_FragColor = vec4(hbgLinearToSrgb(color), 1.0);
}
