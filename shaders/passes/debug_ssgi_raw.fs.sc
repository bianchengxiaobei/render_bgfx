$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_ssgi, 0);

void main()
{
    const vec3 indirect = max(texture2D(s_ssgi, v_texcoord0).rgb, vec3(0.0, 0.0, 0.0));
    gl_FragColor = vec4(min(indirect, vec3(8.0, 8.0, 8.0)), 1.0);
}
