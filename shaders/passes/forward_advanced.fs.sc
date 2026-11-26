$input v_texcoord0, v_texcoord1, v_worldPos, v_normal, v_tangent, v_bitangent

#define HBG_FORWARD_LIGHTMAP_GI 1
#define HBG_SHADOW_ADVANCED_DEPTH_FILTERS 1
#define HBG_SHADOW_IMS_SAT_STAGE 12

#include "material/surface_data.sc"
#include "material/surface_data_advanced.sc"
#include "lighting/contact_shadow.sc"
#include "lighting/light_loop.sc"
#include "lighting/refraction.sc"

void main()
{
    vec3 viewWS = u_global_cameraPos.xyz - v_worldPos;
    vec3 normalWS = normalize(v_normal);

    float viewSign = gl_FrontFacing ? -1.0 : 1.0;
    SurfaceData surfaceData = hbgMakeSurfaceData(v_worldPos, normalWS, v_tangent, v_bitangent,
                                                 v_texcoord0, viewSign);
    surfaceData = hbgApplyAdvancedMaterial(surfaceData, v_tangent, v_bitangent, normalWS);
    surfaceData.lightLayerMask = u_object_lightLayerMask.x;

    const vec2 screenUV = gl_FragCoord.xy * u_global_screenSize.zw;
    const vec2 contactShadowData = hbgSampleContactShadow(screenUV);
    surfaceData.contactShadowMask = contactShadowData.x;
    surfaceData.contactShadowFade = contactShadowData.y;

#ifdef ALPHA_CLIP
    if (surfaceData.alpha < u_material_metallicRoughness.w)
    {
        discard;
    }
#endif

    const float shadowVisibility = hbgSampleDirectionalShadow(v_worldPos, surfaceData.normalWS);
    vec3 color = hbgEvaluateLightLoop(surfaceData, viewWS, v_worldPos, shadowVisibility) +
                 surfaceData.emissive;
    color = hbgApplyScreenSpaceRefraction(color, v_worldPos, viewWS, surfaceData.normalWS, screenUV,
                                          surfaceData.transmissionMask,
                                          max(surfaceData.thickness, 0.0),
                                          max(surfaceData.ior, 1.0),
                                          hbgRefractionFeature(u_material_advanced2.z),
                                          surfaceData.perceptualRoughness);
    gl_FragColor = vec4(max(color, vec3(0.0, 0.0, 0.0)), surfaceData.alpha);
}
