$input v_uv, v_color, v_worldPos, v_normal

#include "vfx/vfx_spike_common.sc"

SAMPLER2D(s_vfxMain, 0);

void main()
{
    const vec4 texel = texture2D(s_vfxMain, v_uv);
    gl_FragColor = vec4(v_color.rgb * texel.rgb, v_color.a * texel.a);
}
