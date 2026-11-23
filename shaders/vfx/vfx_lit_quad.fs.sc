$input v_uv, v_color, v_worldPos, v_normal

#include "bgfx_shader.sh"
#include "material/surface_data_types.sc"

#define HBG_CONTACT_SHADOW_TEXTURE 1
#include "lighting/contact_shadow.sc"
#include "lighting/light_loop.sc"

SAMPLER2D(s_vfxMain, 0);
uniform vec4 u_vfxProp_Metallic;
uniform vec4 u_vfxProp_Roughness;

void main()
{
    const vec4 texel = texture2D(s_vfxMain, v_uv);
    SurfaceData surfaceData;
    surfaceData.baseColor = v_color.rgb * texel.rgb;
    surfaceData.alpha = v_color.a * texel.a;
    surfaceData.metallic = hbgSaturate(u_vfxProp_Metallic.x);
    surfaceData.perceptualRoughness = hbgSaturate(u_vfxProp_Roughness.x);
    surfaceData.roughness = max(surfaceData.perceptualRoughness * surfaceData.perceptualRoughness, 0.001);
    surfaceData.ambientOcclusion = 1.0;
    surfaceData.materialAmbientOcclusion = 1.0;
    const vec3 normalWS = normalize(v_normal);
    surfaceData.normalWS = normalWS;
    surfaceData.geometricNormalWS = normalWS;
    const vec3 tangentHint = abs(normalWS.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    surfaceData.tangentWS = normalize(cross(normalWS, tangentHint));
    surfaceData.emissive = vec3(0.0, 0.0, 0.0);
    const vec2 contactShadowData = hbgSampleContactShadow(gl_FragCoord.xy * u_global_screenSize.zw);
    surfaceData.contactShadowMask = contactShadowData.x;
    surfaceData.contactShadowFade = contactShadowData.y;
    surfaceData.lightLayerMask = 65535.0;

    const vec3 viewWS = u_global_cameraPos.xyz - v_worldPos;
    const float shadowVisibility = hbgSampleDirectionalShadow(v_worldPos, normalWS);
    const vec3 color = hbgEvaluateLightLoop(surfaceData, viewWS, v_worldPos, shadowVisibility) +
                       surfaceData.emissive;
    gl_FragColor = vec4(max(color, vec3(0.0, 0.0, 0.0)), surfaceData.alpha);
}
