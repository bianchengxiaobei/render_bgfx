$input v_uv, v_color, v_worldPos, v_normal

#include "material/surface_data_types.sc"
#include "lighting/contact_shadow.sc"
#include "lighting/light_loop.sc"

void main()
{
    SurfaceData surfaceData;
    surfaceData.baseColor = v_color.rgb;
    surfaceData.alpha = v_color.a;
    surfaceData.metallic = 0.0;
    surfaceData.perceptualRoughness = 0.5;
    surfaceData.roughness = 0.25;
    surfaceData.ambientOcclusion = 1.0;
    surfaceData.materialAmbientOcclusion = 0.0;
    surfaceData.normalWS = normalize(v_normal);
    surfaceData.geometricNormalWS = surfaceData.normalWS;
    surfaceData.tangentWS = normalize(cross(surfaceData.normalWS, vec3(0.0, 1.0, 0.0)));
    surfaceData.emissive = vec3(0.0, 0.0, 0.0);
    surfaceData.contactShadowMask = 0.0;
    surfaceData.contactShadowFade = 0.0;
    surfaceData.lightLayerMask = 1.0;

    const vec3 viewWS = u_global_cameraPos.xyz - v_worldPos;
    const float shadowVisibility = hbgSampleDirectionalShadow(v_worldPos, surfaceData.normalWS);
    const vec3 color = hbgEvaluateLightLoop(surfaceData, viewWS, v_worldPos, shadowVisibility);
    gl_FragColor = vec4(max(color, vec3(0.0, 0.0, 0.0)), surfaceData.alpha);
}
