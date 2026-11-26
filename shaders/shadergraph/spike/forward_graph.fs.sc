$input v_texcoord0, v_worldPos, v_normal, v_tangent, v_bitangent

#define HBG_CONTACT_SHADOW_TEXTURE 1

#include "material/surface_data_types.sc"
#include "shadergraph/spike/surface.spike.sc"
#include "lighting/contact_shadow.sc"
#include "lighting/light_loop.sc"

void main()
{
    const vec3 viewWS = u_global_cameraPos.xyz - v_worldPos;
    const vec3 normalWS = normalize(v_normal);
    SurfaceData surfaceData = hbgMakeGraphSurface(v_worldPos, normalWS, v_tangent, v_bitangent,
                                                  v_texcoord0);
    surfaceData.lightLayerMask = u_object_lightLayerMask.x;

    const vec2 contactShadowData =
        hbgSampleContactShadow(gl_FragCoord.xy * u_global_screenSize.zw);
    surfaceData.contactShadowMask = contactShadowData.x;
    surfaceData.contactShadowFade = contactShadowData.y;

    const float shadowVisibility = hbgSampleDirectionalShadow(v_worldPos, surfaceData.normalWS);
    const vec3 color = hbgEvaluateLightLoop(surfaceData, viewWS, v_worldPos, shadowVisibility) +
                       surfaceData.emissive;
    gl_FragColor = vec4(max(color, vec3(0.0, 0.0, 0.0)), surfaceData.alpha);
}
