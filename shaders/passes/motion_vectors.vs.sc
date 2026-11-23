$input a_position, a_texcoord2
$output v_currentClip, v_prevClip

#include "common/common.sc"

void main()
{
    const vec4 worldPosition = mul(u_model[0], vec4(a_position, 1.0));
    const vec4 previousWorldPosition = mul(u_object_prevModel, vec4(a_texcoord2, 1.0));

    v_currentClip = mul(u_global_nonJitteredViewProj, worldPosition);
    v_prevClip = mul(u_global_prevViewProj, previousWorldPosition);
    gl_Position = mul(u_global_viewProj, worldPosition);
}
