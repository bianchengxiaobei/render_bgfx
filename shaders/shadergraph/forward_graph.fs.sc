$input v_texcoord0, v_texcoord1, v_positionOS, v_normalOS, v_tangentOS, v_bitangentOS, v_worldPos, v_normal, v_tangent, v_bitangent, v_color, v_vertexID, v_instanceID

#define HBG_CONTACT_SHADOW_TEXTURE 1

#include "common/common.sc"
#include "material/surface_data_types.sc"
#include "surface.sc"
#include "lighting/contact_shadow.sc"
#include "lighting/light_loop.sc"

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

    SurfaceData surfaceData;
    surfaceData.baseColor = graph.baseColor;
    surfaceData.alpha = graph.alpha;
    surfaceData.metallic = graph.metallic;
    surfaceData.perceptualRoughness = clamp(1.0 - graph.smoothness, 0.0, 1.0);
    surfaceData.roughness = max(surfaceData.perceptualRoughness * surfaceData.perceptualRoughness,
                                0.001);
    surfaceData.ambientOcclusion = graph.occlusion;
    surfaceData.materialAmbientOcclusion = graph.occlusion;
    surfaceData.normalWS = normalize(graph.normalWS);
    surfaceData.geometricNormalWS = normalize(v_normal);
    surfaceData.tangentWS = v_tangent;
    surfaceData.emissive = graph.emission;
    surfaceData.contactShadowMask = 0.0;
    surfaceData.contactShadowFade = 0.0;
    surfaceData.lightLayerMask = u_object_lightLayerMask.x;

    const vec2 contactShadowData =
        hbgSampleContactShadow(gl_FragCoord.xy * u_global_screenSize.zw);
    surfaceData.contactShadowMask = contactShadowData.x;
    surfaceData.contactShadowFade = contactShadowData.y;

#ifdef ALPHA_CLIP
    if (surfaceData.alpha < graph.alphaClipThreshold)
    {
        discard;
    }
#endif

    const float shadowVisibility =
        hbgSampleDirectionalShadow(v_worldPos, surfaceData.normalWS);
    const vec3 color = hbgEvaluateLightLoop(surfaceData, gi.viewDirWS, v_worldPos, shadowVisibility) +
                       surfaceData.emissive;
    gl_FragColor = vec4(max(color, vec3(0.0, 0.0, 0.0)), surfaceData.alpha);
}
