$input a_position

#include "common/common.sc"

void main()
{
    const vec4 worldPosition = mul(u_model[0], vec4(a_position, 1.0));
    gl_Position = mul(u_pass_shadowViewProj, worldPosition);
}
