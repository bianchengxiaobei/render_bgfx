$input a_position, i_data0, i_data1, i_data2, i_data3

#include "common/common.sc"

void main()
{
    mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
    const vec4 worldPosition = mul(model, vec4(a_position, 1.0));
    gl_Position = mul(u_pass_shadowViewProj, worldPosition);
}
