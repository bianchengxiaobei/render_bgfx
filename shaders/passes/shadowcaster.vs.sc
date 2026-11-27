$input a_position, a_texcoord0
$output v_texcoord0

#include "common/common.sc"

void main()
{
    const vec4 worldPosition = mul(u_model[0], vec4(a_position, 1.0));
    v_texcoord0 = a_texcoord0;
    gl_Position = mul(u_pass_shadowViewProj, worldPosition);
}
