$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_decal0, 0);

void main()
{
    const vec4 decal0 = texture2D(s_decal0, v_texcoord0);
    gl_FragColor = vec4(decal0.rgb, 1.0);
}
