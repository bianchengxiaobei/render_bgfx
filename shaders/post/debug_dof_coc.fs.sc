$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_dofCoc, 0);

void main()
{
    const float coc = clamp(texture2D(s_dofCoc, v_texcoord0).x, -1.0, 1.0);
    const float magnitude = hbgSaturate(abs(coc));
    const vec3 farColor = mix(vec3(0.05, 0.0, 0.0), vec3(1.0, 0.15, 0.05), magnitude);
    const vec3 nearColor = mix(vec3(0.0, 0.02, 0.05), vec3(0.05, 0.35, 1.0), magnitude);
    gl_FragColor = vec4(coc < 0.0 ? nearColor : farColor, 1.0);
}
