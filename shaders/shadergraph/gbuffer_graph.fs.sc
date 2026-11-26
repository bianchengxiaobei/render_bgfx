$input v_texcoord0, v_texcoord1, v_positionOS, v_normalOS, v_tangentOS, v_bitangentOS, v_worldPos, v_normal, v_tangent, v_bitangent, v_color, v_vertexID, v_instanceID

#include "common/common.sc"
#include "material/normal_buffer.sc"
#include "surface.sc"

void main()
{
    GraphSurfaceInputs gi;
    gi.uv0 = v_texcoord0;
    gi.uv1 = v_texcoord1;
    gi.positionOS = v_positionOS;
    gi.positionWS = v_worldPos;
    gi.normalOS = v_normalOS;
    gi.normalWS = normalize(v_normal);
    gi.tangentOS = v_tangentOS;
    gi.tangentWS = v_tangent;
    gi.bitangentOS = v_bitangentOS;
    gi.bitangentWS = v_bitangent;
    gi.viewDirWS = u_global_cameraPos.xyz - v_worldPos;
    gi.screenPos = vec4(gl_FragCoord.xy * u_global_screenSize.zw, gl_FragCoord.z, 1.0);
    gi.vertexColor = v_color;
    gi.time = u_global_time.x;
    gi.deltaTime = u_global_time.y;
    gi.faceSign = gl_FrontFacing ? -1.0 : 1.0;
    gi.vertexID = v_vertexID;
    gi.instanceID = v_instanceID;

    GraphSurface graph = hbgEvaluateGraphSurface(gi);

#ifdef ALPHA_CLIP
    if (graph.alpha < graph.alphaClipThreshold)
    {
        discard;
    }
#endif

    const float perceptualRoughness = clamp(1.0 - graph.smoothness, 0.0, 1.0);
    gl_FragData[0] = vec4(graph.baseColor, graph.alpha);
    gl_FragData[1] = vec4(graph.metallic, graph.occlusion, 0.0, 0.0);
    gl_FragData[2] = hbgEncodeNormalBuffer(normalize(graph.normalWS), perceptualRoughness);
    gl_FragData[3] = vec4(max(graph.emission, vec3(0.0, 0.0, 0.0)), 1.0);
    gl_FragData[4] = vec4(0.0, 0.0, 0.0, 1.0);
}
