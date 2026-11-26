$input v_uv

#include "bgfx_shader.sh"

SAMPLER2D(s_vfxMain, 0);
uniform vec4 u_vfxShadowParams;

void main()
{
    if (u_vfxShadowParams.y > 0.5 && texture2D(s_vfxMain, v_uv).a < u_vfxShadowParams.x)
    {
        discard;
    }
    gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
