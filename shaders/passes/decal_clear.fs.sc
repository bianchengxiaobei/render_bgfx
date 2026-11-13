$input v_texcoord0

#include "common/common.sc"

void main()
{
    gl_FragData[0] = vec4(0.0, 0.0, 0.0, 1.0);
    gl_FragData[1] = vec4(0.5, 0.5, 0.5, 1.0);
    gl_FragData[2] = vec4(0.0, 0.0, 0.0, 1.0);
}
