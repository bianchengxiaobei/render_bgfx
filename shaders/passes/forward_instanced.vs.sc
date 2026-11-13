$input a_position, a_normal, a_tangent, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_texcoord0, v_worldPos, v_normal, v_tangent, v_bitangent

#include "common/common.sc"

void main()
{
    mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
    vec4 worldPosition = mul(model, vec4(a_position, 1.0));
    vec3 worldNormal = normalize(mul(model, vec4(a_normal, 0.0)).xyz);
    vec3 tangentWorld = mul(model, vec4(a_tangent.xyz, 0.0)).xyz;
    float tangentLengthSq = dot(tangentWorld, tangentWorld);
    bool hasTangent = tangentLengthSq > 1.0e-12;
    vec3 worldTangent = hasTangent ? tangentWorld * inversesqrt(tangentLengthSq) : vec3(0.0, 0.0, 0.0);
    vec3 worldBitangent = hasTangent ? cross(worldNormal, worldTangent) * a_tangent.w : vec3(0.0, 0.0, 0.0);

    gl_Position = mul(u_global_viewProj, worldPosition);

    v_worldPos = worldPosition.xyz;
    v_normal = worldNormal;
    v_tangent = worldTangent;
    v_bitangent = worldBitangent;
    v_texcoord0 = a_texcoord0;
}
