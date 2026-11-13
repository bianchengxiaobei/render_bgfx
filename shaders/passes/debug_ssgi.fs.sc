$input v_texcoord0

#include "common/common.sc"
#include "lighting/ssgi_filter.sc"

void main()
{
    float centerDepth = 0.0;
    const vec3 indirect = hbgSampleSsgiFiltered(v_texcoord0, centerDepth);
    gl_FragColor = vec4(min(max(indirect, vec3(0.0, 0.0, 0.0)), vec3(8.0, 8.0, 8.0)), 1.0);
}
