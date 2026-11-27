$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_source, 0);
SAMPLER2D(s_exposure, 1);

void main()
{
    float exposure = max(texture2D(s_exposure, vec2(0.5, 0.5)).x, 0.0);
    vec2 uv = vec2(v_texcoord0.x, 1.0 - v_texcoord0.y);
    gl_FragColor = vec4(texture2D(s_source, uv).rgb * exposure, 1.0);
}
