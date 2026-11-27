$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_hdrColor, 0);

void main()
{
    gl_FragColor = texture2D(s_hdrColor, v_texcoord0);
}
