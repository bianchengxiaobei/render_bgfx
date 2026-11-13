$input v_texcoord0

#include "common/common.sc"
#include "postprocess/fxaa.sc"

void main()
{
    vec3 color = max(texture2D(s_ldrColor, v_texcoord0).rgb, vec3(0.0, 0.0, 0.0));
#if HBG_FXAA
    const vec2 texel = 1.0 / max(u_postprocess_screenParams.zw, vec2(1.0, 1.0));
    color = hbgFxaaApply(v_texcoord0, texel, color);
#endif
    gl_FragColor = vec4(hbgLinearToSrgb(color), 1.0);
}
