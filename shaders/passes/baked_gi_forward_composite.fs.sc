$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_bakedIndirect, 13);

void main()
{
    const vec4 baked = texture2D(s_bakedIndirect, vec2(v_texcoord0.x, 1.0 - v_texcoord0.y));
    gl_FragColor = vec4(baked.rgb, 0.0);
}
