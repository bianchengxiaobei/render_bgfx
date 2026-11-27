$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_riverDepthSource, 0);

void main()
{
    float depth = texture2D(s_riverDepthSource, v_texcoord0).r;
    gl_FragColor = vec4(depth, 0.0, 0.0, 1.0);
}
