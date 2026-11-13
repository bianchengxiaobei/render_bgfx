$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_ssaoInput, 0);

void main()
{
    const float visibility = texture2D(s_ssaoInput, v_texcoord0).r;
    gl_FragColor = vec4(visibility, visibility, visibility, 1.0);
}
