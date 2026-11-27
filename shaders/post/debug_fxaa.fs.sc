$input v_texcoord0

#include "common/common.sc"
#include "postprocess/fxaa.sc"

void main()
{
    const vec2 texel = 1.0 / max(u_postprocess_screenParams.zw, vec2(1.0, 1.0));
    const float edge = hbgFxaaEdgeStrength(v_texcoord0, texel);
    const vec3 low = vec3(0.02, 0.03, 0.08);
    const vec3 high = vec3(1.0, 0.85, 0.15);
    gl_FragColor = vec4(hbgLinearToSrgb(mix(low, high, edge)), 1.0);
}
