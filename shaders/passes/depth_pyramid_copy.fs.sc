$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_depthSource, 0);

void main()
{
    const float depth = texture2D(s_depthSource, v_texcoord0).r;
    gl_FragColor = vec4(depth, depth, 0.0, 1.0);
}
