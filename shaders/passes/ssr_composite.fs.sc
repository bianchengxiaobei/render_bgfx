$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_ssr, 0);

void main()
{
    const vec3 reflection = max(texture2D(s_ssr, v_texcoord0).rgb, vec3(0.0, 0.0, 0.0));
    gl_FragColor = vec4(reflection, 0.0);
}
