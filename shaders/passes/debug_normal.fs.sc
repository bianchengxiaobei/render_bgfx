$input v_texcoord0

#include "common/common.sc"
#include "material/normal_buffer.sc"

SAMPLER2D(s_normalBuffer, 0);

void main()
{
    const vec4 normalBuffer = texture2D(s_normalBuffer, v_texcoord0);
    if (normalBuffer.r == 0.0 && normalBuffer.g == 0.0 && normalBuffer.b == 0.0)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const vec3 normalWS = hbgDecodeNormalBuffer(normalBuffer);
    gl_FragColor = vec4(normalWS * 0.5 + 0.5, 1.0);
}
