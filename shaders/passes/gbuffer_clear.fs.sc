$input v_texcoord0

#include "common/common.sc"
#include "material/normal_buffer.sc"

void main()
{
    gl_FragData[0] = vec4(0.0, 0.0, 0.0, 0.0);
    gl_FragData[1] = vec4(0.0, 1.0, 0.0, 0.0);
    gl_FragData[2] = hbgEncodeNormalBuffer(vec3(0.0, 0.0, 1.0), 1.0);
    gl_FragData[3] = vec4(0.0, 0.0, 0.0, 1.0);
}
