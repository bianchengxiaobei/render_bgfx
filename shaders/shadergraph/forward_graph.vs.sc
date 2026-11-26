$input a_position, a_normal, a_tangent, a_texcoord0, a_texcoord1, a_color0
$output v_texcoord0, v_texcoord1, v_positionOS, v_normalOS, v_tangentOS, v_bitangentOS, v_worldPos, v_normal, v_tangent, v_bitangent, v_color, v_vertexID, v_instanceID

#include "common/common.sc"
#include "surface.sc"

void main()
{
    const vec4 sourceWorld = mul(u_model[0], vec4(a_position, 1.0));
    const vec3 sourceNormalWS = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
    const vec3 sourceTangentWS = mul(u_model[0], vec4(a_tangent.xyz, 0.0)).xyz;

    GraphSurfaceInputs gi;
    gi.uv0 = a_texcoord0;
    gi.uv1 = a_texcoord1;
    gi.positionOS = a_position;
    gi.positionWS = sourceWorld.xyz;
    gi.normalOS = a_normal;
    gi.normalWS = sourceNormalWS;
    gi.tangentOS = a_tangent.xyz;
    gi.tangentWS = sourceTangentWS;
    gi.bitangentOS = cross(a_normal, a_tangent.xyz) * a_tangent.w;
    gi.bitangentWS = cross(sourceNormalWS, sourceTangentWS) * a_tangent.w;
    gi.viewDirWS = u_global_cameraPos.xyz - sourceWorld.xyz;
    gi.screenPos = vec4(0.0, 0.0, 0.0, 1.0);
    gi.vertexColor = a_color0;
    gi.time = u_global_time.x;
    gi.deltaTime = u_global_time.y;
    gi.faceSign = 1.0;
    gi.vertexID = float(gl_VertexID);
    gi.instanceID = float(gl_InstanceID);

#ifdef HBG_SG_VERTEX_DISPLACEMENT
    GraphSurface graph = hbgEvaluateGraphSurface(gi);
    const vec3 displacedPositionOS = graph.positionOS;
#else
    const vec3 displacedPositionOS = a_position;
#endif
    const vec4 worldPosition = mul(u_model[0], vec4(displacedPositionOS, 1.0));

    const vec3 normalOS = a_normal;
    const vec3 tangentOS = a_tangent.xyz;
    const vec3 bitangentOS = cross(normalOS, tangentOS) * a_tangent.w;
    const vec3 worldNormal = normalize(mul(u_model[0], vec4(normalOS, 0.0)).xyz);
    const vec3 tangentWorld = mul(u_model[0], vec4(tangentOS, 0.0)).xyz;
    const float tangentLengthSq = dot(tangentWorld, tangentWorld);
    const bool hasTangent = tangentLengthSq > 1.0e-12;
    const vec3 worldTangent = hasTangent ? tangentWorld * inversesqrt(tangentLengthSq)
                                         : vec3(0.0, 0.0, 0.0);
    const vec3 worldBitangent = hasTangent ? normalize(cross(worldNormal, worldTangent)) *
                                                 sign(a_tangent.w)
                                           : vec3(0.0, 0.0, 0.0);

    gl_Position = mul(u_global_viewProj, worldPosition);

    v_texcoord0 = a_texcoord0;
    v_texcoord1 = a_texcoord1;
    v_positionOS = displacedPositionOS;
    v_normalOS = normalOS;
    v_tangentOS = tangentOS;
    v_bitangentOS = bitangentOS;
    v_worldPos = worldPosition.xyz;
    v_normal = worldNormal;
    v_tangent = worldTangent;
    v_bitangent = worldBitangent;
    v_color = a_color0;
    v_vertexID = gi.vertexID;
    v_instanceID = gi.instanceID;
}
