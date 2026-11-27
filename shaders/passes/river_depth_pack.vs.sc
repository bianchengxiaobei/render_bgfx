$input a_position
$output v_texcoord0

#include "common/common.sc"

uniform vec4 u_riverDepthPackSize;

void main()
{
    v_texcoord0 = a_position.xy / u_riverDepthPackSize.xy;
    float x = 2.0 * (a_position.z + 0.5) / u_riverDepthPackSize.z - 1.0;
    gl_Position = vec4(x, 0.0, 0.0, 1.0);
}
