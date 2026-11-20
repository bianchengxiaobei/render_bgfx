$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_bakedLightmap, 0);

void main()
{
    const vec4 baked = texture2D(s_bakedLightmap, v_texcoord0);
    const float mask = step(0.5, baked.a);
    const vec3 radiance = max(baked.rgb, vec3(0.0, 0.0, 0.0)) * max(u_debug_lightmapParams.x, 0.0);
    const vec3 maskView = mix(vec3(0.015, 0.015, 0.015), vec3(1.0, 0.0, 0.0), mask);
    const vec3 color = mix(radiance, maskView, step(0.5, u_debug_lightmapParams.y));
    gl_FragColor = vec4(color, 1.0);
}
