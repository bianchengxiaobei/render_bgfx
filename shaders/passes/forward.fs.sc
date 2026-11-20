$input v_texcoord0, v_texcoord1, v_worldPos, v_normal, v_tangent, v_bitangent

#define HBG_FORWARD_LIGHTMAP_GI 1

#include "material/surface_data.sc"
#include "lighting/contact_shadow.sc"
#include "lighting/light_loop.sc"

void main()
{
    vec3 viewWS = u_global_cameraPos.xyz - v_worldPos;
    vec3 normalWS = normalize(v_normal);

    // bgfx reports the glTF CCW front face as back-facing on these
    // backends; invert the sign so double-sided normals still point outward.
    float viewSign = gl_FrontFacing ? -1.0 : 1.0;
    SurfaceData surfaceData = hbgMakeSurfaceData(v_worldPos, normalWS, v_tangent, v_bitangent,
                                                 v_texcoord0, viewSign);
    surfaceData.lightLayerMask = u_object_lightLayerMask.x;

    const vec2 contactShadowData = hbgSampleContactShadow(gl_FragCoord.xy * u_global_screenSize.zw);
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
    gl_FragColor = vec4(max(color, vec3(0.0, 0.0, 0.0)), surfaceData.alpha);
}
