$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_shadowCachedAtlas, 0);
uniform vec4 u_shadowCopyParams;

void main()
{
    const vec2 uv = gl_FragCoord.xy * u_shadowCopyParams.zw;
    gl_FragDepth = texture2DLod(s_shadowCachedAtlas, uv, 0.0).x;
}
